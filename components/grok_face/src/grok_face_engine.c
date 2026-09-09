#include "grok_face_engine.h"

#include <math.h>
#include <string.h>

#include "bloub_expressions.h"
#include "bloub_eyefit.h"
#include "bloub_math.h"
#include "bloub_skins.h"

/**
 * How long a change of body shape, or of resting expression, takes to slide.
 *
 * Both use the same duration deliberately: they are the customiser's two axes
 * and a change on either should read as the same gesture.
 */
#define SHAPE_MORPH 0.45f

/**
 * How long the gaze takes to catch up with a new aim. Shorter than
 * SHAPE_MORPH: a gaze that follows should look attentive, not viscous. Since
 * the target is re-posted on every movement, this duration is what gives the
 * tracking its inertia -- the gaze never quite reaches a moving target.
 */
#define LOOK_MORPH 0.24f

static const grok_face_look_t NO_LOOK = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

// --- mutable animation state ---------------------------------------------

static bloub_state_id_t s_cur = STATE_IDLE;
static bloub_state_id_t s_prev = STATE_IDLE;
static bool s_has_prev;
static double s_t_cur;
static double s_t_prev;
static double s_blink_at = -10.0;

/**
 * Frozen departure pose, set only when a state change arrives while a fade is
 * already running. See engine_set_state.
 */
static pose_t s_frozen;
static bool s_has_frozen;

static int s_shape = -1;
static int s_shape_prev = -1;
static double s_shape_at = -10.0;

static int s_expr = -1;
static int s_expr_prev = -1;
static double s_expr_at = -10.0;

static grok_face_look_t s_look;
static grok_face_look_t s_look_prev;
static double s_look_at = -10.0;

// Scratch space for one sample. Static rather than automatic: a pose is over a
// kilobyte and the render task's stack is not the place for three of them.
static pose_t s_pose_cur;
static pose_t s_pose_prev;
static pose_t s_pose_blend;
static float s_shape_scratch[PROFILE_SAMPLES];
static bloub_expression_t s_expr_scratch;

// --- the two customiser axes ---------------------------------------------

/**
 * The shape in force at `now`, morph included, or NULL for none.
 *
 * Never clears `s_shape_prev` when the morph ends: sampling has to stay a pure
 * function of time, so re-reading an earlier date must still produce the
 * intermediate frame. Keeping one extra index is the whole cost.
 */
static const float *shape_at_time(double now)
{
    if (s_shape < 0) return NULL;
    const float *to = BLOUB_SHAPE_RADII[s_shape];
    if (s_shape_prev < 0) return to;
    float k = (float)((now - s_shape_at) / SHAPE_MORPH);
    if (k >= 1.0f) return to;
    const float *from = BLOUB_SHAPE_RADII[s_shape_prev];
    float t = bloub_ease_out_quint(bloub_clamp01(k));
    for (int i = 0; i < PROFILE_SAMPLES; i++) {
        s_shape_scratch[i] = bloub_lerp(from[i], to[i], t);
    }
    return s_shape_scratch;
}

/** The expression in force at `now`, morph included, or NULL for none. */
static const bloub_expression_t *expr_at_time(double now)
{
    if (s_expr < 0) return NULL;
    const bloub_expression_t *to = &BLOUB_EXPRESSIONS[s_expr];
    if (s_expr_prev < 0) return to;
    float k = (float)((now - s_expr_at) / SHAPE_MORPH);
    if (k >= 1.0f) return to;
    bloub_expression_blend(&BLOUB_EXPRESSIONS[s_expr_prev], to,
                           bloub_ease_out_quint(bloub_clamp01(k)), &s_expr_scratch);
    return &s_expr_scratch;
}

/** Samples one state at its own clock, wrapped if it is a one-shot animation. */
static void sample_state(bloub_state_id_t id, float elapsed, pose_t *out)
{
    float loop = bloub_state_loop_period(id);
    if (loop > 0.0f) elapsed = fmodf(elapsed, loop);
    bloub_pose_sample(id, elapsed, out);
}

