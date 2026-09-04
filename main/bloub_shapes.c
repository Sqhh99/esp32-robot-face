#include "bloub_shapes.h"

#include <math.h>
#include <string.h>

#include "bloub_math.h"

// clang-format off
const float BLOUB_PROFILE_EGG[PROFILE_SAMPLES] = {
    0.8369f,0.8424f,0.8497f,0.8585f,0.8674f,0.8775f,0.8878f,0.8983f,0.9089f,0.9185f,
    0.9288f,0.9374f,0.9445f,0.9504f,0.9543f,0.9559f,0.9555f,0.9519f,0.9466f,0.9389f,
    0.9302f,0.9193f,0.9085f,0.8969f,0.8852f,0.8734f,0.8625f,0.8513f,0.8411f,0.8325f,
    0.8243f,0.8179f,0.8137f,0.8112f,0.8102f,0.8128f,0.8178f,0.8262f,0.8374f,0.8518f,
    0.8702f,0.8922f,0.9169f,0.9446f,0.9741f,1.0023f,1.0267f,1.0433f,1.0481f,1.0393f,
    1.0216f,0.9970f,0.9697f,0.9418f,0.9169f,0.8949f,0.8760f,0.8604f,0.8490f,0.8394f,
    0.8337f,0.8314f,0.8305f,0.8326f
};

const float BLOUB_PROFILE_HEXAGON[PROFILE_SAMPLES] = {
    0.9210f,0.9282f,0.9441f,0.9706f,0.9984f,1.0059f,0.9896f,0.9562f,0.9290f,0.9124f,
    0.9047f,0.9058f,0.9157f,0.9349f,0.9642f,0.9873f,0.9882f,0.9665f,0.9336f,0.9105f,
    0.8968f,0.8918f,0.8955f,0.9080f,0.9293f,0.9611f,0.9820f,0.9812f,0.9590f,0.9282f,
    0.9089f,0.8978f,0.8964f,0.9026f,0.9189f,0.9439f,0.9778f,0.9990f,0.9964f,0.9713f,
    0.9439f,0.9274f,0.9196f,0.9206f,0.9308f,0.9502f,0.9799f,1.0121f,1.0226f,1.0071f,
    0.9752f,0.9510f,0.9366f,0.9316f,0.9351f,0.9485f,0.9711f,1.0026f,1.0213f,1.0155f,
    0.9863f,0.9547f,0.9347f,0.9232f
};

const float BLOUB_PROFILE_TRIANGLE[PROFILE_SAMPLES] = {
    0.7819f,0.8211f,0.8747f,0.9440f,1.0223f,1.0960f,1.1401f,1.1340f,1.0808f,1.0047f,
    0.9265f,0.8603f,0.8104f,0.7730f,0.7450f,0.7273f,0.7151f,0.7118f,0.7148f,0.7245f,
    0.7427f,0.7680f,0.8037f,0.8518f,0.9148f,0.9876f,1.0583f,1.1073f,1.1109f,1.0667f,
    0.9940f,0.9164f,0.8482f,0.7948f,0.7555f,0.7261f,0.7056f,0.6925f,0.6859f,0.6869f,
    0.6938f,0.7084f,0.7305f,0.7615f,0.8040f,0.8595f,0.9311f,1.0092f,1.0791f,1.1171f,
    1.1054f,1.0501f,0.9779f,0.9050f,0.8450f,0.7990f,0.7656f,0.7413f,0.7258f,0.7160f,
    0.7146f,0.7204f,0.7330f,0.7528f
};
const float BLOUB_PROFILE_BAR_UPRIGHT[PROFILE_SAMPLES] = {
    0.103920f,0.103504f,0.104089f,0.105706f,0.108434f,0.112417f,0.117884f,0.125176f,0.134814f,0.147600f,
    0.164819f,0.188642f,0.223027f,0.275992f,0.353672f,0.384171f,0.392500f,0.384171f,0.353672f,0.275992f,
    0.223027f,0.188642f,0.164819f,0.147600f,0.134814f,0.125176f,0.117884f,0.112417f,0.108434f,0.105706f,
    0.104089f,0.103504f,0.103920f,0.105358f,0.107890f,0.111648f,0.116844f,0.123797f,0.132992f,0.145173f,
    0.161522f,0.184018f,0.216214f,0.265161f,0.344724f,0.398216f,0.427931f,0.444211f,0.449500f,0.444211f,
    0.427931f,0.398216f,0.344724f,0.265161f,0.216214f,0.184018f,0.161522f,0.145173f,0.132992f,0.123797f,
    0.116844f,0.111648f,0.107890f,0.105358f
};

