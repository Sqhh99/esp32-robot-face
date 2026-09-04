#pragma once

#include <stdbool.h>
#include <stdint.h>

// Body silhouettes and the small set of non-radial primitives (capsules,
// tapered bars, the teardrop) that bloub's states are built from.
//
// Ported from bloub's src/bot/shape.ts. The web version turns a silhouette
// into an SVG path (a Catmull-Rom spline through the 64 sampled points) and
// lets the browser fill it. We do the same thing minus the spline: `toPoints`
// is ported verbatim, and the resulting 64-gon is filled with a scanline
// crossing pass. Straight edges instead of the spline costs 0.18 px of
// sagitta on a 145 px ball — invisible — and it is what makes the body cheap:
// per row it is a handful of edge crossings and a flat span write, instead of
// an atan2f/sqrtf per pixel.

#define PROFILE_SAMPLES 64

// r(theta) profiles measured frame-by-frame off the reference video, in
// units of the ball's resting radius. theta=0 points right, increasing
// clockwise (screen y-down) — see profiles.ts. Do not hand-edit; regenerate
// with tools/extract-profiles.py against the reference video if these ever
// need to change.
extern const float BLOUB_PROFILE_EGG[PROFILE_SAMPLES];
extern const float BLOUB_PROFILE_HEXAGON[PROFILE_SAMPLES];
extern const float BLOUB_PROFILE_TRIANGLE[PROFILE_SAMPLES];

// The "!" bars are convex hulls of two circles of different radii (tapered)
// or equal radii (a pure capsule); both are star-convex from their own
// centre, so — like egg/hexagon/triangle — they reduce to a 64-sample radial
// profile, precomputed the same way `profileFromPolygon` does in shape.ts.
// BAR_UPRIGHT's samples are relative to (0, BAR_UPRIGHT_CY), not (0,0).
extern const float BLOUB_PROFILE_BAR_UPRIGHT[PROFILE_SAMPLES];
extern const float BLOUB_PROFILE_BAR_ITALIC[PROFILE_SAMPLES];
#define BAR_UPRIGHT_CY (-0.1875f)

typedef struct {
    float radii[PROFILE_SAMPLES]; // ball-radius units
    float rot;                    // radians
    float cx, cy;                 // ball-radius units
    float sx, sy;                 // squash/stretch, applied after rotation
} silhouette_t;

void sil_circle(silhouette_t *s, float radius);
void sil_profile(silhouette_t *s, const float profile[PROFILE_SAMPLES]);

/** r(theta) via linear interpolation between the two nearest samples. */
float profile_radius_at(const float radii[PROFILE_SAMPLES], float theta);

// --- Body as a scanline-fillable polygon --------------------------------

/**
 * One non-horizontal polygon edge, in the form the scanline pass wants:
 * where it crosses its topmost row, and how far x moves per row.
 */
typedef struct {
    float y_top, y_bot; // y_top < y_bot
    float x_at_top;
    float dxdy;
} body_edge_t;

typedef struct {
    body_edge_t edges[PROFILE_SAMPLES];
    int count;
    float y_min, y_max; // screen rows the polygon spans
} body_poly_t;

/**
 * Projects a silhouette to absolute screen coordinates (bloub's `toPoints`,
 * plus the screen-centre offset) and builds its edge table. Called once per
 * frame, not per band.
 */
void body_poly_build(body_poly_t *out, const silhouette_t *s, float scale_px, float origin_x,
                     float origin_y);

#define BODY_MAX_CROSSINGS 16

/**
 * x coordinates where the polygon crosses scanline `fy`, sorted ascending.
 * Fill between consecutive pairs (even-odd rule). Returns how many were
 * written; normally 2, more only where a shape doubles back on a row.
 */
int body_poly_crossings(const body_poly_t *poly, float fy, float xs[BODY_MAX_CROSSINGS]);

// --- Capsules -------------------------------------------------------------

/**
 * A stadium (bloub's `capsulePath`), prepared so the per-pixel test needs no
 * square root: point-to-segment distance is compared squared. Used for the
 * eye holes and for every arc segment.
 */
typedef struct {
    float x0, y0, x1, y1;
    float dx, dy;
    float inv_l2; // 1/|b-a|^2, or 0 when the segment is degenerate (a disc)
    float r, r2;
} capsule_t;

void capsule_prepare(capsule_t *c, float x0, float y0, float x1, float y1, float r);

static inline bool capsule_contains(const capsule_t *c, float px, float py)
{
    float ax = px - c->x0;
    float ay = py - c->y0;
    float t = (ax * c->dx + ay * c->dy) * c->inv_l2;
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;
    float qx = ax - t * c->dx;
    float qy = ay - t * c->dy;
    return qx * qx + qy * qy <= c->r2;
}

/**
 * Point-in-shape test for the "hull of two circles" primitive with unequal
 * radii: a rounded cone / tapered capsule. Only the leaning "!"'s teardrop
 * still needs it at render time (the bars go through the profile path, and
 * equal-radius hulls are `capsule_t` above).
 */
bool hull2_contains(float x1, float y1, float r1, float x2, float y2, float r2, float px, float py);
