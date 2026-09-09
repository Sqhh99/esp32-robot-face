#include "display.h"

#include "config.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Board wiring copied from the working ../20_Camera project.
#define LCD_HOST SPI2_HOST
#define LCD_SCLK GPIO_NUM_12
#define LCD_MOSI GPIO_NUM_11
#define LCD_MISO GPIO_NUM_13
#define LCD_CS GPIO_NUM_48
#define LCD_DC GPIO_NUM_47
#define LCD_RST GPIO_NUM_21
#define LCD_BACKLIGHT GPIO_NUM_40

#define LCD_PIXEL_CLOCK_HZ (60 * 1000 * 1000)
#define BAND_PIXELS (LCD_H_RES * BAND_ROWS)

static const char *TAG = "display";

// Two internal-SRAM buffers let rendering overlap the previous SPI transfer.
static DMA_ATTR uint16_t s_band_buf[2][BAND_PIXELS];

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_buffers_free;

static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t io,
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

static esp_err_t send_panel_init_commands(void)
{
    // Power, direction and gamma settings from the known-good 20_Camera LCD
    // driver. The generic IDF driver has already sent sleep-out and RGB565.
    static const uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    static const uint8_t gamma_pos[] = {
        0x70, 0x05, 0x0A, 0x0B, 0x0A, 0x27, 0x2F,
        0x44, 0x47, 0x37, 0x14, 0x14, 0x29, 0x2F,
    };
    static const uint8_t gamma_neg[] = {
        0x70, 0x07, 0x0C, 0x08, 0x08, 0x04, 0x2F,
        0x33, 0x46, 0x18, 0x15, 0x15, 0x2B, 0x2D,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0xB2, porch, sizeof(porch)),
                        TAG, "porch setting failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0x35, (uint8_t[]){0x00}, 1),
                        TAG, "tearing setting failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0x36, (uint8_t[]){0x70}, 1),
                        TAG, "orientation setting failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0x3A, (uint8_t[]){0x05}, 1),
                        TAG, "pixel format setting failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0xB7, (uint8_t[]){0x35}, 1),
                        TAG, "gate setting failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0xBB, (uint8_t[]){0x2D}, 1),
                        TAG, "vcom setting failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0xC0, (uint8_t[]){0x2C}, 1),
                        TAG, "lcm setting failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0xC2, (uint8_t[]){0x01}, 1),
                        TAG, "vdv setting failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0xC3, (uint8_t[]){0x15}, 1),
                        TAG, "vrh setting failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0xC4, (uint8_t[]){0x20}, 1),
                        TAG, "vdv offset setting failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0xC6, (uint8_t[]){0x0F}, 1),
                        TAG, "frame rate setting failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0xD0, (uint8_t[]){0xA4, 0xA1}, 2),
                        TAG, "power setting failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0xD6, (uint8_t[]){0xA1}, 1),
                        TAG, "gate control setting failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0xE0, gamma_pos, sizeof(gamma_pos)),
                        TAG, "positive gamma setting failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0xE1, gamma_neg, sizeof(gamma_neg)),
                        TAG, "negative gamma setting failed");
    return ESP_OK;
}

esp_err_t display_init(void)
{
    const gpio_config_t backlight_config = {
        .pin_bit_mask = 1ULL << LCD_BACKLIGHT,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&backlight_config), TAG, "backlight GPIO init failed");
    gpio_set_level(LCD_BACKLIGHT, 0);

    s_buffers_free = xSemaphoreCreateCounting(2, 2);
    ESP_RETURN_ON_FALSE(s_buffers_free != NULL, ESP_ERR_NO_MEM, TAG,
                        "band buffer semaphore allocation failed");

    const spi_bus_config_t bus_config = {
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = LCD_MISO,
        .sclk_io_num = LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BAND_PIXELS * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO),
                        TAG, "SPI bus init failed");

    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_CS,
        .dc_gpio_num = LCD_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 2,
        .on_color_trans_done = on_color_trans_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &s_io),
        TAG, "panel IO init failed");

    const esp_lcd_panel_dev_config_t panel_config = {
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
        .reset_gpio_num = LCD_RST,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(s_io, &panel_config, &s_panel),
                        TAG, "ST7789 panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel setup failed");
    ESP_RETURN_ON_ERROR(send_panel_init_commands(), TAG, "board panel setup failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG,
                        "panel inversion failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG,
                        "display on failed");

    ESP_LOGI(TAG, "ST7789 ready: 240x240, SPI 60 MHz");
    return ESP_OK;
}

uint16_t *display_acquire_band(void)
{
    xSemaphoreTake(s_buffers_free, portMAX_DELAY);

    static unsigned next;
    return s_band_buf[next++ & 1];
}

esp_err_t display_flush_rows(int y_start, int row_count, const uint16_t *buffer)
{
    if (buffer == NULL || y_start < 0 || row_count <= 0 ||
        y_start + row_count > LCD_V_RES) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_lcd_panel_draw_bitmap(s_panel, 0, y_start, LCD_H_RES,
                                     y_start + row_count, buffer);
}

esp_err_t display_set_backlight(bool on)
{
    return gpio_set_level(LCD_BACKLIGHT, on ? 1 : 0);
}
