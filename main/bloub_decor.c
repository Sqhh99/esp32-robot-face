#include "bloub_decor.h"

#include <math.h>

#include "bloub_math.h"

// a, k, tilt, speed, phase, sweep, hue, hueSpan, width, cx, cy
const arc_seed_t BLOUB_RINGS[6] = {
    {1.374316f, 0.274937f, 0.111637f, 3.531251f, 3.521581f, 0.745288f, 4.5382f, 63.3283f, 0.052606f, 0.0000f, 0.1000f},
    {1.347803f, 0.316944f, 1.009311f, 3.411252f, 6.146908f, 0.646808f, 81.6460f, 66.5198f, 0.060184f, 0.0000f, 0.1000f},
    {1.396025f, 0.407338f, 1.103976f, 3.680664f, 1.062764f, 0.841260f, 149.8437f, 61.3668f, 0.056178f, 0.0000f, 0.1000f},
    {1.385397f, 0.396188f, 1.996122f, 3.015526f, 1.116472f, 0.674048f, 185.7906f, 108.9912f, 0.051693f, 0.0000f, 0.1000f},
    {1.375075f, 0.413304f, 2.554288f, 3.306341f, 5.341918f, 0.834125f, 269.8506f, 73.2949f, 0.052619f, 0.0000f, 0.1000f},
    {1.303824f, 0.359854f, 3.015789f, 3.219782f, 2.674188f, 0.672155f, 327.0996f, 87.6283f, 0.055452f, 0.0000f, 0.1000f},
};

const arc_seed_t BLOUB_SWOOSH[4] = {
    {0.780000f, 0.050000f, -0.620000f, 0.300000f, 0.000000f, 0.400000f, 95.0000f, 100.0000f, 0.050000f, 0.0000f, -0.1200f},
    {0.980000f, 0.070000f, -0.570000f, 0.300000f, 0.060000f, 0.400000f, 157.0000f, 100.0000f, 0.050000f, 0.0000f, -0.1200f},
    {1.180000f, 0.090000f, -0.520000f, 0.300000f, 0.120000f, 0.400000f, 219.0000f, 100.0000f, 0.050000f, 0.0000f, -0.1200f},
    {1.380000f, 0.110000f, -0.470000f, 0.300000f, 0.180000f, 0.400000f, 281.0000f, 100.0000f, 0.050000f, 0.0000f, -0.1200f},
};

const arc_seed_t BLOUB_COMET_RIBBONS[4] = {
    {0.811750f, 0.134118f, 0.540912f, 0.583333f, 0.005392f, 0.340000f, 4.0841f, 80.0000f, 0.095000f, 0.0000f, 0.0000f},
    {0.837250f, 0.162353f, 0.575912f, 0.583333f, -0.041185f, 0.340000f, 96.5094f, 80.0000f, 0.095000f, 0.0000f, 0.0000f},
    {0.862750f, 0.190588f, 0.610912f, 0.583333f, -0.079678f, 0.340000f, 186.9965f, 80.0000f, 0.095000f, 0.0000f, 0.0000f},
    {0.888250f, 0.218824f, 0.645912f, 0.583333f, -0.130667f, 0.340000f, 267.1046f, 80.0000f, 0.095000f, 0.0000f, 0.0000f},
};

const particle_seed_t BLOUB_PARTICLES[5] = {
    {0.000000f, 4.177982f, 0.635674f},
    {0.200000f, 4.022911f, 0.631372f},
    {0.400000f, 6.147502f, 0.618600f},
    {0.600000f, 4.462918f, 0.675934f},
    {0.800000f, 0.894203f, 0.726892f},
};

int arc_generate_stamps(const arc_seed_t *seed, float t, float scale_px, float origin_x,
                         float origin_y, float opacity, arc_stamp_t *out)
{
    if (opacity <= 0.02f) {
        return 0;
    }
    uint32_t alpha = (uint32_t)(bloub_clamp01(opacity) * 255.0f + 0.5f);

    const int N = ARC_SEGMENTS;
    float spin_ = seed->phase + t * seed->speed * BLOUB_TAU;
    float cu = cosf(seed->tilt);
    float su = sinf(seed->tilt);
    float kz = sqrtf(fmaxf(0.0f, 1.0f - seed->k * seed->k));
    float span = seed->sweep * BLOUB_TAU;

    float px[ARC_SEGMENTS + 1], py[ARC_SEGMENTS + 1], pz[ARC_SEGMENTS + 1];
    for (int i = 0; i <= N; i++) {
        float th = spin_ + ((float)i / (float)N) * span;
        float ct = cosf(th);
        float st = sinf(th);
        float x = seed->a * (ct * cu + st * (-su) * seed->k) + seed->cx;
        float y = seed->a * (ct * su + st * cu * seed->k) + seed->cy;
        float z = seed->a * st * kz;
        px[i] = x * scale_px + origin_x;
        py[i] = y * scale_px + origin_y;
        pz[i] = z;
    }

    float radius = (seed->width * scale_px) * 0.5f;
    int count = 0;
    for (int i = 0; i < N; i++) {
        bool behind_a = pz[i] < 0.0f;
        bool behind_b = pz[i + 1] < 0.0f;
        if (behind_a != behind_b) {
            continue; // matches the source's own small gap at the crossing
        }
        float hue = seed->hue + seed->hue_span * ((float)i / (float)N);
        arc_stamp_t *s = &out[count++];
        capsule_prepare(&s->cap, px[i], py[i], px[i + 1], py[i + 1], radius);
        s->min_x = fminf(px[i], px[i + 1]) - radius;
        s->max_x = fmaxf(px[i], px[i + 1]) + radius;
        s->min_y = fminf(py[i], py[i + 1]) - radius;
        s->max_y = fmaxf(py[i], py[i + 1]) + radius;
        uint16_t cpu = bloub_wheel565(hue);
        s->color_cpu = cpu;
        s->color = bloub_panel_swap(cpu);
        s->alpha = (uint8_t)alpha;
        s->behind = behind_a;
    }
    return count;
}

int particles_sample(float t, float scale_px, dot_render_t out[PARTICLE_MAX_ACTIVE])
{
    int count = 0;
    for (int i = 0; i < 5; i++) {
        const particle_seed_t *p = &BLOUB_PARTICLES[i];
        float u = t - p->birth;
        if (u < 0.0f || u > 0.62f) {
            continue;
        }
        float opacity = bloub_clamp01(u / 0.06f) * bloub_clamp01((0.62f - u) / 0.08f);
        if (opacity <= 0.02f) {
            continue;
        }
        float rho = p->rho * powf(0.75f, u * 10.0f);
        float a = p->angle + (u * 100.0f * (float)M_PI) / 180.0f;
        dot_render_t *d = &out[count++];
        d->x = cosf(a) * rho * scale_px;
        d->y = sinf(a) * rho * scale_px;
        d->r = (0.04f + 0.028f * bloub_clamp01(u / 0.55f)) * scale_px;
        d->opacity = opacity;
        d->has_depth = true;
        d->depth = bloub_clamp01(1.0f - rho / 0.8f);
    }
    return count;
}
