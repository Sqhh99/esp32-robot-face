#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GROK_FACE_IDLE = 0,
    GROK_FACE_THINKING,
    GROK_FACE_WINK,
    GROK_FACE_WIDE,
    GROK_FACE_ALERT,
    GROK_FACE_NOTIFY,
    GROK_FACE_SLEEP,
    GROK_FACE_EGG,
    GROK_FACE_HEXAGON,
    GROK_FACE_PLAY,
    GROK_FACE_ORBIT,
    GROK_FACE_BURST,
    GROK_FACE_COMET,
    GROK_FACE_EXPRESSION_COUNT,
} grok_face_expression_t;

/**
 * Returns a writable buffer for one horizontal band.
 *
 * The buffer must hold width * band_rows RGB565 pixels and be aligned to at
 * least four bytes. If the backend transfers with DMA, it must also be
 * DMA-capable. The callback may block until a buffer is available.
 */
typedef uint16_t *(*grok_face_acquire_buffer_cb_t)(void *user_ctx);

/**
 * Queues or sends one horizontal band.
 *
 * The buffer belongs to the display backend until its transfer completes.
 */
typedef esp_err_t (*grok_face_flush_band_cb_t)(void *user_ctx, int y_start,
                                               int row_count,
                                               const uint16_t *pixels);

typedef struct {
    int width;
    int height;
    int band_rows;
    float center_x;
    float center_y;
    float radius;
    bool swap_color_bytes;
    grok_face_acquire_buffer_cb_t acquire_buffer;
    grok_face_flush_band_cb_t flush_band;
    void *user_ctx;
} grok_face_config_t;

/**
 * Initializes the singleton renderer and clears every display band.
 *
 * Width must be even, height must be divisible by band_rows, and callbacks
 * must remain valid for the lifetime of the component. Initialize once and
 * call all component APIs from the same task.
 */
esp_err_t grok_face_init(const grok_face_config_t *config);

/** Renders and flushes exactly one animation frame. */
esp_err_t grok_face_render_frame(void);

/** Selects an expression and starts its transition from the current one. */
esp_err_t grok_face_set_expression(grok_face_expression_t expression);

/** Advances to the next expression, wrapping after GROK_FACE_COMET. */
esp_err_t grok_face_next_expression(void);

/** Returns the selected expression, or GROK_FACE_IDLE before initialization. */
grok_face_expression_t grok_face_get_expression(void);

/** Returns a stable lowercase name, or "unknown" for an invalid value. */
const char *grok_face_expression_name(grok_face_expression_t expression);

#ifdef __cplusplus
}
#endif
