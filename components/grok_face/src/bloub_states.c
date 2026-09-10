#include "bloub_states.h"

#include <math.h>

#include "bloub_math.h"

#define DEG2RAD(d) ((d) * (float)M_PI / 180.0f)

static void pose_base(pose_t *out)
{
    sil_circle(&out->sil, 1.0f);
    out->off_x = 0.0f;
    out->off_y = 0.0f;
    out->gaze = BLOUB_REST_GAZE;
    out->split = EYE_SPLIT;
    out->eyes[0] = (eye_cfg_t){.w = EYE_W, .h = EYE_H, .open = 1.0f, .tilt = 0.0f};
    out->eyes[1] = (eye_cfg_t){.w = EYE_W, .h = EYE_H, .open = 1.0f, .tilt = 0.0f};
    out->eye_alpha = 1.0f;
    out->dot_count = 0;
    out->dots_behind = false;
    out->has_teardrop = false;
    out->tear_opacity = 0.0f;
    out->arc_count = 0;
    out->arc_t = 0.0f;
    out->has_notif = false;
}

/** Pulse that sweeps left-to-right across the three "thinking" dots. */
static float dot_pulse(float t, int index)
{
    float p = fmodf((t - (float)index * 0.5f) / 1.5f, 1.0f);
    if (p < 0.0f) p += 1.0f;
    float k = (p < 0.5f) ? (0.5f - 0.5f * cosf(p * BLOUB_TAU)) : 0.0f;
    return bloub_clamp01(k * 2.0f);
}

static void pose_idle(float t, pose_t *out)
{
    (void)t;
    pose_base(out);
}

static void pose_thinking(float t, pose_t *out)
{
    pose_base(out);
    float mid = dot_pulse(t, 1);
    // The ball itself becomes the middle dot: keeps the morph continuous.
    float emerge = 0.3f + 0.7f * bloub_ease_out_cubic(bloub_clamp01(t / 0.3f));
    sil_circle(&out->sil, DOT_R * (1.0f + (DOT_PEAK - 1.0f) * mid));
    out->sil.cx = DOT_X1;
    out->eye_alpha = 0.0f;

    const float xs[3] = {DOT_X0, DOT_X1, DOT_X2};
    const int idxs[2] = {0, 2};
    out->dot_count = 2;
    for (int j = 0; j < 2; j++) {
        int i = idxs[j];
        float k = dot_pulse(t, i);
        out->dots[j] = (dot_render_t){
            .x = xs[i] * emerge,
            .y = 0.0f,
            .r = DOT_R * (1.0f + (DOT_PEAK - 1.0f) * k),
            .opacity = 0.55f + 0.45f * k,
            .has_depth = false,
            .depth = 0.0f,
        };
    }
}

static void pose_wink(float t, pose_t *out)
{
    (void)t;
    pose_base(out);
    out->gaze = (head_gaze_t){-5.37f, 4.55f, 6.7f};
    out->split = 16.25f;
    // The closed eye is a dash WIDER than the open one (0.447 vs 0.236), not
    // the open eye squashed.
    out->eyes[0] =
        (eye_cfg_t){.w = 0.236f, .h = 0.464f, .open = 1.0f, .tilt = 0.0f};
    out->eyes[1] =
        (eye_cfg_t){.w = 0.447f, .h = 0.089f, .open = 1.0f, .tilt = 0.0f};
}

static void pose_wide(float t, pose_t *out)
{
    (void)t;
    pose_base(out);
    out->gaze = (head_gaze_t){6.92f, -21.96f, 11.6f};
    out->split = 18.43f;
    out->eyes[0] = out->eyes[1] =
        (eye_cfg_t){.w = 0.356f, .h = 0.875f, .open = 1.0f, .tilt = 0.0f};
}

