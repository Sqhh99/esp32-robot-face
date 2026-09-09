# grok_face component

`grok_face` is a display-independent ESP-IDF component that renders an animated
Grok/Bloub avatar into caller-owned RGB565 band buffers.

The component owns animation state and timing. It does not initialize a panel,
allocate DMA buffers, create a task or decide when anything changes.

## The four axes

| Axis | Values | Applies to |
| --- | --- | --- |
| `grok_face_set_state` | 14 catalogue animations + `swirl` | always |
| `grok_face_set_expression` | 16 resting faces, or `GROK_FACE_EXPR_NONE` | `idle`, `swirl` |
| `grok_face_set_shape` | 8 resting bodies, or `GROK_FACE_SHAPE_NONE` | `idle`, `wink`, `wide`, `notify`, `swirl` |
| `grok_face_set_color` | 12 body colours | always |

A state that draws its own silhouette keeps it: there the silhouette *is* the
animation. Setting a shape or expression while such a state is showing is not
an error, it simply takes effect when a state that accepts one comes round.
State, shape and expression changes are cross-faded; a colour change is not,
because nothing about a colour moves.

`grok_face_set_look()` aims the gaze at an absolute yaw and pitch, with `mix`
deciding how much it overrides the state's own and `wander` how much idle drift
remains. The two are separate on purpose: a head turned by something that has
since gone away should stay turned *and* stay alive.

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
        .background = 0x0000,
        .acquire_buffer = acquire_buffer,
        .flush_band = flush_band,
        .user_ctx = NULL,
    };

    ESP_ERROR_CHECK(grok_face_init(&config));
    ESP_ERROR_CHECK(grok_face_set_shape(GROK_FACE_SHAPE_GOUTTE));
    ESP_ERROR_CHECK(grok_face_set_expression(GROK_FACE_EXPR_CURIEUX));
    ESP_ERROR_CHECK(grok_face_set_color(GROK_FACE_COLOR_TURQUOISE));
}
```

Call `grok_face_render_frame()` repeatedly from one task.

## Buffer contract

- Each acquired buffer must hold `width * band_rows` RGB565 pixels.
- The buffer must be at least four-byte aligned.
- `height` must be divisible by `band_rows`, and `width` must be even.
- An asynchronous display backend must not return a buffer from
  `acquire_buffer` until its previous flush has completed.
- Set `swap_color_bytes` when the panel expects the high RGB565 byte first
  but pixels are stored as native `uint16_t` values on the ESP32.
- `background` is a native RGB565 value; the component applies the byte swap
  itself. Note that `GROK_FACE_COLOR_ENCRE` (`#0a0a0c`) is meant for a light
  page and is invisible against a black background.

The component is a singleton and is not thread-safe; initialize it once and
call its APIs from the same render task.

## Layout

Three layers, and they do not reach past each other.

| | |
| --- | --- |
| `grok_face.c` | Front door: configuration, the clock, the band loop. No geometry, no animation state. |
| `grok_face_engine.[ch]` | Port of the reference's `engine.ts`. Turns a moment in time into a composed pose, in ball-radius units. Knows nothing about pixels. |
| `grok_face_render.[ch]` | The rasterizer: spans, discs, capsule stamps, eye holes, palette. Knows nothing about time. |
| `bloub_states.[ch]` | The state catalogue: one `pose(t)` per state, plus its timing and its two resting flags. |
| `bloub_expressions.[ch]` | The 16 resting faces. |
| `bloub_skins.[ch]` | The 8 resting bodies and the 12 colours. **Generated.** |
| `bloub_eyefit.[ch]` | Eye-fit offsets per (shape, state, expression). **Generated.** |
| `bloub_shapes/face/decor/math` | Silhouette algebra, eyes-on-a-sphere, arc and particle decor, easings and noise. |

Sampling is a pure function of the time it is handed: every setter is dated and
nothing here reads a clock except `grok_face.c`. Re-sampling an earlier instant
gives the same frame back, which is what makes a transition replayable rather
than merely re-run.

The two generated files are written by `tools/gen_bloub_tables.py` in the parent
project. Do not edit them by hand.
