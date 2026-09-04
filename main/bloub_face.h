#pragma once

#include <stdbool.h>

// Eyes-on-a-sphere projection and the idle "liveliness" (gaze drift +
// blinking), ported from bloub's src/bot/face.ts.

typedef struct {
    float yaw;   // degrees, positive = looking right
    float pitch; // degrees, positive = looking up
    float roll;  // degrees, head tilt
} head_gaze_t;

typedef struct {
    float x, y;       // screen-space eye centre, before body-radius fit, in px (scale already applied)
    float a, b, c, d; // tangent-frame 2x2 (right axis xy, down axis xy)
    float depth;      // z of the eye's outward normal; > 0 = facing the viewer
} eye_pose_t;

/** Half-gap between the eyes on the sphere, degrees (full separation ~31deg). */
#define EYE_SPLIT 15.46f
/** Resting eye size, ball-radius units. */
#define EYE_W 0.186f
#define EYE_H 0.412f

extern const head_gaze_t BLOUB_REST_GAZE;

/** index 0 = inner eye (side -1), index 1 = outer eye (side +1). */
void eye_poses(head_gaze_t gaze, float scale, float split, eye_pose_t out[2]);

typedef struct {
    float dYaw, dPitch, dRoll;
    float lid; // 1 = open, 0 = fully closed (screen-space vertical squash)
    float driftX, driftY;
    float breath; // ~1, tiny sy modulation so the body is never perfectly still
} liveliness_t;

/**
 * Idle life, a pure function of global (not per-state) time `t`.
 * `wander` scales gaze drift (0 kills it), `blink`/`enable_float` gate the
 * blink schedule and the drift/breath respectively — both are disabled while
 * a state hides the face (eyeAlpha 0), matching the source's `alive` gate.
 */
liveliness_t liveliness_sample(float t, float wander, bool blink, bool enable_float);

/** Blink is a vertical squash in screen space, never fully to zero. */
float blink_scale(float lid);
