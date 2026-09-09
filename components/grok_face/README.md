# grok_face component

`grok_face` is a display-independent ESP-IDF component that renders thirteen
animated Grok/Bloub expressions into caller-owned RGB565 band buffers.

The component owns animation state and timing. It does not initialize a panel,
allocate DMA buffers, create a task or decide when expressions change.

## Add to a project

Copy this directory to `components/grok_face`, then add `grok_face` to the
calling component's `REQUIRES` or `PRIV_REQUIRES`.

```c
#include "grok_face.h"

static uint16_t *acquire_buffer(void *ctx)
{
    return my_display_acquire_buffer(ctx);
}

static esp_err_t flush_band(void *ctx, int y, int rows,
                            const uint16_t *pixels)
{
    return my_display_flush_rows(ctx, y, rows, pixels);
}

void face_start(void)
{
    const grok_face_config_t config = {
        .width = 240,
        .height = 240,
        .band_rows = 16,
        .center_x = 120.0f,
        .center_y = 117.0f,
        .radius = 75.0f,
        .swap_color_bytes = true,
        .acquire_buffer = acquire_buffer,
        .flush_band = flush_band,
        .user_ctx = NULL,
    };

    ESP_ERROR_CHECK(grok_face_init(&config));
}
```

Call `grok_face_render_frame()` repeatedly from one task. Change expressions
with `grok_face_next_expression()` or `grok_face_set_expression()`.

## Buffer contract

- Each acquired buffer must hold `width * band_rows` RGB565 pixels.
- The buffer must be at least four-byte aligned.
- `height` must be divisible by `band_rows`, and `width` must be even.
- An asynchronous display backend must not return a buffer from
  `acquire_buffer` until its previous flush has completed.
- Set `swap_color_bytes` when the panel expects the high RGB565 byte first
  but pixels are stored as native `uint16_t` values on the ESP32.

The component is a singleton and is not thread-safe; initialize it once and
call its APIs from the same render task.