static void pose_alert(float t, pose_t *out)
{
    pose_base(out);
    // Measured travel: -0.087 -> +0.732 over 1.5s, ease-in-out.
    float p = bloub_clamp01(t / 1.5f);
    float travel = bloub_ease_in_out_cubic(p) * 0.82f - 0.087f;

    // The return leg is the one place this port departs from the measurement.
    // In the reference the "!" retreats to +0.1 by t=2.0 and then the montage
    // cuts away, so nothing has to happen afterwards. Held on screen and
    // looped, that reads as a stall followed by a 27 px snap back to the
    // start. So the retreat instead eases all the way home, over 0.8s, and
    // the loop period is exactly where it lands: continuous position, and
    // zero velocity at both ends of the join.
    float back = t > 1.6f ? bloub_ease_in_out_cubic(bloub_clamp01((t - 1.6f) / 0.8f)) : 0.0f;
    float x = bloub_lerp(travel, -0.087f, back);
    // Secondary 2.5 Hz buzz, bar and dot in opposite phase.
    float buzz = sinf(t * 2.5f * BLOUB_TAU) * 0.005f;
    float tilt = DEG2RAD(17.7f);

    sil_profile(&out->sil, BLOUB_PROFILE_BAR_ITALIC);
    out->sil.rot = tilt;
    out->sil.cx = x;
    out->sil.cy = -0.325f - buzz;
    out->eye_alpha = 0.0f;

    out->has_teardrop = true;
    out->tear_x = x - sinf(tilt) * 0.58f;
    out->tear_y = -0.325f + cosf(tilt) * 0.58f + buzz * 2.8f;
    out->tear_rot = tilt;
    out->tear_opacity = 1.0f;
}

static void pose_exclaim(float t, pose_t *out)
{
    (void)t;
    pose_base(out);
    // The upright "!" is tapered, not a capsule: the hull of a r=0.132 disc at
    // the top and a r=0.075 one at the bottom, so its profile is taken about
    // (0, BAR_UPRIGHT_CY) rather than the origin.
    sil_profile(&out->sil, BLOUB_PROFILE_BAR_UPRIGHT);
    out->sil.cy = BAR_UPRIGHT_CY;
    out->eye_alpha = 0.0f;

    // Its dot IS a disc here, unlike the leaning "!"'s teardrop.
    out->dot_count = 1;
    out->dots[0] = (dot_render_t){
        .x = -0.012f,
        .y = 0.526f,
        .r = 0.113f,
        .opacity = 1.0f,
        .has_depth = false,
        .depth = 0.0f,
    };
}

static void pose_notify(float t, pose_t *out)
{
    pose_base(out);
    // Pop of the pastille: peaks +14% around 0.3s, then settles.
    float p = bloub_clamp01(t / 0.45f);
    float pop = 1.0f + (NOTIF_POP - 1.0f) * sinf(p * (float)M_PI) * (1.0f - p * 0.35f);
    float r = NOTIF_R * (p < 1.0f ? pop : 1.0f);
    float a = DEG2RAD(NOTIF_ANGLE_DEG);

    out->gaze = (head_gaze_t){-21.94f, -5.82f, -12.2f};
    out->split = 18.89f;
    out->eyes[0] = out->eyes[1] =
        (eye_cfg_t){.w = 0.505f, .h = 0.498f, .open = 1.0f, .tilt = 0.0f};
    out->has_notif = true;
    out->notif_x = cosf(a) * NOTIF_DIST;
    out->notif_y = sinf(a) * NOTIF_DIST;
    out->notif_r = r;
    out->notif_notch_r = r + NOTIF_MARGIN;
}

static void pose_sleep(float t, pose_t *out)
{
    pose_base(out);
    // Measured vertical bounce: +-0.19 around +0.11, period 0.6s.
    sil_circle(&out->sil, 0.1585f);
    out->sil.cy = 0.11f + sinf(t * (BLOUB_TAU / 0.6f)) * 0.19f;
    out->eye_alpha = 0.0f;
}

static void pose_egg(float t, pose_t *out)
{
    (void)t;
    pose_base(out);
    sil_profile(&out->sil, BLOUB_PROFILE_EGG);
    out->gaze = (head_gaze_t){19.97f, 26.01f, -17.1f};
    out->split = 11.07f;
    out->eyes[0] = out->eyes[1] =
        (eye_cfg_t){.w = 0.164f, .h = 0.385f, .open = 1.0f, .tilt = 0.0f};
}

static void pose_hexagon(float t, pose_t *out)
{
    (void)t;
    pose_base(out);
    sil_profile(&out->sil, BLOUB_PROFILE_HEXAGON);
    out->gaze = (head_gaze_t){23.11f, 24.42f, -13.3f};
    out->split = 13.37f;
    out->eyes[0] = out->eyes[1] =
        (eye_cfg_t){.w = 0.177f, .h = 0.411f, .open = 1.0f, .tilt = 0.0f};
}