const float BLOUB_PROFILE_BAR_ITALIC[PROFILE_SAMPLES] = {
    0.134500f,0.135151f,0.137135f,0.140552f,0.145582f,0.152508f,0.161762f,0.173995f,0.190212f,0.212014f,
    0.242094f,0.285322f,0.327276f,0.355090f,0.373649f,0.384398f,0.388000f,0.384398f,0.373649f,0.355090f,
    0.327276f,0.285322f,0.242094f,0.212014f,0.190212f,0.173995f,0.161762f,0.152508f,0.145582f,0.140552f,
    0.137135f,0.135151f,0.134500f,0.135151f,0.137135f,0.140552f,0.145582f,0.152508f,0.161762f,0.173995f,
    0.190212f,0.212014f,0.242094f,0.285322f,0.327276f,0.355090f,0.373649f,0.384398f,0.388000f,0.384398f,
    0.373649f,0.355090f,0.327276f,0.285322f,0.242094f,0.212014f,0.190212f,0.173995f,0.161762f,0.152508f,
    0.145582f,0.140552f,0.137135f,0.135151f
};
// clang-format on

void sil_circle(silhouette_t *s, float radius)
{
    for (int i = 0; i < PROFILE_SAMPLES; i++) {
        s->radii[i] = radius;
    }
    s->rot = 0.0f;
    s->cx = 0.0f;
    s->cy = 0.0f;
    s->sx = 1.0f;
    s->sy = 1.0f;
}

void sil_profile(silhouette_t *s, const float profile[PROFILE_SAMPLES])
{
    memcpy(s->radii, profile, sizeof(float) * PROFILE_SAMPLES);
    s->rot = 0.0f;
    s->cx = 0.0f;
    s->cy = 0.0f;
    s->sx = 1.0f;
    s->sy = 1.0f;
}

float profile_radius_at(const float radii[PROFILE_SAMPLES], float theta)
{
    float t = (theta / BLOUB_TAU) * PROFILE_SAMPLES;
    t = fmodf(t, (float)PROFILE_SAMPLES);
    if (t < 0.0f) t += (float)PROFILE_SAMPLES;
    int i0 = (int)t;
    int i1 = (i0 + 1) % PROFILE_SAMPLES;
    float frac = t - (float)i0;
    return bloub_lerp(radii[i0], radii[i1], frac);
}

// cos/sin of the 64 fixed profile angles. Built once; the profile's angles
// never change, so there is no reason to pay 128 trig calls per frame.
static float s_angle_cos[PROFILE_SAMPLES];
static float s_angle_sin[PROFILE_SAMPLES];
static bool s_angles_ready = false;

static void ensure_angle_tables(void)
{
    if (s_angles_ready) return;
    for (int i = 0; i < PROFILE_SAMPLES; i++) {
        float th = ((float)i / (float)PROFILE_SAMPLES) * BLOUB_TAU;
        s_angle_cos[i] = cosf(th);
        s_angle_sin[i] = sinf(th);
    }
    s_angles_ready = true;
}

