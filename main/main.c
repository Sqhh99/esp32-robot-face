// Bloub on the board's 240x240 ST7789 LCD.
//
// The four board keys drive the component's independent animation axes.

#include "config.h"
#include "display.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "grok_face.h"
#include "keys.h"

static const char *TAG = "grok_face_app";
static grok_face_state_t s_state_cycle_cursor = GROK_FACE_STATE_IDLE;

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

static void handle_key(key_event_t key)
{
    switch (key) {
    case KEY_EVENT_1: {
        grok_face_expression_t expression = (grok_face_expression_t)(
            (grok_face_get_expression() + 1) % GROK_FACE_EXPR_COUNT);
        ESP_ERROR_CHECK(grok_face_set_state(GROK_FACE_STATE_IDLE));
        ESP_ERROR_CHECK(grok_face_set_expression(expression));
        ESP_LOGI(TAG, "KEY1: expression -> %s", grok_face_expression_name(expression));
        break;
    }
    case KEY_EVENT_2: {
        grok_face_shape_t shape =
            (grok_face_shape_t)((grok_face_get_shape() + 1) % GROK_FACE_SHAPE_COUNT);
        ESP_ERROR_CHECK(grok_face_set_state(GROK_FACE_STATE_IDLE));
        ESP_ERROR_CHECK(grok_face_set_shape(shape));
        ESP_LOGI(TAG, "KEY2: shape -> %s", grok_face_shape_name(shape));
        break;
    }
    case KEY_EVENT_3: {
        grok_face_color_t color =
            (grok_face_color_t)((grok_face_get_color() + 1) % GROK_FACE_COLOR_COUNT);
        ESP_ERROR_CHECK(grok_face_set_color(color));
        ESP_LOGI(TAG, "KEY3: color -> %s", grok_face_color_name(color));
        break;
    }
    case KEY_EVENT_4: {
        s_state_cycle_cursor =
            (grok_face_state_t)((s_state_cycle_cursor + 1) % GROK_FACE_STATE_COUNT);
        ESP_ERROR_CHECK(grok_face_set_state(s_state_cycle_cursor));
        ESP_LOGI(TAG, "KEY4: state -> %s", grok_face_state_name(s_state_cycle_cursor));
        break;
    }
    case KEY_EVENT_NONE:
    default:
        break;
    }
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
    ESP_ERROR_CHECK(keys_init());
    ESP_ERROR_CHECK(display_set_backlight(true));

    // Start on a resting expression and shape so the two customiser axes are
    // showing from the first frame rather than after the first period.
    ESP_ERROR_CHECK(grok_face_set_expression(GROK_FACE_EXPR_NEUTRE));
    ESP_ERROR_CHECK(grok_face_set_shape(GROK_FACE_SHAPE_CERCLE));

    for (;;) {
        ESP_ERROR_CHECK(grok_face_render_frame());
        handle_key(keys_poll());

        // DMA waits pace rendering; this also guarantees idle-task time.
        vTaskDelay(1);
    }
}