/**
 * The triangle doesn't spin in place: its centre orbits a small circle
 * (measured radius 0.213), which is what reads as it "toppling" rather than
 * rotating on the spot.
 */
#define TRI_ORBIT 0.213f

static void spinning_triangle(float rot, silhouette_t *out)
{
    sil_profile(out, BLOUB_PROFILE_TRIANGLE);
    out->rot = rot;
    out->cx = -TRI_ORBIT * sinf(rot);
    out->cy = TRI_ORBIT * cosf(rot);
}

static void pose_play(float t, pose_t *out)
{
    pose_base(out);
    float fade = bloub_clamp01(t / 0.35f) * bloub_clamp01((2.2f - t) / 0.5f);
    spinning_triangle(0.0f, &out->sil);
    out->gaze = (head_gaze_t){12.0f, -8.0f, -6.0f};
    out->split = 15.0f;
    out->eyes[0] = out->eyes[1] =
        (eye_cfg_t){.w = 0.18f, .h = 0.34f, .open = 1.0f, .tilt = 0.0f};

    out->arc_count = 4;
    out->arc_t = t;
    for (int i = 0; i < 4; i++) {
        out->arcs[i] = BLOUB_SWOOSH[i];
        out->arcs[i].cx = 0.45f - t * 0.42f; // the bouquet sweeps right-to-left
        out->arc_opacity[i] = fade;
    }
}

static void pose_orbit(float t, pose_t *out)
{
    pose_base(out);
    // Rotation: ramps over 0.35s then holds 1.25 turns/s, counter-clockwise.
    float ramp = bloub_ease_in_out_cubic(bloub_clamp01(t / 0.35f));
    float rot = -BLOUB_TAU * 1.25f * t * ramp;
    // The body relaxes from the triangle back to the ball as the orbit plays.
    float back = bloub_ease_in_out_cubic(bloub_clamp01((t - 1.6f) / 0.9f));

    silhouette_t tri, ball;
    spinning_triangle(rot, &tri);
    sil_circle(&ball, 1.0f);
    ball.rot = rot;

    silhouette_t sil;
    for (int i = 0; i < PROFILE_SAMPLES; i++) {
        sil.radii[i] = tri.radii[i] + (ball.radii[i] - tri.radii[i]) * back;
    }
    sil.rot = rot;
    sil.cx = tri.cx * (1.0f - back);
    sil.cy = tri.cy * (1.0f - back);
    sil.sx = 1.0f;
    sil.sy = 1.0f;
    out->sil = sil;

    float fade = bloub_clamp01(t / 0.8f) * bloub_clamp01((3.6f - t) / 0.9f);
    out->gaze.yaw = BLOUB_REST_GAZE.yaw + sinf(t * 6.5f) * 65.0f * (1.0f - back);
    out->gaze.pitch = -4.0f + back * 32.0f;
    out->gaze.roll = -13.0f;
    out->eyes[0] = out->eyes[1] = (eye_cfg_t){
        .w = 0.18f,
        .h = 0.34f + back * 0.07f,
        .open = 1.0f,
        .tilt = 0.0f,
    };

    out->arc_count = 6;
    out->arc_t = t;
    for (int i = 0; i < 6; i++) {
        out->arcs[i] = BLOUB_RINGS[i];
        // The rings enter one by one over 0.8s.
        out->arc_opacity[i] = fade * bloub_clamp01((t - (float)i * 0.13f) / 0.3f);
    }
}

static void pose_burst(float t, pose_t *out)
{
    pose_base(out);
    // Measured collapse: 1.0 -> 0.166 over 0.7s, ease-out, no bounce.
    float collapse = 1.0f - 0.834f * bloub_ease_out_quint(bloub_clamp01(t / 0.7f));
    float regrow = bloub_ease_out_quint(bloub_clamp01((t - 1.7f) / 0.7f));
    sil_circle(&out->sil, collapse + (1.0f - collapse) * regrow);
    out->eye_alpha = bloub_clamp01((t - 1.85f) / 0.4f);
    out->dot_count = particles_sample(t, 1.0f, out->dots);
    out->dots_behind = true;
}

