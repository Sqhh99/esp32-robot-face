#include "bloub_expressions.h"

#include "bloub_math.h"

// Amplitudes follow bible-strong-avatar-lab, which exposes the same model
// (head X/Y/Z, per-eye width and height, gap, per-eye angle): there width runs
// from 0.8 to 2.7 times neutral, height from 0.3 to 1.5, and angles up to
// +-80 degrees. These stay inside that envelope.
//
// A tilt is only visible on an elongated eye: at a width/height ratio near 1 the
// capsule is a circle and looks the same at every angle. The reference enforces
// it with a test -- ratio outside [0.6, 1.7] for a tilt of 20 degrees or more,
// outside [0.8, 1.25] below. It already went wrong once.

/** `tilt` in degrees, positive = the top of the capsule goes right. */
#define EYE(w, h, tilt, open) \
    {(w), (h), (open), (tilt)}

/** Both eyes alike, tilts MIRRORED so the tops converge or diverge. */
#define PAIR(w, h, tilt, open) \
    {EYE(w, h, tilt, open), EYE(w, h, -(tilt), open)}

const bloub_expression_t BLOUB_EXPRESSIONS[GROK_EXPR_COUNT] = {
    // the pose measured frame by frame off the reference video
    {{28.49f, 28.62f, -13.0f}, EYE_SPLIT, PAIR(EYE_W, EYE_H, 0.0f, 1.0f)},
    {{4.0f, 5.0f, -4.0f}, 16.0f, PAIR(0.21f, 0.44f, 0.0f, 1.0f)},
    {{3.0f, -3.0f, 0.0f}, 19.0f, PAIR(0.45f, 0.47f, 0.0f, 1.0f)},
    {{6.0f, -14.0f, 0.0f}, 19.5f, PAIR(0.4f, 0.56f, -10.0f, 1.0f)},
    // eyes narrowed into an arc: the tops converge slightly
    {{5.0f, 9.0f, 0.0f}, 17.0f, PAIR(0.27f, 0.17f, 14.0f, 1.0f)},
    {{4.0f, 14.0f, 0.0f}, 18.0f, PAIR(0.34f, 0.13f, 20.0f, 1.0f)},
    // tops of the eyes converging hard toward the centre, and narrowed
    {{3.0f, 7.0f, 0.0f}, 17.0f, PAIR(0.34f, 0.15f, 30.0f, 1.0f)},
    // the reverse: the tops diverge, and the gaze falls
    {{3.0f, -13.0f, 0.0f}, 16.0f, PAIR(0.22f, 0.4f, -28.0f, 1.0f)},
    {{2.0f, -20.0f, 0.0f}, 20.5f, PAIR(0.4f, 0.6f, 0.0f, 1.0f)},
    // one eye distinctly more closed than the other
    {{12.0f, 6.0f, -6.0f}, 16.0f, {EYE(0.21f, 0.4f, 0.0f, 1.0f), EYE(0.22f, 0.15f, 0.0f, 1.0f)}},
    // Asymmetric on both axes: sizes AND tilts mismatched. The narrowed eye is
    // deliberately flat (ratio 1.6): near a ratio of 1 it would be round and its
    // tilt would not show.
    {{-14.0f, 3.0f, 8.0f}, 16.5f, {EYE(0.2f, 0.44f, -18.0f, 1.0f), EYE(0.28f, 0.17f, 14.0f, 1.0f)}},
    // the head leans: it is the roll that carries the curiosity, so the two
    // tilts are equal here rather than mirrored
    {{16.0f, -9.0f, -15.0f}, 16.5f, {EYE(0.24f, 0.46f, -8.0f, 1.0f), EYE(0.2f, 0.38f, -8.0f, 1.0f)}},
    {{5.0f, 17.0f, 0.0f}, 17.0f, PAIR(0.3f, 0.15f, 18.0f, 1.0f)},
    {{-19.0f, -14.0f, -7.0f}, 14.0f, PAIR(0.17f, 0.3f, 0.0f, 1.0f)},
    // horizontal slits and the gaze off to the side
    {{-22.0f, 2.0f, 0.0f}, 16.0f, PAIR(0.3f, 0.12f, 0.0f, 1.0f)},
    // lids half dropped: this goes through `open`, so the on-screen vertical
    // squash -- the very mechanism the blink uses
    {{6.0f, -9.0f, -3.0f}, 16.0f, PAIR(0.2f, 0.42f, 0.0f, 0.42f)},
};

static const char *const EXPRESSION_NAMES[GROK_EXPR_COUNT] = {
    "neutre", "attentif", "surpris", "excite",  "heureux", "hilare", "colere", "triste",
    "effraye", "mefiant", "confus",  "curieux", "fier",    "timide", "blase",  "somnolent",
};

const char *bloub_expression_name(int expr)
{
    if (expr < 0 || expr >= GROK_EXPR_COUNT) {
        return "none";
    }
    return EXPRESSION_NAMES[expr];
}

static void blend_eye(const eye_cfg_t *a, const eye_cfg_t *b, float t, eye_cfg_t *out)
{
    out->w = bloub_lerp(a->w, b->w, t);
    out->h = bloub_lerp(a->h, b->h, t);
    out->open = bloub_lerp(a->open, b->open, t);
    out->tilt = bloub_lerp(a->tilt, b->tilt, t);
}

void bloub_expression_blend(const bloub_expression_t *a, const bloub_expression_t *b, float t,
                            bloub_expression_t *out)
{
    out->gaze.yaw = bloub_lerp(a->gaze.yaw, b->gaze.yaw, t);
    out->gaze.pitch = bloub_lerp(a->gaze.pitch, b->gaze.pitch, t);
    out->gaze.roll = bloub_lerp(a->gaze.roll, b->gaze.roll, t);
    out->split = bloub_lerp(a->split, b->split, t);
    blend_eye(&a->eyes[0], &b->eyes[0], t, &out->eyes[0]);
    blend_eye(&a->eyes[1], &b->eyes[1], t, &out->eyes[1]);
}
