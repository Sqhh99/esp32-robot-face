#include "touch.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_log.h"

// Per Waveshare's own BSP for this board: touch reset is a separate line from
// the panel's own reset, and INT is the only pin the AMOLED-1.75 (as opposed
// to the AMOLED-1.8) frees up for this purpose.
#define TOUCH_RST_GPIO GPIO_NUM_40
#define TOUCH_INT_GPIO GPIO_NUM_11

// Physical panel resolution (not the 464-row trim the renderer draws into) —
// touch coordinates are reported against the full panel.
#define TOUCH_X_MAX 466
#define TOUCH_Y_MAX 466

static const char *TAG = "touch";

static esp_lcd_touch_handle_t s_touch;
static bool s_was_down;

esp_err_t touch_init(i2c_master_bus_handle_t bus)
{
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    // The macro leaves this at 0; the I2C driver rejects a 0 Hz clock, so it
    // must be set explicitly (Waveshare's own BSP does the same after
    // expanding this macro).
    io_config.scl_speed_hz = 400000;
    esp_err_t ret = esp_lcd_new_panel_io_i2c(bus, &io_config, &io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "touch io init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = TOUCH_X_MAX,
        .y_max = TOUCH_Y_MAX,
        .rst_gpio_num = TOUCH_RST_GPIO,
        .int_gpio_num = TOUCH_INT_GPIO,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
    };
    ret = esp_lcd_touch_new_i2c_cst9217(io_handle, &tp_cfg, &s_touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "touch controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "CST9217 touch ready");
    return ESP_OK;
}

bool touch_take_tap(void)
{
    if (s_touch == NULL) {
        return false;
    }

    if (esp_lcd_touch_read_data(s_touch) != ESP_OK) {
        return false;
    }

    esp_lcd_touch_point_data_t point;
    uint8_t point_num = 0;
    const bool down =
        esp_lcd_touch_get_data(s_touch, &point, &point_num, 1) == ESP_OK && point_num > 0;

    const bool tap_started = down && !s_was_down;
    s_was_down = down;
    return tap_started;
}