static void pose_comet(float t, pose_t *out)
{
    pose_base(out);
    float collapse = 1.0f - (1.0f - COMET_DOT) * bloub_ease_out_quint(bloub_clamp01(t / 0.55f));
    float regrow = bloub_ease_out_quint(bloub_clamp01((t - 1.85f) / 0.6f));
    float fade = bloub_clamp01((t - 0.15f) / 0.25f) * bloub_clamp01((1.95f - t) / 0.3f);

    sil_circle(&out->sil, collapse + (1.0f - collapse) * regrow);
    // Measured wobble: drifts down then back up.
    out->sil.cy = sinf(bloub_clamp01(t / 1.7f) * (float)M_PI) * 0.035f;
    out->eye_alpha = bloub_clamp01((t - 2.0f) / 0.35f);

    out->arc_count = 4;
    out->arc_t = t;
    for (int i = 0; i < 4; i++) {
        out->arcs[i] = BLOUB_COMET_RIBBONS[i];
        out->arc_opacity[i] = fade;
    }
}

/**
 * Entry transition, and the ONLY state that was chosen rather than measured.
 *
 * It borrows orbit's vocabulary -- the same rings, with their measured
 * parameters -- but cuts it short: three of the six, and gone inside 1.3s.
 *
 * Both flags matter here. `base_body` lets a chosen shape replace the body, so
 * a pebble or a droplet morphs into this instead of jumping; `base_face` makes
 * it wear the resting face, so an aimed gaze applies from this state on. A
 * state with a gaze pose of its own would hand back mid-course and the eyes
 * would jump on the resume.
 */
static void pose_swirl(float t, pose_t *out)
{
    pose_base(out);
    out->arc_count = 3;
    out->arc_t = t;
    for (int i = 0; i < 3; i++) {
        out->arcs[i] = BLOUB_RINGS[i];
        // They come in one after another, then clear before the block ends, so
        // the return to rest happens on an already-clean frame.
        out->arc_opacity[i] = bloub_clamp01((t - (float)i * 0.06f) / 0.14f) *
                              bloub_clamp01((1.22f - t) / 0.34f);
    }
}

typedef void (*pose_fn_t)(float t, pose_t *out);

static const pose_fn_t POSE_FNS[STATE_COUNT] = {
    [STATE_IDLE] = pose_idle,       [STATE_THINKING] = pose_thinking, [STATE_WINK] = pose_wink,
    [STATE_WIDE] = pose_wide,       [STATE_ALERT] = pose_alert,       [STATE_NOTIFY] = pose_notify,
    [STATE_EXCLAIM] = pose_exclaim, [STATE_SLEEP] = pose_sleep,       [STATE_EGG] = pose_egg,
    [STATE_HEXAGON] = pose_hexagon, [STATE_PLAY] = pose_play,         [STATE_ORBIT] = pose_orbit,
    [STATE_BURST] = pose_burst,     [STATE_COMET] = pose_comet,       [STATE_SWIRL] = pose_swirl,
};

static const bool BLINK_IN[STATE_COUNT] = {
    [STATE_IDLE] = false,   [STATE_THINKING] = true, [STATE_WINK] = true,    [STATE_WIDE] = true,
    [STATE_ALERT] = false,  [STATE_NOTIFY] = true,   [STATE_EXCLAIM] = false,
    [STATE_SLEEP] = false,  [STATE_EGG] = true,      [STATE_HEXAGON] = true, [STATE_PLAY] = true,
    [STATE_ORBIT] = false,  [STATE_BURST] = false,   [STATE_COMET] = false,
    // the shape morph is masked by a blink, as everywhere else
    [STATE_SWIRL] = true,
};

// True where the silhouette is the RESTING body, so a chosen shape may replace
// it. False wherever the state draws its own shape: there the silhouette IS the
// animation and overwriting it would delete the state.
static const bool BASE_BODY[STATE_COUNT] = {
    [STATE_IDLE] = true,    [STATE_WINK] = true,     [STATE_WIDE] = true,
    [STATE_NOTIFY] = true,  [STATE_SWIRL] = true,
};

// True where the state wears the RESTING face. Only these two: every other
// state with a face has one measured off the video, and that is the point.
static const bool BASE_FACE[STATE_COUNT] = {
    [STATE_IDLE] = true,    [STATE_SWIRL] = true,
};

