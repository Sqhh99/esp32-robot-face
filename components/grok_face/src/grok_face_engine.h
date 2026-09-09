#pragma once

#include "bloub_face.h"
#include "bloub_states.h"
#include "grok_face.h"

// The animation engine, ported from bloub's src/bot/engine.ts.
//
// It owns every piece of mutable animation state and knows nothing about
// pixels: `engine_sample` turns a moment in time into a fully composed pose,
// still in ball-radius units. Turning that into spans and stamps is
// grok_face_render's job.
//
// Like the source, sampling is a pure function of the time it is handed --
// every setter is DATED and nothing reads a clock here. Re-sampling an earlier
// instant gives the same frame back, which is what makes a transition
// re-playable rather than merely re-run.

typedef struct {
    /**
     * The composed pose. Its silhouette already carries the resting drift and
     * the breath; a chosen shape and expression have already replaced the
     * body and face where the state accepts them.
     */
    pose_t pose;
    /** Resolved head orientation: the pose's, mixed with any aim, plus drift. */
    head_gaze_t gaze;
    /** 1 = eyes open, 0 = closed. Blink schedule and forced blink already merged. */
    float lid;
    /** Body offset in ball-radius units, for the eyes, dots and pastille. */
    float off_x, off_y;
    /**
     * Eye-fit offset, ball-radius units, added to the EYES only -- it is a
     * translation of the pair, so their spacing, sizes and tilts survive it.
     */
    float eye_dx, eye_dy;
} engine_frame_t;

/** Resets to `idle` with no history, as a fresh engine placed on that state. */
void engine_init(double now);

/** Changes state, cross-fading from the current one over its `morph`. */
void engine_set_state(grok_face_state_t id, double now);

/** Chosen body shape, or -1 for none. Slides to the new value over SHAPE_MORPH. */
void engine_set_shape(int shape, double now);

/** Chosen resting expression, or -1 for none. Slides like the shape does. */
void engine_set_expression(int expr, double now);

/** New aim, or NULL to hand the gaze back to the state's own pose. */
void engine_set_look(const grok_face_look_t *look, double now);

grok_face_state_t engine_get_state(void);
int engine_get_shape(void);
int engine_get_expression(void);

/**
 * The frame at `now`, seconds since an arbitrary origin.
 *
 * One clock in, deliberately: the blink schedule and the drift need it wrapped
 * to keep float precision tight over long uptimes, and the engine does that
 * wrapping itself so a caller cannot hand in two clocks that disagree.
 */
void engine_sample(double now, engine_frame_t *out);
