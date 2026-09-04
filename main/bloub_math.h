#pragma once

// Small, pure math helpers shared by the bloub port. Header-only: everything
// here is tiny and called from per-pixel hot paths, so `static inline` lets
// the compiler fold it away instead of paying a call per pixel.
//
// Ported from bloub's src/bot/math.ts — see that file's comments for the
// measured/derived rationale behind the easing curves and the noise mix.

#include <math.h>
#include <stdint.h>

#define BLOUB_TAU (6.28318530717958647692f)

static inline float bloub_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float bloub_clamp01(float v)
{
    return bloub_clampf(v, 0.0f, 1.0f);
}

static inline float bloub_lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static inline float bloub_ease_out_cubic(float t)
{
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

static inline float bloub_ease_in_out_cubic(float t)
{
    if (t < 0.5f) {
        return 4.0f * t * t * t;
    }
    float u = -2.0f * t + 2.0f;
    return 1.0f - (u * u * u) / 2.0f;
}

static inline float bloub_ease_out_quint(float t)
{
    float u = 1.0f - t;
    float u2 = u * u;
    return 1.0f - u2 * u2 * u;
}

/** Periodic 1D noise: loops seamlessly on `period`, used for gaze drift. */
static inline float bloub_loop_noise(float t, float period, float seed)
{
    float p = (t / period) * BLOUB_TAU;
    return 0.55f * sinf(p + seed) + 0.30f * sinf(2.0f * p + seed * 1.7f + 1.1f) +
           0.15f * sinf(3.0f * p + seed * 2.3f + 2.4f);
}

/** Deterministic PRNG (mulberry32), matching bloub's createRng bit for bit. */
static inline float bloub_rng_next(uint32_t *state)
{
    uint32_t a = (*state + 0x6d2b79f5u);
    *state = a;
    uint32_t t = (a ^ (a >> 15)) * (1u | a);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return (float)((t ^ (t >> 14)) >> 0) / 4294967296.0f;
}

/**
 * HSL-ish colour wheel at fixed saturation/lightness (measured on the
 * reference video: S 45-62%, L 50-67%), returned as RGB565. Matches
 * decor.ts's `wheel()`.
 */
/**
 * The panel takes RGB565 with the bytes the other way round from how the CPU
 * stores a uint16, so every colour is swapped once here, at the point it is
 * built. Pure white and pure black are palindromes and come through either
 * way, which is exactly why this was invisible until a saturated colour (the
 * notification pastille) turned up on screen as orange instead of blue.
 *
 * Everything downstream of these builders is already panel-ordered, so
 * nothing in the render loop pays for it. The corollary is that mixing
 * colours (fades) must happen BEFORE the swap — see bloub_blend565.
 */
static inline uint16_t bloub_panel_swap(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}

/**
 * Blends `src` over `dst` by `a` (0-255), in RGB565.
 *
 * Operates on CPU-ordered values, so call it before bloub_panel_swap. Used
 * to bake a fade into a shape's colour once per frame, never per pixel.
 */
static inline uint16_t bloub_blend565(uint16_t dst, uint16_t src, uint32_t a)
{
    uint32_t inv = 255u - a;
    uint32_t r = ((((dst >> 11) & 0x1F) * inv + ((src >> 11) & 0x1F) * a) + 128u) >> 8;
    uint32_t g = ((((dst >> 5) & 0x3F) * inv + ((src >> 5) & 0x3F) * a) + 128u) >> 8;
    uint32_t b = (((dst & 0x1F) * inv + (src & 0x1F) * a) + 128u) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/** CPU-ordered RGB565. Pass through bloub_panel_swap before it reaches a buffer. */
static inline uint16_t bloub_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/**
 * Composites a CPU-ordered colour onto a panel-ordered destination pixel.
 *
 * This is what a fade has to be. Scaling a colour toward black instead is
 * only equivalent where the background actually is black: over the white
 * body, a half-faded ring painted that way comes out as a dark streak rather
 * than a pale one. The two swaps are three instructions each and are paid
 * only on pixels that are genuinely translucent — a fully opaque shape takes
 * the plain-store path in the caller.
 */
static inline uint16_t bloub_blend_panel(uint16_t dst_panel, uint16_t src_cpu, uint32_t a)
{
    return bloub_panel_swap(bloub_blend565(bloub_panel_swap(dst_panel), src_cpu, a));
}

static inline uint16_t bloub_wheel565(float hue_deg)
{
    float h = fmodf(hue_deg, 360.0f);
    if (h < 0.0f) h += 360.0f;
    const float s = 0.55f;
    const float l = 0.62f;
    float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = l - c / 2.0f;
    float r, g, b;
    if (h < 60) { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }
    uint8_t R = (uint8_t)((r + m) * 255.0f + 0.5f);
    uint8_t G = (uint8_t)((g + m) * 255.0f + 0.5f);
    uint8_t B = (uint8_t)((b + m) * 255.0f + 0.5f);
    return bloub_rgb565(R, G, B); // CPU-ordered; fade first, then swap
}
