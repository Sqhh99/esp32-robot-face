#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Four independent axes, matching the reference project's own vocabulary:
 *
 *   state       what the avatar is DOING. Each one is an animation with its own
 *               silhouette, timing and decor, measured off the reference video.
 *   expression  the resting FACE. Head orientation, eye gap, eye proportions and
 *               each eye's tilt.
 *   shape       the resting BODY.
 *   color       the body's colour.
 *
 * Shape and expression are not global overrides: a state that draws its own
 * silhouette (the "!", the dots, the egg, the spinning triangle) keeps it,
 * because there the silhouette IS the animation. Only the states listed as
 * resting-body accept a shape (idle, wink, wide, notify, swirl) and only the
 * resting-face ones accept an expression (idle, swirl). Setting either while
 * another state is showing is not an error; it takes effect when a state that
 * accepts it comes round.
 */

/** Animated states, in the reference's own playback order. */
typedef enum {
    GROK_FACE_STATE_IDLE = 0,
    GROK_FACE_STATE_THINKING,
    GROK_FACE_STATE_WINK,
    GROK_FACE_STATE_WIDE,
    GROK_FACE_STATE_ALERT,
    GROK_FACE_STATE_NOTIFY,
    GROK_FACE_STATE_EXCLAIM,
    GROK_FACE_STATE_SLEEP,
    GROK_FACE_STATE_EGG,
    GROK_FACE_STATE_HEXAGON,
    GROK_FACE_STATE_PLAY,
    GROK_FACE_STATE_ORBIT,
    GROK_FACE_STATE_BURST,
    GROK_FACE_STATE_COMET,
    /**
     * Number of states in the catalogue. Everything below this line is reachable
     * only by asking for it: grok_face_next_state() stops here.
     */
    GROK_FACE_STATE_CATALOGUE_COUNT,
    /**
     * Not a catalogue animation but an interface transition, and the only state
     * that was chosen rather than measured. It carries both the resting body and
     * the resting face, so a shape morphs into it instead of jumping.
     */
    GROK_FACE_STATE_SWIRL = GROK_FACE_STATE_CATALOGUE_COUNT,
    GROK_FACE_STATE_COUNT,
} grok_face_state_t;

/** Resting expressions. Only a resting-face state wears one. */
typedef enum {
    /** Keep the state's own face, the one measured off the video. */
    GROK_FACE_EXPR_NONE = -1,
    GROK_FACE_EXPR_NEUTRE = 0,
    GROK_FACE_EXPR_ATTENTIF,
    GROK_FACE_EXPR_SURPRIS,
    GROK_FACE_EXPR_EXCITE,
    GROK_FACE_EXPR_HEUREUX,
    GROK_FACE_EXPR_HILARE,
    GROK_FACE_EXPR_COLERE,
    GROK_FACE_EXPR_TRISTE,
    GROK_FACE_EXPR_EFFRAYE,
    GROK_FACE_EXPR_MEFIANT,
    GROK_FACE_EXPR_CONFUS,
    GROK_FACE_EXPR_CURIEUX,
    GROK_FACE_EXPR_FIER,
    GROK_FACE_EXPR_TIMIDE,
    GROK_FACE_EXPR_BLASE,
    GROK_FACE_EXPR_SOMNOLENT,
    GROK_FACE_EXPR_COUNT,
} grok_face_expression_t;

/** Resting body shapes. Only a resting-body state wears one. */
typedef enum {
    /** Keep the state's own silhouette. */
    GROK_FACE_SHAPE_NONE = -1,
    GROK_FACE_SHAPE_CERCLE = 0,
    GROK_FACE_SHAPE_GALET,
    GROK_FACE_SHAPE_SQUIRCLE,
    GROK_FACE_SHAPE_CAPSULE,
    GROK_FACE_SHAPE_TRIANGLE,
    GROK_FACE_SHAPE_HEXAGONE,
    GROK_FACE_SHAPE_NUAGE,
    GROK_FACE_SHAPE_GOUTTE,
    GROK_FACE_SHAPE_COUNT,
} grok_face_shape_t;

/**
 * Body colours.
 *
 * The reference draws these on a light page, so GROK_FACE_COLOR_ENCRE (#0a0a0c)
 * only reads against a light `background`. On the default black one it is
 * invisible, which is inherent to the palette rather than a defect.
 */
typedef enum {
    GROK_FACE_COLOR_ENCRE = 0,
    GROK_FACE_COLOR_BRUN,
    GROK_FACE_COLOR_ROUGE,
    GROK_FACE_COLOR_ORANGE,
    GROK_FACE_COLOR_AMBRE,
    GROK_FACE_COLOR_VERT,
    GROK_FACE_COLOR_TURQUOISE,
    GROK_FACE_COLOR_BLEU,
    GROK_FACE_COLOR_VIOLET,
    GROK_FACE_COLOR_ROSE,
    GROK_FACE_COLOR_GRIS,
    GROK_FACE_COLOR_CREME,
    GROK_FACE_COLOR_COUNT,
} grok_face_color_t;

/**
 * Where the avatar looks when something outside drives it.
 *
 * `yaw` and `pitch` are ABSOLUTE directions that replace the state's own as
 * `mix` rises, and the component does the mixing because only it knows the pose
 * at that instant. `mix` says how much the outside commands the DIRECTION;
 * `wander` says, separately, how much automatic drift is left, because a head
 * turned with no pointer must stay turned and still look alive. `spin` is a turn
 * in degrees to take on the way, faded out on arrival.
 */
typedef struct {
    float yaw;
    float pitch;
    float mix;
    float spin;
    float wander;
} grok_face_look_t;

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
    /**
     * Page colour behind the avatar, as a native RGB565 value (the component
     * applies swap_color_bytes itself). Zero, the usual choice, is black.
     */
    uint16_t background;
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

/** Selects a state and starts its transition from the current one. */
esp_err_t grok_face_set_state(grok_face_state_t state);

/** Advances through the catalogue, wrapping after COMET. Never selects SWIRL. */
esp_err_t grok_face_next_state(void);

/** Selects the resting expression, or GROK_FACE_EXPR_NONE to keep each state's own. */
esp_err_t grok_face_set_expression(grok_face_expression_t expression);

/** Selects the resting body shape, or GROK_FACE_SHAPE_NONE to keep each state's own. */
esp_err_t grok_face_set_shape(grok_face_shape_t shape);

/** Selects the body colour. */
esp_err_t grok_face_set_color(grok_face_color_t color);

/**
 * Aims the gaze. A target that is not finite is refused and the previous one
 * kept, so a single NaN cannot settle in forever.
 */
esp_err_t grok_face_set_look(const grok_face_look_t *look);

/** Hands the gaze back to the current state's own pose. */
esp_err_t grok_face_clear_look(void);

grok_face_state_t grok_face_get_state(void);
grok_face_expression_t grok_face_get_expression(void);
grok_face_shape_t grok_face_get_shape(void);
grok_face_color_t grok_face_get_color(void);

/** Stable lowercase names, or "unknown" / "none" for a value outside the range. */
const char *grok_face_state_name(grok_face_state_t state);
const char *grok_face_expression_name(grok_face_expression_t expression);
const char *grok_face_shape_name(grok_face_shape_t shape);
const char *grok_face_color_name(grok_face_color_t color);

#ifdef __cplusplus
}
#endif