void body_poly_build(body_poly_t *out, const silhouette_t *s, float scale_px, float origin_x,
                     float origin_y)
{
    ensure_angle_tables();

    // bloub's toPoints(): polar -> rotate -> squash -> translate -> scale.
    float cr = cosf(s->rot);
    float sr = sinf(s->rot);
    float px[PROFILE_SAMPLES], py[PROFILE_SAMPLES];
    for (int i = 0; i < PROFILE_SAMPLES; i++) {
        float r = s->radii[i];
        float x = r * s_angle_cos[i];
        float y = r * s_angle_sin[i];
        float rx = x * cr - y * sr;
        float ry = x * sr + y * cr;
        px[i] = (rx * s->sx + s->cx) * scale_px + origin_x;
        py[i] = (ry * s->sy + s->cy) * scale_px + origin_y;
    }

    out->count = 0;
    out->y_min = 1e30f;
    out->y_max = -1e30f;
    for (int i = 0; i < PROFILE_SAMPLES; i++) {
        int j = (i + 1) % PROFILE_SAMPLES;
        float ax = px[i], ay = py[i];
        float bx = px[j], by = py[j];
        if (ay == by) continue; // horizontal edges cross no scanline
        body_edge_t *e = &out->edges[out->count++];
        if (ay < by) {
            e->y_top = ay;
            e->y_bot = by;
            e->x_at_top = ax;
            e->dxdy = (bx - ax) / (by - ay);
        } else {
            e->y_top = by;
            e->y_bot = ay;
            e->x_at_top = bx;
            e->dxdy = (ax - bx) / (ay - by);
        }
        if (e->y_top < out->y_min) out->y_min = e->y_top;
        if (e->y_bot > out->y_max) out->y_max = e->y_bot;
    }
}

int body_poly_crossings(const body_poly_t *poly, float fy, float xs[BODY_MAX_CROSSINGS])
{
    int n = 0;
    for (int i = 0; i < poly->count; i++) {
        const body_edge_t *e = &poly->edges[i];
        // Half-open in y so a shared vertex is counted exactly once.
        if (fy < e->y_top || fy >= e->y_bot) continue;
        if (n == BODY_MAX_CROSSINGS) break;
        xs[n++] = e->x_at_top + (fy - e->y_top) * e->dxdy;
    }
    for (int i = 1; i < n; i++) {
        float v = xs[i];
        int j = i - 1;
        while (j >= 0 && xs[j] > v) {
            xs[j + 1] = xs[j];
            j--;
        }
        xs[j + 1] = v;
    }
    return n;
}

void capsule_prepare(capsule_t *c, float x0, float y0, float x1, float y1, float r)
{
    c->x0 = x0;
    c->y0 = y0;
    c->x1 = x1;
    c->y1 = y1;
    c->dx = x1 - x0;
    c->dy = y1 - y0;
    float l2 = c->dx * c->dx + c->dy * c->dy;
    c->inv_l2 = l2 > 1e-9f ? 1.0f / l2 : 0.0f;
    c->r = r;
    c->r2 = r * r;
}

bool hull2_contains(float x1, float y1, float r1, float x2, float y2, float r2, float px, float py)
{
    float ax = x2 - x1;
    float ay = y2 - y1;
    float len_sq = ax * ax + ay * ay;
    if (len_sq < 1e-9f) {
        float rmax = r1 > r2 ? r1 : r2;
        float dx = px - x1, dy = py - y1;
        return dx * dx + dy * dy <= rmax * rmax;
    }
    float len = sqrtf(len_sq);
    float nx = ax / len, ny = ay / len;
    float pxr = px - x1, pyr = py - y1;
    float u = pxr * nx + pyr * ny;
    if (u <= 0.0f) {
        return pxr * pxr + pyr * pyr <= r1 * r1;
    }
    if (u >= len) {
        float qx = px - x2, qy = py - y2;
        return qx * qx + qy * qy <= r2 * r2;
    }
    float v = -pxr * ny + pyr * nx;
    float dr = r1 - r2;
    float k_sq = len_sq - dr * dr;
    if (k_sq <= 0.0f) {
        // Degenerate (one circle swallows the axis): fall back to whichever
        // cap is closer.
        float dx = px - x1, dy = py - y1;
        return dx * dx + dy * dy <= r1 * r1;
    }
    float k = sqrtf(k_sq);
    return (dr * u + k * fabsf(v)) <= r1 * len;
}
