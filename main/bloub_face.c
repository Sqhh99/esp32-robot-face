#include "bloub_face.h"

#include <math.h>
#include <stdint.h>

#include "bloub_math.h"

const head_gaze_t BLOUB_REST_GAZE = {28.49f, 28.62f, -13.0f};

typedef struct {
    float x, y, z;
} vec3_t;

static inline float deg2rad(float d)
{
    return d * (float)M_PI / 180.0f;
}

/** Rotates two vectors of an orthonormal frame within their common plane. */
static inline void spin(vec3_t u, vec3_t v, float angle, vec3_t *out_u, vec3_t *out_v)
{
    float c = cosf(angle);
    float s = sinf(angle);
    out_u->x = u.x * c + v.x * s;
    out_u->y = u.y * c + v.y * s;
    out_u->z = u.z * c + v.z * s;
    out_v->x = v.x * c - u.x * s;
    out_v->y = v.y * c - u.y * s;
    out_v->z = v.z * c - u.z * s;
}

void eye_poses(head_gaze_t gaze, float scale, float split, eye_pose_t out[2])
{
    vec3_t f = {0, 0, 1};
    vec3_t right = {1, 0, 0};
    vec3_t down = {0, 1, 0};
    vec3_t tmp_f, tmp_r, tmp_d;

    spin(f, right, deg2rad(gaze.yaw), &tmp_f, &tmp_r);
    f = tmp_f;
    right = tmp_r;

    spin(down, f, deg2rad(gaze.pitch), &tmp_d, &tmp_f);
    down = tmp_d;
    f = tmp_f;

    spin(right, down, deg2rad(gaze.roll), &tmp_r, &tmp_d);
    right = tmp_r;
    down = tmp_d;

    const float sides[2] = {-1.0f, 1.0f};
    for (int i = 0; i < 2; i++) {
        vec3_t ef, er;
        spin(f, right, deg2rad(split * sides[i]), &ef, &er);
        out[i].x = ef.x * scale;
        out[i].y = ef.y * scale;
        out[i].a = er.x;
        out[i].b = er.y;
        out[i].c = down.x;
        out[i].d = down.y;
        out[i].depth = ef.z;
    }
}

// --- Blink schedule ---------------------------------------------------
//
// The source precomputes a finite (900s) array once at module load. This
// device runs indefinitely, so the same mulberry32 recurrence is instead
// carried forward lazily, a handful of entries at a time, giving the exact
// same statistics (1.9-4.6s between blinks, occasional double-blink) forever.

#define BLINK_DUR 0.18f
#define BLINK_QUEUE_CAP 4

static uint32_t s_blink_rng = 0x5eed;
static float s_blink_queue[BLINK_QUEUE_CAP];
static int s_blink_queue_len = 0;
static float s_blink_generated_up_to = -1.0f;

static void blink_schedule_ensure(float upto_t)
{
    if (s_blink_generated_up_to < 0.0f) {
        s_blink_queue[0] = 1.4f;
        s_blink_queue_len = 1;
        s_blink_generated_up_to = 1.4f;
    }
    while (s_blink_generated_up_to < upto_t + 5.0f && s_blink_queue_len < BLINK_QUEUE_CAP - 1) {
        float t = s_blink_generated_up_to;
        t += 1.9f + bloub_rng_next(&s_blink_rng) * 2.7f;
        s_blink_queue[s_blink_queue_len++] = t;
        s_blink_generated_up_to = t;
        if (bloub_rng_next(&s_blink_rng) < 0.18f) {
            t += 0.24f;
            s_blink_queue[s_blink_queue_len++] = t;
            s_blink_generated_up_to = t;
        }
    }
}

static float blink_lid(float t)
{
    blink_schedule_ensure(t);

    while (s_blink_queue_len > 0 && s_blink_queue[0] + BLINK_DUR < t) {
        for (int i = 1; i < s_blink_queue_len; i++) {
            s_blink_queue[i - 1] = s_blink_queue[i];
        }
        s_blink_queue_len--;
    }

    if (s_blink_queue_len > 0) {
        float start = s_blink_queue[0];
        if (t >= start) {
            float k = (t - start) / BLINK_DUR;
            if (k <= 1.0f) {
                return k < 0.45f ? 1.0f - k / 0.45f : (k - 0.45f) / 0.55f;
            }
        }
    }
    return 1.0f;
}

liveliness_t liveliness_sample(float t, float wander, bool blink, bool enable_float)
{
    liveliness_t out;
    out.dYaw = (bloub_loop_noise(t, 11.3f, 0.4f) * 5.5f + bloub_loop_noise(t, 3.7f, 2.1f) * 1.6f) * wander;
    out.dPitch = (bloub_loop_noise(t, 9.1f, 1.3f) * 4.2f + bloub_loop_noise(t, 4.3f, 0.7f) * 1.3f) * wander;
    out.dRoll = bloub_loop_noise(t, 13.7f, 3.2f) * 2.2f * wander;
    out.lid = blink ? blink_lid(t) : 1.0f;
    out.driftX = enable_float ? bloub_loop_noise(t, 7.9f, 1.9f) * 0.006f : 0.0f;
    out.driftY = enable_float ? bloub_loop_noise(t, 5.3f, 0.3f) * 0.007f : 0.0f;
    out.breath = enable_float ? 1.0f + sinf((t / 3.4f) * BLOUB_TAU) * 0.005f : 1.0f;
    return out;
}

float blink_scale(float lid)
{
    return 0.06f + 0.94f * bloub_clamp01(lid);
}