// bloub's per-state `morph`: how long the cross-fade into this state runs.
static const float MORPH[STATE_COUNT] = {
    [STATE_IDLE] = 0.45f,   [STATE_THINKING] = 0.4f, [STATE_WINK] = 0.3f,  [STATE_WIDE] = 0.55f,
    [STATE_ALERT] = 0.45f,  [STATE_NOTIFY] = 0.5f,   [STATE_EXCLAIM] = 0.45f,
    [STATE_SLEEP] = 0.5f,   [STATE_EGG] = 0.4f,      [STATE_HEXAGON] = 0.4f,
    [STATE_PLAY] = 0.5f,    [STATE_ORBIT] = 0.6f,    [STATE_BURST] = 0.4f, [STATE_COMET] = 0.45f,
    [STATE_SWIRL] = 0.3f,
};

// The reference's own `duration` per state, used only where a state's
// animation is one-shot and would otherwise freeze. Several land on exact
// multiples of their state's internal rhythm, so the restart is seamless:
// alert's 2.5 Hz buzz is 6 whole cycles in 2.4s, and notify's pastille is
// back at its resting radius well before 2.2s.
// Chosen so the wrap lands where the animation began, not merely at the
// reference's cut point:
//   alert  2.4 - the retreat above eases exactly home by then
//   play   2.2 - the swoosh's fade envelope reaches 0 (at 2.0 it was still
//                at 40% and jumped position in full view)
//   orbit  3.6 - likewise for the rings' fade
//   notify 2.2 - the pastille is back at its resting radius well before this
//   burst  2.6 / comet 2.4 - the body has finished regrowing to a full ball
static const float LOOP_PERIOD[STATE_COUNT] = {
    [STATE_IDLE] = 0.0f,     [STATE_THINKING] = 0.0f, [STATE_WINK] = 0.0f,
    [STATE_WIDE] = 0.0f,     [STATE_ALERT] = 2.4f,    [STATE_NOTIFY] = 2.2f,
    [STATE_EXCLAIM] = 0.0f,  [STATE_SLEEP] = 0.0f,    [STATE_EGG] = 0.0f,
    [STATE_HEXAGON] = 0.0f,  [STATE_PLAY] = 2.2f,     [STATE_ORBIT] = 3.6f,
    [STATE_BURST] = 2.6f,    [STATE_COMET] = 2.4f,
    // swirl is a transition, not something to hold: its rings play once and
    // clear, leaving the resting face. Replaying them would make it a pulse.
    [STATE_SWIRL] = 0.0f,
};

static const char *STATE_NAMES[STATE_COUNT] = {
    [STATE_IDLE] = "idle",       [STATE_THINKING] = "thinking", [STATE_WINK] = "wink",
    [STATE_WIDE] = "wide",       [STATE_ALERT] = "alert",       [STATE_NOTIFY] = "notify",
    [STATE_EXCLAIM] = "exclaim", [STATE_SLEEP] = "sleep",       [STATE_EGG] = "egg",
    [STATE_HEXAGON] = "hexagon", [STATE_PLAY] = "play",         [STATE_ORBIT] = "orbit",
    [STATE_BURST] = "burst",     [STATE_COMET] = "comet",       [STATE_SWIRL] = "swirl",
};

void bloub_pose_sample(bloub_state_id_t id, float t, pose_t *out)
{
    POSE_FNS[id](t, out);
}

bool bloub_state_blink_in(bloub_state_id_t id)
{
    return BLINK_IN[id];
}

bool bloub_state_base_body(bloub_state_id_t id)
{
    return BASE_BODY[id];
}

bool bloub_state_base_face(bloub_state_id_t id)
{
    return BASE_FACE[id];
}

float bloub_state_loop_period(bloub_state_id_t id)
{
    return LOOP_PERIOD[id];
}

float bloub_state_morph(bloub_state_id_t id)
{
    return MORPH[id];
}

static eye_cfg_t lerp_eye(const eye_cfg_t *a, const eye_cfg_t *b, float t)
{
    return (eye_cfg_t){
        .w = bloub_lerp(a->w, b->w, t),
        .h = bloub_lerp(a->h, b->h, t),
        .open = bloub_lerp(a->open, b->open, t),
        .tilt = bloub_lerp(a->tilt, b->tilt, t),
    };
}

