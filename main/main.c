// Bloub: thirteen animated expressions on the board's 240x240 ST7789 LCD.
// Each state is shown for four seconds before advancing automatically.

#include "config.h"
#include "display.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "grok_face.h"

static const char *TAG = "grok_face_app";

static uint16_t *face_acquire_buffer(void *user_ctx)
{
    (void)user_ctx;
    return display_acquire_band();
}

static esp_err_t face_flush_band(void *user_ctx, int y_start, int row_count,
                                 const uint16_t *pixels)
{
    (void)user_ctx;
    return display_flush_rows(y_start, row_count, pixels);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Grok face starting");

    ESP_ERROR_CHECK(display_init());
    const grok_face_config_t face_config = {
        .width = LCD_H_RES,
        .height = LCD_V_RES,
        .band_rows = BAND_ROWS,
        .center_x = BALL_CX,
        .center_y = BALL_CY,
        .radius = BALL_R,
        .swap_color_bytes = true,
        .acquire_buffer = face_acquire_buffer,
        .flush_band = face_flush_band,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(grok_face_init(&face_config));
    ESP_ERROR_CHECK(display_set_backlight(true));

    int64_t next_state_at_us =
        esp_timer_get_time() + (int64_t)STATE_HOLD_MS * 1000;

    for (;;) {
        ESP_ERROR_CHECK(grok_face_render_frame());

        const int64_t now_us = esp_timer_get_time();
        if (now_us >= next_state_at_us) {
            ESP_ERROR_CHECK(grok_face_next_expression());
            next_state_at_us = now_us + (int64_t)STATE_HOLD_MS * 1000;
        }

        // DMA waits pace rendering; this also guarantees idle-task time.
        vTaskDelay(1);
    }
}