/**
 * A state's pose with the customiser's choices applied where it accepts them.
 *
 * The shape swaps only the radial profile: the state's own rotation, offset and
 * squash are kept, so a shape dropped onto `wink` still leans the way `wink`
 * leans.
 */
static void posed(bloub_state_id_t id, float elapsed, const float *shape,
                  const bloub_expression_t *expr, pose_t *out)
{
    sample_state(id, elapsed, out);
    if (shape != NULL && bloub_state_base_body(id)) {
        memcpy(out->sil.radii, shape, sizeof(out->sil.radii));
    }
    if (expr != NULL && bloub_state_base_face(id)) {
        out->gaze = expr->gaze;
        out->split = expr->split;
        out->eyes[0] = expr->eyes[0];
        out->eyes[1] = expr->eyes[1];
    }
}

// --- the eye-fit offset ---------------------------------------------------

/**
 * One morph axis: read the table on its two BOUNDS and interpolate with its own
 * curve.
 *
 * Never on the interpolated value. During a shape morph the profile in force is
 * a freshly blended array that exists in no table, and a blended expression has
 * no identity either. Handing those to a solver is exactly what made every
 * per-frame version of this correction tremble.
 */
static bloub_vec2_t on_axis(double start, double now, bloub_vec2_t a, bloub_vec2_t b)
{
    if (a.x == b.x && a.y == b.y) return b;
    float k = (float)((now - start) / SHAPE_MORPH);
    if (k >= 1.0f) return b;
    float t = bloub_ease_out_quint(bloub_clamp01(k));
    bloub_vec2_t v = {bloub_lerp(a.x, b.x, t), bloub_lerp(a.y, b.y, t)};
    return v;
}

/** The expression axis, for one of the two shapes in play. */
static bloub_vec2_t fit_for_shape(int shape, bloub_state_id_t state, double now)
{
    return on_axis(s_expr_at, now, bloub_eyefit(shape, state, s_expr_prev),
                   bloub_eyefit(shape, state, s_expr));
}

/** Then the shape axis. Both use the silhouette morph's own curve and duration. */
static bloub_vec2_t eyefit_at_time(double now, bloub_state_id_t state)
{
    return on_axis(s_shape_at, now, fit_for_shape(s_shape_prev, state, now),
                   fit_for_shape(s_shape, state, now));
}

// --- aim ------------------------------------------------------------------

static grok_face_look_t look_at_time(double now)
{
    float k = (float)((now - s_look_at) / LOOK_MORPH);
    if (k >= 1.0f) return s_look;
    float t = bloub_ease_out_quint(bloub_clamp01(k));
    grok_face_look_t l = {
        .yaw = bloub_lerp(s_look_prev.yaw, s_look.yaw, t),
        .pitch = bloub_lerp(s_look_prev.pitch, s_look.pitch, t),
        .mix = bloub_lerp(s_look_prev.mix, s_look.mix, t),
        .spin = bloub_lerp(s_look_prev.spin, s_look.spin, t),
        .wander = bloub_lerp(s_look_prev.wander, s_look.wander, t),
    };
    return l;
}

void engine_set_look(const grok_face_look_t *look, double now)
{
    /*
     * A target that is not finite is refused, and the engine KEEPS the last
     * one: a single NaN posted once would propagate to every frame and the
     * avatar would never come to rest again. The engine does not get to depend
     * on its callers being careful.
     */
    if (look != NULL &&
        !isfinite(look->yaw + look->pitch + look->mix + look->spin + look->wander)) {
        return;
    }
    /*
     * Restarts from the CURRENT value, not from the previous target the way a
     * shape change does: this is called on every pointer movement, and
     * restarting from the old target would step the gaze backwards before each
     * catch-up -- the tracking would judder instead of glide.
     */
    s_look_prev = look_at_time(now);
    s_look = look != NULL ? *look : NO_LOOK;
    s_look_at = now;
}

// --- state, shape and expression setters ---------------------------------

