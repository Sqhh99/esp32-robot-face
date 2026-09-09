#include "grok_face.h"

#include <math.h>

#include "bloub_expressions.h"
#include "bloub_skins.h"
#include "bloub_states.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "grok_face_engine.h"
#include "grok_face_render.h"

// The component's front door: configuration, the clock, and the band loop.
//
// It owns no geometry and no animation state. grok_face_engine turns a moment
// in time into a composed pose; grok_face_render turns that into pixels; this
// file only decides when each happens and hands the bands to the display.

static const char *TAG = "grok_face";

static grok_face_config_t s_config;
static bool s_initialized;

/*
 * State, shape and expression are NOT mirrored here: the engine holds them and
 * the getters ask it. Keeping a second copy in the facade is how the two drift
 * apart. The colour is the exception -- it is the renderer's, not the engine's,
 * because nothing about it is animated.
 */
/*
 * The reference's default is `encre` (#0a0a0c), which is meant for its light
 * page and would be invisible on the black background this component defaults
 * to. `creme` is the palette's light end, so the avatar shows up out of the box
 * and a caller that sets its own background can pick `encre` deliberately.
 */
static grok_face_color_t s_color = GROK_FACE_COLOR_CREME;

// Too big for the render task's stack.
static engine_frame_t s_engine_frame;
static render_frame_t s_render_frame;

// Bands touched by the previous frame. A frame only repaints the bands its
// own content reaches, so when the content shrinks (the sleeping dot bouncing
// away, the burst collapsing) the bands it has just left have to be blanked
// once, or they keep the previous frame's pixels.
static int s_prev_band_first;
static int s_prev_band_last;

#define BAND_COUNT (s_config.height / s_config.band_rows)

static double now_seconds(void)
{
    return (double)esp_timer_get_time() * 1e-6;
}

