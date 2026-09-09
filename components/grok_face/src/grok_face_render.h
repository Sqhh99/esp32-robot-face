#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bloub_decor.h"
#include "bloub_shapes.h"
#include "bloub_states.h"
#include "esp_err.h"
#include "grok_face_engine.h"

// The rasteriser: everything that knows about pixels, and nothing that knows
// about time.
//
// `render_prepare` resolves one engine frame into screen space -- once per
// frame, not once per band -- and `render_band` paints any horizontal slice of
// it. Splitting them is what lets a frame be drawn a band at a time into a
// small DMA buffer while the previous band is still in flight.

typedef struct {
    int width, height;
    int band_rows;
    float center_x, center_y;
    float radius;
    bool swap_color_bytes;
    /** Page colour, CPU-ordered RGB565. */
    uint16_t background;
} render_config_t;

/**
 * One eye, ready to be tested per pixel.
 *
 * The tangent frame is stored INVERTED, so the test maps a screen pixel into
 * the eye's own space rather than transforming the capsule's outline. The blink
 * squash is folded into the inverse, and so is the eye's own lean.
 */
typedef struct {
    bool active;
    float A, B, C, E; // lx = A*sx - B*sy; ly = E*sy - C*sx
    float tx, ty;     // absolute screen px, eye centre
    capsule_t local;  // the stadium, in the eye's own local space
    float min_x, max_x, min_y, max_y; // absolute screen bbox
} eye_render_t;

typedef struct {
    body_poly_t poly;
    eye_render_t eyes[2];
    uint8_t eye_alpha; // eye holes dissolve rather than pop during a morph

    bool has_notif;
    float notif_cx, notif_cy, notif_r, notif_notch_r; // absolute screen px

    bool has_teardrop;
    float tear_cos, tear_sin, tear_cx, tear_cy; // absolute screen px
    uint8_t tear_alpha;

    int stamp_count;
    arc_stamp_t stamps[POSE_MAX_ARCS * ARC_MAX_STAMPS];
    bool any_behind, any_front;

    int dot_count;
    struct {
        float cx, cy, r;
        uint8_t alpha;
    } dots[POSE_MAX_DOTS];
    bool dots_behind;

    /** Screen rows anything in this frame can touch, already clipped to the panel. */
    int row_lo, row_hi;
} render_frame_t;

void render_init(const render_config_t *config);

/**
 * Sets the body colour, CPU-ordered RGB565. The body, the dots and the
 * teardrop take it; the arcs keep their own hue wheel and the notification
 * pastille its own measured blue.
 */
void render_set_body_color(uint16_t rgb565_cpu);

/** Fills a band buffer with the page colour. */
void render_clear_band(uint16_t *buf);

/** Resolves one engine frame into screen space. Once per frame, not per band. */
void render_prepare(const engine_frame_t *frame, render_frame_t *out);

/** Paints the band starting at row `band_y0` into an already-cleared buffer. */
void render_band(uint16_t *buf, int band_y0, const render_frame_t *f);
