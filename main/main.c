// Bloub on the board's 240x240 ST7789 LCD.
//
// The demo drives all four of the component's axes at once: the animated state,
// the resting expression, the resting body shape and the body colour, each on
// its own period.

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

/** A deadline that has come round, rearmed for the next one. */
static bool due(int64_t now_us, int64_t *next_us, int period_ms)
{
    if (now_us < *next_us) {
        return false;
    }
    *next_us = now_us + (int64_t)period_ms * 1000;
    return true;
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
        .background = FACE_BACKGROUND,
        .acquire_buffer = face_acquire_buffer,
        .flush_band = face_flush_band,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(grok_face_init(&face_config));
    ESP_ERROR_CHECK(display_set_backlight(true));

    // Start on a resting expression and shape so the two customiser axes are
    // showing from the first frame rather than after the first period.
    ESP_ERROR_CHECK(grok_face_set_expression(GROK_FACE_EXPR_NEUTRE));
    ESP_ERROR_CHECK(grok_face_set_shape(GROK_FACE_SHAPE_CERCLE));

    int expression = GROK_FACE_EXPR_NEUTRE;
    int shape = GROK_FACE_SHAPE_CERCLE;
    int color = grok_face_get_color();

    const int64_t start_us = esp_timer_get_time();
    int64_t next_state_us = start_us + (int64_t)STATE_HOLD_MS * 1000;
    int64_t next_expr_us = start_us + (int64_t)EXPR_HOLD_MS * 1000;
    int64_t next_shape_us = start_us + (int64_t)SHAPE_HOLD_MS * 1000;
    int64_t next_color_us = start_us + (int64_t)COLOR_HOLD_MS * 1000;

    for (;;) {
        ESP_ERROR_CHECK(grok_face_render_frame());

        const int64_t now_us = esp_timer_get_time();
        if (due(now_us, &next_state_us, STATE_HOLD_MS)) {
            ESP_ERROR_CHECK(grok_face_next_state());
        }
        if (due(now_us, &next_expr_us, EXPR_HOLD_MS)) {
            expression = (expression + 1) % GROK_FACE_EXPR_COUNT;
            ESP_ERROR_CHECK(grok_face_set_expression(expression));
        }
        if (due(now_us, &next_shape_us, SHAPE_HOLD_MS)) {
            shape = (shape + 1) % GROK_FACE_SHAPE_COUNT;
            ESP_ERROR_CHECK(grok_face_set_shape(shape));
        }
        if (due(now_us, &next_color_us, COLOR_HOLD_MS)) {
            color = (color + 1) % GROK_FACE_COLOR_COUNT;
            ESP_ERROR_CHECK(grok_face_set_color(color));
        }

        // DMA waits pace rendering; this also guarantees idle-task time.
        vTaskDelay(1);
    }
}