esp_err_t grok_face_init(const grok_face_config_t *config)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (config == NULL || config->width <= 0 || config->height <= 0 ||
        config->band_rows <= 0 || (config->width & 1) != 0 ||
        config->height % config->band_rows != 0 || config->radius <= 0.0f ||
        !isfinite(config->center_x) || !isfinite(config->center_y) ||
        !isfinite(config->radius) || config->acquire_buffer == NULL ||
        config->flush_band == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;

    const render_config_t render_config = {
        .width = config->width,
        .height = config->height,
        .band_rows = config->band_rows,
        .center_x = config->center_x,
        .center_y = config->center_y,
        .radius = config->radius,
        .swap_color_bytes = config->swap_color_bytes,
        .background = config->background,
    };
    render_init(&render_config);
    render_set_body_color(BLOUB_COLORS[s_color].rgb565);

    engine_init(now_seconds());
    s_prev_band_first = 0;
    s_prev_band_last = BAND_COUNT - 1;

    for (int band = 0; band < BAND_COUNT; band++) {
        uint16_t *buf = s_config.acquire_buffer(s_config.user_ctx);
        if (buf == NULL) {
            return ESP_ERR_NO_MEM;
        }
        render_clear_band(buf);
        esp_err_t err = s_config.flush_band(s_config.user_ctx, band * s_config.band_rows,
                                            s_config.band_rows, buf);
        if (err != ESP_OK) {
            return err;
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "ready: %dx%d, band_rows=%d, state=%s, color=%s",
             s_config.width, s_config.height, s_config.band_rows,
             bloub_state_name(engine_get_state()), BLOUB_COLOR_NAMES[s_color]);
    return ESP_OK;
}

// --- the four axes --------------------------------------------------------

esp_err_t grok_face_set_state(grok_face_state_t state)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (state < GROK_FACE_STATE_IDLE || state >= GROK_FACE_STATE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    engine_set_state(state, now_seconds());
    ESP_LOGI(TAG, "state -> %s", bloub_state_name(state));
    return ESP_OK;
}

esp_err_t grok_face_next_state(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    // Walks the catalogue only. `swirl` sits past its end and is reachable
    // solely by asking for it: it is an interface transition, not an animation.
    grok_face_state_t cur = engine_get_state();
    grok_face_state_t next =
        (cur >= GROK_FACE_STATE_CATALOGUE_COUNT)
            ? GROK_FACE_STATE_IDLE
            : (grok_face_state_t)((cur + 1) % GROK_FACE_STATE_CATALOGUE_COUNT);
    return grok_face_set_state(next);
}

esp_err_t grok_face_set_expression(grok_face_expression_t expression)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (expression < GROK_FACE_EXPR_NONE || expression >= GROK_FACE_EXPR_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    engine_set_expression((int)expression, now_seconds());
    ESP_LOGI(TAG, "expression -> %s", bloub_expression_name((int)expression));
    return ESP_OK;
}

esp_err_t grok_face_set_shape(grok_face_shape_t shape)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (shape < GROK_FACE_SHAPE_NONE || shape >= GROK_FACE_SHAPE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    engine_set_shape((int)shape, now_seconds());
    ESP_LOGI(TAG, "shape -> %s", grok_face_shape_name(shape));
    return ESP_OK;
}

esp_err_t grok_face_set_color(grok_face_color_t color)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (color < GROK_FACE_COLOR_ENCRE || color >= GROK_FACE_COLOR_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    s_color = color;
    // A colour change is not animated: the reference cross-fades shapes and
    // expressions because they move, and a colour does not.
    render_set_body_color(BLOUB_COLORS[color].rgb565);
    ESP_LOGI(TAG, "color -> %s", BLOUB_COLOR_NAMES[color]);
    return ESP_OK;
}

esp_err_t grok_face_set_look(const grok_face_look_t *look)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (look == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    engine_set_look(look, now_seconds());
    return ESP_OK;
}

esp_err_t grok_face_clear_look(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    engine_set_look(NULL, now_seconds());
    return ESP_OK;
}

grok_face_state_t grok_face_get_state(void)
{
    return engine_get_state();
}

grok_face_expression_t grok_face_get_expression(void)
{
    return (grok_face_expression_t)engine_get_expression();
}

grok_face_shape_t grok_face_get_shape(void)
{
    return (grok_face_shape_t)engine_get_shape();
}

grok_face_color_t grok_face_get_color(void)
{
    return s_color;
}

const char *grok_face_state_name(grok_face_state_t state)
{
    if (state < GROK_FACE_STATE_IDLE || state >= GROK_FACE_STATE_COUNT) {
        return "unknown";
    }
    return bloub_state_name(state);
}

const char *grok_face_expression_name(grok_face_expression_t expression)
{
    return bloub_expression_name((int)expression);
}

const char *grok_face_shape_name(grok_face_shape_t shape)
{
    if (shape < GROK_FACE_SHAPE_CERCLE || shape >= GROK_FACE_SHAPE_COUNT) {
        return "none";
    }
    return BLOUB_SHAPE_NAMES[shape];
}

const char *grok_face_color_name(grok_face_color_t color)
{
    if (color < GROK_FACE_COLOR_ENCRE || color >= GROK_FACE_COLOR_COUNT) {
        return "unknown";
    }
    return BLOUB_COLOR_NAMES[color];
}

// --- one frame ------------------------------------------------------------

esp_err_t grok_face_render_frame(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    double now_s = now_seconds();
    engine_sample(now_s, &s_engine_frame);
    render_prepare(&s_engine_frame, &s_render_frame);

    int band_first = s_render_frame.row_lo / s_config.band_rows;
    int band_last = s_render_frame.row_hi / s_config.band_rows;

    // Repaint the union with the previous frame's range, so bands the content
    // has just vacated get blanked exactly once instead of keeping stale pixels.
    int paint_first = band_first < s_prev_band_first ? band_first : s_prev_band_first;
    int paint_last = band_last > s_prev_band_last ? band_last : s_prev_band_last;
    s_prev_band_first = band_first;
    s_prev_band_last = band_last;

    for (int band = paint_first; band <= paint_last; band++) {
        int y0 = band * s_config.band_rows;
        uint16_t *buf = s_config.acquire_buffer(s_config.user_ctx);
        if (buf == NULL) {
            return ESP_ERR_NO_MEM;
        }
        render_clear_band(buf);
        render_band(buf, y0, &s_render_frame);

        esp_err_t err =
            s_config.flush_band(s_config.user_ctx, y0, s_config.band_rows, buf);
        if (err != ESP_OK) {
            return err;
        }
    }

    // Frame rate, so the cost of any future change here is measurable rather
    // than guessed at.
    static uint32_t s_frames = 0;
    static double s_stats_at = 0.0;
    s_frames++;
    if (now_s - s_stats_at >= 2.0) {
        ESP_LOGI(TAG, "%.1f fps | %s | %s | %s | %s | bands %d..%d | stamps %d",
                 (double)s_frames / (now_s - s_stats_at), bloub_state_name(engine_get_state()),
                 grok_face_shape_name(grok_face_get_shape()),
                 bloub_expression_name(engine_get_expression()), BLOUB_COLOR_NAMES[s_color],
                 paint_first, paint_last, s_render_frame.stamp_count);
        s_frames = 0;
        s_stats_at = now_s;
    }
    return ESP_OK;
}