void engine_init(double now)
{
    s_cur = STATE_IDLE;
    s_prev = STATE_IDLE;
    s_has_prev = false;
    s_has_frozen = false;
    s_t_cur = now;
    s_t_prev = now;
    s_blink_at = -10.0;
    s_shape = s_shape_prev = -1;
    s_expr = s_expr_prev = -1;
    s_shape_at = s_expr_at = -10.0;
    s_look = NO_LOOK;
    s_look_prev = NO_LOOK;
    s_look_at = -10.0;
}

/** The composed pose at `now`, fade included. Extracted so a change can freeze it. */
static void compose(double now, pose_t *out);

void engine_set_state(grok_face_state_t id, double now)
{
    if (id == s_cur) return;
    /*
     * The engine keeps one slot of history, so a change arriving DURING a fade
     * used to replace the blend's origin with the full pose of the state being
     * left, rather than the partly blended frame that was actually on screen.
     * Measured on idle -> wide -> idle at 100 ms: a 35.9 px jump against 8.0 px
     * of normal movement. So we freeze the composed pose and blend from it.
     *
     * And only in that case. Freezing on every change would stop the outgoing
     * state's own animation dead for the whole fade -- alert's travelling "!"
     * would halt mid-course -- and there is nothing to fix outside a fade,
     * where the state being left already IS the displayed frame.
     */
    float morph = bloub_state_morph(s_cur);
    bool mid_fade = s_has_prev && (now - s_t_cur) < morph;
    if (mid_fade) {
        compose(now, &s_frozen);
        s_has_frozen = true;
    } else {
        s_has_frozen = false;
    }
    s_prev = s_cur;
    s_t_prev = s_t_cur;
    s_cur = id;
    s_t_cur = now;
    // In the reference video every change of shape is masked by a blink.
    if (bloub_state_blink_in(id)) {
        s_blink_at = now;
    }
}

void engine_set_shape(int shape, double now)
{
    if (shape == s_shape) return;
    s_shape_prev = s_shape;
    s_shape = shape;
    s_shape_at = now;
}

void engine_set_expression(int expr, double now)
{
    if (expr == s_expr) return;
    s_expr_prev = s_expr;
    s_expr = expr;
    s_expr_at = now;
}

grok_face_state_t engine_get_state(void)
{
    return s_cur;
}

int engine_get_shape(void)
{
    return s_shape;
}

int engine_get_expression(void)
{
    return s_expr;
}

// --- sampling -------------------------------------------------------------

/**
 * Origin of the running fade: the frozen pose if there is one, otherwise the
 * state being left evaluated at its OWN elapsed time -- so still animating,
 * which is the point.
 */
static const pose_t *fade_origin(double now, const float *shape,
                                 const bloub_expression_t *expr)
{
    if (s_has_frozen) return &s_frozen;
    if (!s_has_prev) return NULL;
    posed(s_prev, (float)(now - s_t_prev), shape, expr, &s_pose_prev);
    return &s_pose_prev;
}

static void compose(double now, pose_t *out)
{
    const float *shape = shape_at_time(now);
    const bloub_expression_t *expr = expr_at_time(now);
    float since = (float)(now - s_t_cur);
    posed(s_cur, since < 0.0f ? 0.0f : since, shape, expr, &s_pose_cur);

    float morph = bloub_state_morph(s_cur);
    if (since >= morph) {
        *out = s_pose_cur;
        return;
    }
    const pose_t *origin = fade_origin(now, shape, expr);
    if (origin == NULL) {
        *out = s_pose_cur;
        return;
    }
    // Exponential ease-out, the curve measured off the reference video. The
    // ratio is clamped: re-reading a date BEFORE the state change would give a
    // negative one, which the ease-out extrapolates -- the silhouette then
    // shoots thirty times too far.
    //
    // Blended into scratch and copied out, never straight into `out`: chaining
    // two state changes calls this with `out` pointing at the frozen pose,
    // which is also the origin it is reading.
    bloub_pose_blend(origin, &s_pose_cur, bloub_ease_out_quint(bloub_clamp01(since / morph)),
                     &s_pose_blend);
    *out = s_pose_blend;
}

