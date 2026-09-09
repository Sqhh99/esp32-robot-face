#pragma once

#include <stdbool.h>

#include "grok_face.h"
#include "bloub_decor.h"
#include "bloub_face.h"
#include "bloub_shapes.h"

// The 15-state catalogue, ported from bloub's src/bot/states.ts.
//
// Everything here is in ball-radius units and stays unscaled: the engine
// multiplies by the on-screen ball radius once, uniformly, at render time —
// exactly like the source engine's own `R` scale, applied in `sample()`
// rather than in each state's `pose()`.
//
// A state declares whether it carries the RESTING BODY and the RESTING FACE.
// Those two flags are what let the customiser's shape and expression replace
// them; everywhere else the silhouette is the animation and must not be
// overwritten. The engine reads the flags, the catalogue only declares them.

typedef grok_face_state_t bloub_state_id_t;

#define STATE_IDLE GROK_FACE_STATE_IDLE
#define STATE_THINKING GROK_FACE_STATE_THINKING
#define STATE_WINK GROK_FACE_STATE_WINK
#define STATE_WIDE GROK_FACE_STATE_WIDE
#define STATE_ALERT GROK_FACE_STATE_ALERT
#define STATE_NOTIFY GROK_FACE_STATE_NOTIFY
#define STATE_EXCLAIM GROK_FACE_STATE_EXCLAIM
#define STATE_SLEEP GROK_FACE_STATE_SLEEP
#define STATE_EGG GROK_FACE_STATE_EGG
#define STATE_HEXAGON GROK_FACE_STATE_HEXAGON
#define STATE_PLAY GROK_FACE_STATE_PLAY
#define STATE_ORBIT GROK_FACE_STATE_ORBIT
#define STATE_BURST GROK_FACE_STATE_BURST
#define STATE_COMET GROK_FACE_STATE_COMET
#define STATE_SWIRL GROK_FACE_STATE_SWIRL
#define STATE_COUNT GROK_FACE_STATE_COUNT

typedef struct {
    float w, h;   // ball-radius units
    float open;   // 1 = open, 0 = closed
    /**
     * The capsule's own lean, in degrees, positive = its top goes right.
     * Applied AFTER the sphere's tangent frame. Without it both eyes must lean
     * the same way (that is all head roll can do), which puts anger and sadness
     * out of reach: they need MIRRORED leans.
     *
     * Every state measured off the video leaves this at zero; it is the
     * expressions that use it.
     */
    float tilt;
} eye_cfg_t;

// A single state needs 5 dots and 6 arcs at most. A pose being cross-faded
// carries both states' decor at once (bloub's blendPose concatenates them
// with scaled opacities), so the arrays are sized for two.
#define POSE_MAX_DOTS 10
#define POSE_MAX_ARCS 12

typedef struct {
    silhouette_t sil;

    float off_x, off_y; // always 0 for these states; kept for engine-level drift
    head_gaze_t gaze;
    float split; // half eye-gap on the sphere, degrees
    eye_cfg_t eyes[2];
    float eye_alpha; // < 0.5 => eyes not drawn at all (no alpha blending)

    int dot_count;
    dot_render_t dots[POSE_MAX_DOTS];
    bool dots_behind; // true = drawn before the body (burst's particles)

    // The alert state's leaning "!" dot is a teardrop, not a circle — its own
    // primitive (hull2_contains), positioned/rotated like any other dot.
    bool has_teardrop;
    float tear_x, tear_y;  // ball-radius units
    float tear_rot;        // radians
    float tear_opacity;    // so it cross-fades like the other decor

    int arc_count;
    arc_seed_t arcs[POSE_MAX_ARCS];
    float arc_opacity[POSE_MAX_ARCS];
    float arc_t;

    bool has_notif;
    float notif_x, notif_y, notif_r, notif_notch_r; // ball-radius units
} pose_t;

/** Fills `out` with state `id`'s pose at its own local elapsed time `t`. */
void bloub_pose_sample(bloub_state_id_t id, float t, pose_t *out);

/** True if entering this state should be masked by a forced blink, like the reference video. */
bool bloub_state_blink_in(bloub_state_id_t id);

/**
 * True if this state's silhouette is the RESTING body, so a chosen shape may
 * replace it. False for every state that draws its own shape: there the
 * silhouette is the animation.
 */
bool bloub_state_base_body(bloub_state_id_t id);

/**
 * True if this state wears the RESTING face, so a chosen expression may replace
 * it. Only `idle` and `swirl`: the other states with a face have one measured
 * off the video, and reproducing those is the point.
 */
bool bloub_state_base_face(bloub_state_id_t id);

/**
 * Period on which to restart this state's local clock, or 0 to let it run
 * free.
 *
 * In the source these states are blocks of a montage: each is held for its
 * `duration` and then cut away from, so a one-shot animation never has to
 * answer "what happens next". Here a state is held until the next tap, so the
 * one-shot ones — the "!" flying in, the rings, the burst, the comet — would
 * play once and then sit dead. Replaying them on the reference's own
 * `duration` is what makes them loop.
 *
 * Only the one-shot states get a period. The rest are either static poses or
 * already periodic on their own cycle (the thinking dots' 1.5s pulse, the
 * sleeping bounce), and wrapping those would cut across their own rhythm.
 */
float bloub_state_loop_period(bloub_state_id_t id);

/** Cross-fade duration when entering this state, in seconds (bloub's `morph`). */
float bloub_state_morph(bloub_state_id_t id);

/**
 * Interpolates two poses, bloub's `blendPose`: geometry lerps, decor
 * cross-fades on opacity rather than on geometry, and the things that belong
 * to exactly one of the two states (the pastille, whether dots sit behind the
 * body) switch at the halfway point instead of blending.
 */
void bloub_pose_blend(const pose_t *a, const pose_t *b, float t, pose_t *out);

const char *bloub_state_name(bloub_state_id_t id);
