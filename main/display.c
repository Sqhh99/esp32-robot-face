#include "display.h"

#include "config.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Pins for the Waveshare ESP32-S3-Touch-AMOLED-1.75 (466x466 round panel),
// per Waveshare's own reference BSP.
#define LCD_HOST SPI2_HOST
#define LCD_CS GPIO_NUM_12
#define LCD_PCLK GPIO_NUM_38
#define LCD_RST GPIO_NUM_39
#define LCD_DATA0 GPIO_NUM_4
#define LCD_DATA1 GPIO_NUM_5
#define LCD_DATA2 GPIO_NUM_6
#define LCD_DATA3 GPIO_NUM_7

// The panel's addressable window starts 6 pixels in, per Waveshare's own
// init sequence for this board.
#define PANEL_X_GAP 6

// The physical panel is 466 rows tall; LCD_V_RES is trimmed to 464 so
// BAND_ROWS divides it evenly (see config.h). The renderer never touches the
// remaining PANEL_DEAD_ROWS rows at the bottom, so display_init() blanks them
// once at startup instead of leaving whatever was in GRAM on power-up.
#define PANEL_V_RES 466
#define PANEL_DEAD_ROWS (PANEL_V_RES - LCD_V_RES)

#define LCD_CMD_BRIGHTNESS 0x51

// 80 MHz is the ESP32-S3 QSPI ceiling; drop to 40 MHz if the panel ever shows
// tearing or corrupted pixels.
#define LCD_PIXEL_CLOCK_HZ (80 * 1000 * 1000)

#define BAND_PIXELS (LCD_H_RES * BAND_ROWS)

static const char *TAG = "display";

// Two buffers, alternating by acquisition, so one can be filled while the
// other is being transmitted. DMA_ATTR keeps them in DMA-capable internal
// SRAM.
static DMA_ATTR uint16_t s_band_buf[2][BAND_PIXELS];

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_buffers_free;

// Waveshare's own power-on sequence for this panel: pixel format RGB565,
// brightness and HBM registers open, full-window address set (6..0x1D7 x,
// 0..0x1D1 y, matching the 6 px PANEL_X_GAP and 466x464 window this project
// drives), sleep out, display on.
static const co5300_lcd_init_cmd_t s_init_cmds[] = {
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, NULL, 0, 600},
    {0x29, NULL, 0, 0},
};

static bool IRAM_ATTR on_trans_done(esp_lcd_panel_io_handle_t io,
                                    esp_lcd_panel_io_event_data_t *event,
                                    void *user_ctx)
{
    (void)io;
    (void)event;
    (void)user_ctx;

    BaseType_t higher_priority_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_buffers_free, &higher_priority_woken);
    return higher_priority_woken == pdTRUE;
}

esp_err_t display_init(void)
{
    s_buffers_free = xSemaphoreCreateCounting(2, 2);
    if (s_buffers_free == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const spi_bus_config_t bus_config = CO5300_PANEL_BUS_QSPI_CONFIG(
        LCD_PCLK, LCD_DATA0, LCD_DATA1, LCD_DATA2, LCD_DATA3,
        BAND_PIXELS * sizeof(uint16_t));
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO),
                        TAG, "spi bus init failed");

    esp_lcd_panel_io_spi_config_t io_config =
        CO5300_PANEL_IO_QSPI_CONFIG(LCD_CS, on_trans_done, NULL);
    io_config.pclk_hz = LCD_PIXEL_CLOCK_HZ;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &s_io),
        TAG, "panel io init failed");

    const co5300_vendor_config_t vendor_config = {
        .init_cmds = s_init_cmds,
        .init_cmds_size = sizeof(s_init_cmds) / sizeof(s_init_cmds[0]),
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(s_io, &panel_config, &s_panel),
                        TAG, "panel init failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel setup failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, PANEL_X_GAP, 0),
                        TAG, "panel gap failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "display on failed");

#if PANEL_DEAD_ROWS > 0
    // Blank the strip below LCD_V_RES once; the renderer never draws there.
    static const uint16_t s_dead_rows[LCD_H_RES * PANEL_DEAD_ROWS];
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_draw_bitmap(s_panel, 0, LCD_V_RES, LCD_H_RES, PANEL_V_RES, s_dead_rows),
        TAG, "dead row clear failed");
#endif

    return ESP_OK;
}

uint16_t *display_acquire_band(void)
{
    xSemaphoreTake(s_buffers_free, portMAX_DELAY);

    static unsigned next;
    return s_band_buf[next++ & 1];
}

esp_err_t display_flush_band(int band_index, const uint16_t *buffer)
{
    const int y0 = band_index * BAND_ROWS;
    return esp_lcd_panel_draw_bitmap(s_panel, 0, y0, LCD_H_RES, y0 + BAND_ROWS, buffer);
}

esp_err_t display_set_brightness(uint8_t level)
{
    return esp_lcd_panel_io_tx_param(s_io, LCD_CMD_BRIGHTNESS, &level, 1);
}