/**
 * Wrap for the clock the periodic functions run on, so float precision stays
 * tight after many hours of uptime. Every period here is far shorter than this.
 */
#define GLOBAL_WRAP 100000.0

void engine_sample(double now, engine_frame_t *out)
{
    float global_t = (float)fmod(now, GLOBAL_WRAP);

    const float *shape = shape_at_time(now);
    const bloub_expression_t *expr = expr_at_time(now);

    float since = (float)(now - s_t_cur);
    posed(s_cur, since < 0.0f ? 0.0f : since, shape, expr, &s_pose_cur);
    bloub_vec2_t fit = eyefit_at_time(now, s_cur);

    // --- transition -------------------------------------------------------
    // The previous state is never purged: `since < morph` is enough to ignore
    // it once the fade is over, and forgetting it would make the engine
    // non-replayable. That is the optimisation that looks innocent and breaks
    // everything.
    pose_t *p = &s_pose_cur;
    float morph = bloub_state_morph(s_cur);
    if (since < morph) {
        const pose_t *origin = fade_origin(now, shape, expr);
        if (origin != NULL) {
            float ratio = bloub_ease_out_quint(bloub_clamp01(since / morph));
            bloub_pose_blend(origin, &s_pose_cur, ratio, &s_pose_blend);
            p = &s_pose_blend;
            // The eye-fit offset follows the SAME curve as the silhouette that
            // motivates it, from the state being left.
            if (s_has_prev) {
                bloub_vec2_t before = eyefit_at_time(now, s_prev);
                fit.x = bloub_lerp(before.x, fit.x, ratio);
                fit.y = bloub_lerp(before.y, fit.y, ratio);
            }
        }
    }

    // --- resting life -----------------------------------------------------
    // Blink and gaze wander are gated on the face being substantially there,
    // but the holes themselves fade with eye_alpha so a face dissolving into a
    // "!" or a sleeping dot does not pop out of existence.
    bool alive = p->eye_alpha > 0.5f;
    grok_face_look_t look = look_at_time(now);
    // `float` (breath/drift) is always on in the source; only blink and gaze
    // wander are gated by whether the face is actually showing.
    liveliness_t life =
        liveliness_sample(global_t, alive ? look.wander : 0.0f, alive, true);

    /*
     * Both aims REPLACE the pose's rather than adding to them, and the spin is
     * subtracted along the way. The drift is added AFTER the mix, otherwise the
     * target would cancel it at the same time as the pose -- and it has to
     * survive a head turned with no pointer.
     *
     * The roll follows nothing: the avatar's head leans -13 degrees in the
     * video, and rolling it with the pointer breaks that signature.
     */
    out->gaze.yaw = bloub_lerp(p->gaze.yaw, look.yaw, look.mix) + life.dYaw - look.spin;
    out->gaze.pitch = bloub_lerp(p->gaze.pitch, look.pitch, look.mix) + life.dPitch;
    out->gaze.roll = p->gaze.roll + life.dRoll;

    // Blink forced by the state change, on top of the schedule. Measured on
    // the UNWRAPPED clock: on the wrapped one the difference goes hugely
    // negative every time it turns over, and forced blinks stop for good.
    float forced = bloub_clamp01((float)(now - s_blink_at) / 0.2f);
    float forced_lid = forced < 1.0f ? fabsf(forced * 2.0f - 1.0f) : 1.0f;
    out->lid = fminf(life.lid, forced_lid);

    out->off_x = p->off_x + life.driftX;
    out->off_y = p->off_y + life.driftY;
    out->eye_dx = fit.x;
    out->eye_dy = fit.y;

    out->pose = *p;
    out->pose.sil.cx += out->off_x;
    out->pose.sil.cy += out->off_y;
    out->pose.sil.sy *= life.breath;
}