void bloub_pose_blend(const pose_t *a, const pose_t *b, float t, pose_t *out)
{
    float out_t = 1.0f - t;

    // Every silhouette is sampled at the same 64 angles, so a shape morph is
    // just a lerp of radii — no path-morphing machinery needed. This is the
    // whole reason bloub represents bodies as radial profiles.
    for (int i = 0; i < PROFILE_SAMPLES; i++) {
        out->sil.radii[i] = bloub_lerp(a->sil.radii[i], b->sil.radii[i], t);
    }
    // Rotate the short way round, so +170deg -> -170deg doesn't spin a whole turn.
    float d_rot = b->sil.rot - a->sil.rot;
    while (d_rot > (float)M_PI) d_rot -= BLOUB_TAU;
    while (d_rot < -(float)M_PI) d_rot += BLOUB_TAU;
    out->sil.rot = a->sil.rot + d_rot * t;
    out->sil.cx = bloub_lerp(a->sil.cx, b->sil.cx, t);
    out->sil.cy = bloub_lerp(a->sil.cy, b->sil.cy, t);
    out->sil.sx = bloub_lerp(a->sil.sx, b->sil.sx, t);
    out->sil.sy = bloub_lerp(a->sil.sy, b->sil.sy, t);

    out->off_x = bloub_lerp(a->off_x, b->off_x, t);
    out->off_y = bloub_lerp(a->off_y, b->off_y, t);
    out->gaze.yaw = bloub_lerp(a->gaze.yaw, b->gaze.yaw, t);
    out->gaze.pitch = bloub_lerp(a->gaze.pitch, b->gaze.pitch, t);
    out->gaze.roll = bloub_lerp(a->gaze.roll, b->gaze.roll, t);
    out->split = bloub_lerp(a->split, b->split, t);
    out->eyes[0] = lerp_eye(&a->eyes[0], &b->eyes[0], t);
    out->eyes[1] = lerp_eye(&a->eyes[1], &b->eyes[1], t);
    out->eye_alpha = bloub_lerp(a->eye_alpha, b->eye_alpha, t);

    // Decor cross-fades on opacity: both states' dots and arcs are carried at
    // once, rather than trying to interpolate between different counts.
    out->dot_count = 0;
    for (int i = 0; i < a->dot_count && out->dot_count < POSE_MAX_DOTS; i++) {
        dot_render_t d = a->dots[i];
        d.opacity *= out_t;
        out->dots[out->dot_count++] = d;
    }
    for (int i = 0; i < b->dot_count && out->dot_count < POSE_MAX_DOTS; i++) {
        dot_render_t d = b->dots[i];
        d.opacity *= t;
        out->dots[out->dot_count++] = d;
    }

    out->arc_count = 0;
    for (int i = 0; i < a->arc_count && out->arc_count < POSE_MAX_ARCS; i++) {
        out->arcs[out->arc_count] = a->arcs[i];
        out->arc_opacity[out->arc_count] = a->arc_opacity[i] * out_t;
        out->arc_count++;
    }
    for (int i = 0; i < b->arc_count && out->arc_count < POSE_MAX_ARCS; i++) {
        out->arcs[out->arc_count] = b->arcs[i];
        out->arc_opacity[out->arc_count] = b->arc_opacity[i] * t;
        out->arc_count++;
    }
    // Both states' arcs must be evaluated at their own local time, but a pose
    // carries one clock. During a fade the outgoing arcs are already fading
    // out, so following the incoming state's clock is the one that shows.
    out->arc_t = t < 0.5f ? a->arc_t : b->arc_t;

    // The teardrop belongs to one state at a time; fade it rather than
    // switching it, so the leaning "!" dissolves instead of vanishing.
    if (a->has_teardrop && (!b->has_teardrop || t < 0.5f)) {
        out->has_teardrop = true;
        out->tear_x = a->tear_x;
        out->tear_y = a->tear_y;
        out->tear_rot = a->tear_rot;
        out->tear_opacity = a->tear_opacity * out_t;
    } else if (b->has_teardrop) {
        out->has_teardrop = true;
        out->tear_x = b->tear_x;
        out->tear_y = b->tear_y;
        out->tear_rot = b->tear_rot;
        out->tear_opacity = b->tear_opacity * t;
    } else {
        out->has_teardrop = false;
        out->tear_opacity = 0.0f;
    }

    // These belong to exactly one of the two states, so they switch rather
    // than blend, at the halfway point.
    const pose_t *pick = t < 0.5f ? a : b;
    out->has_notif = pick->has_notif;
    out->notif_x = pick->notif_x;
    out->notif_y = pick->notif_y;
    out->notif_r = pick->notif_r;
    out->notif_notch_r = pick->notif_notch_r;
    out->dots_behind = pick->dots_behind;
}

const char *bloub_state_name(bloub_state_id_t id)
{
    return STATE_NAMES[id];
}
