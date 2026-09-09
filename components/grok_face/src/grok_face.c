#include "grok_face.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "bloub_decor.h"
#include "bloub_face.h"
#include "bloub_math.h"
#include "bloub_shapes.h"
#include "bloub_states.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "grok_face";

static grok_face_config_t s_config;
static bool s_initialized;

#define LCD_H_RES (s_config.width)
#define LCD_V_RES (s_config.height)
#define BAND_ROWS (s_config.band_rows)
#define BAND_COUNT (LCD_V_RES / BAND_ROWS)
#define BALL_CX (s_config.center_x)
#define BALL_CY (s_config.center_y)
#define BALL_R (s_config.radius)
#define COLOR_BG 0x0000
#define COLOR_FG 0xFFFF
#define NOTIF_R8 46
#define NOTIF_G8 232
#define NOTIF_B8 106

static bloub_state_id_t s_state = STATE_IDLE;
static bloub_state_id_t s_prev_state = STATE_IDLE;
static bool s_has_prev = false;
static double s_state_entered_s = 0.0;
static double s_prev_entered_s = 0.0;
static double s_blink_at_s = -10.0; // global time a forced (state-change) blink started

// Anything dimmer than this is not worth a draw call.
#define MIN_VISIBLE 0.02f

// --- Eye rendering ----------------------------------------------------
//
// Each eye is a stadium (bloub's `capsulePath`) drawn through the sphere's
// tangent-frame matrix, bloub's own blink squash, and the body-radius fit
// that keeps it on a non-circular silhouette's actual surface. None of our
// states give an eye its own extra tilt, so — unlike engine.ts — there is no
// per-eye rotation to compose on top of the tangent frame.
//
// The eye is a HOLE, not a white shape: the render pass fills the body and
// then erases the eye's pixels back to the background, which is what makes it
// clip itself against the silhouette for free.

typedef struct {
    bool active;
    float A, B, C, E; // lx = A*sx - B*sy; ly = E*sy - C*sx  (inverse of the tangent-frame matrix)
    float tx, ty;     // absolute screen px, eye centre
    capsule_t local;  // the stadium, in the eye's own local space
    float min_x, max_x, min_y, max_y; // absolute screen bbox
} eye_render_t;

static void prepare_eye(const eye_pose_t *ep, const eye_cfg_t *cfg, float fit, float off_x_px,
                        float off_y_px, float k, eye_render_t *out)
{
    float ax = ep->a, ay = ep->b, cx2 = ep->c, cy2 = ep->d;
    float det = ax * cy2 - cx2 * ay;
    if (fabsf(det) < 1e-6f || k < 1e-4f) {
        out->active = false;
        return;
    }
    float inv_det = 1.0f / det;
    out->A = cy2 * inv_det;
    out->B = (cx2 * inv_det) / k;
    out->C = ay * inv_det;
    out->E = (ax * inv_det) / k;
    out->tx = ep->x * fit + off_x_px + BALL_CX;
    out->ty = ep->y * fit + off_y_px + BALL_CY;

    float hw = cfg->w * BALL_R * 0.5f;
    float hh = cfg->h * BALL_R * 0.5f;
    // Stadium inscribed in [-hw,hw] x [-hh,hh]: the short axis is the radius.
    if (hh >= hw) {
        capsule_prepare(&out->local, 0.0f, -(hh - hw), 0.0f, hh - hw, hw);
    } else {
        capsule_prepare(&out->local, -(hw - hh), 0.0f, hw - hh, 0.0f, hh);
    }

    float bbox_hw = fabsf(ax) * hw + fabsf(cx2) * hh + 1.0f;
    float bbox_hh = fabsf(ay * k) * hw + fabsf(cy2 * k) * hh + 1.0f;
    out->min_x = out->tx - bbox_hw;
    out->max_x = out->tx + bbox_hw;
    out->min_y = out->ty - bbox_hh;
    out->max_y = out->ty + bbox_hh;
    out->active = true;
}

// --- Span / stamp rasterizers -------------------------------------------

static inline void clip_x(int *min_x, int *max_x)
{
    if (*min_x < 0) *min_x = 0;
    if (*max_x > LCD_H_RES - 1) *max_x = LCD_H_RES - 1;
}

static inline void clip_y(int *min_y, int *max_y, int band_y0)
{
    if (*min_y < band_y0) *min_y = band_y0;
    if (*max_y > band_y0 + BAND_ROWS - 1) *max_y = band_y0 + BAND_ROWS - 1;
}

/** Flat horizontal run, written 32 bits at a time. Rows are 4-byte aligned (240*2 = 480). */
static inline void fill_span(uint16_t *row, int xa, int xb, uint16_t color)
{
    if (xa > xb) return;
    if (xa & 1) row[xa++] = color;
    int n32 = (xb - xa + 1) >> 1;
    uint32_t pair = ((uint32_t)color << 16) | color;
    uint32_t *p32 = (uint32_t *)(void *)(row + xa);
    for (int i = 0; i < n32; i++) p32[i] = pair;
    for (int x = xa + (n32 << 1); x <= xb; x++) row[x] = color;
}

/**
 * Rasterizes one arc segment.
 *
 * Per row it narrows x to the part of the segment that row can actually
 * reach, rather than sweeping the whole bounding box: a diagonal segment
 * fills only a sliver of its box, and on the orbit rings that sliver was
 * about a third of the width being tested. Solving |y0 + t*dy - y| <= r for
 * t, clamping to the segment, and padding by r gives a tight superset for
 * free.
 */
static void draw_stamp(uint16_t *buf, int band_y0, const arc_stamp_t *s)
{
    int min_y = (int)floorf(s->min_y), max_y = (int)ceilf(s->max_y);
    clip_y(&min_y, &max_y, band_y0);
    if (min_y > max_y) return;
    int box_min_x = (int)floorf(s->min_x), box_max_x = (int)ceilf(s->max_x);
    clip_x(&box_min_x, &box_max_x);
    if (box_min_x > box_max_x) return;

    const capsule_t *c = &s->cap;
    bool steep = fabsf(c->dy) > 1e-3f;
    float inv_dy = steep ? 1.0f / c->dy : 0.0f;

    for (int y = min_y; y <= max_y; y++) {
        float fy = (float)y;
        int min_x = box_min_x, max_x = box_max_x;

        if (steep) {
            float ta = (fy - c->r - c->y0) * inv_dy;
            float tb = (fy + c->r - c->y0) * inv_dy;
            if (ta > tb) {
                float tmp = ta;
                ta = tb;
                tb = tmp;
            }
            ta = bloub_clamp01(ta);
            tb = bloub_clamp01(tb);
            float xa = c->x0 + ta * c->dx;
            float xb = c->x0 + tb * c->dx;
            if (xa > xb) {
                float tmp = xa;
                xa = xb;
                xb = tmp;
            }
            int lo = (int)floorf(xa - c->r);
            int hi = (int)ceilf(xb + c->r);
            if (lo > min_x) min_x = lo;
            if (hi < max_x) max_x = hi;
            if (min_x > max_x) continue;
        }

        uint16_t *row = buf + (y - band_y0) * LCD_H_RES;
        if (s->alpha == 255) {
            for (int x = min_x; x <= max_x; x++) {
                if (capsule_contains(c, (float)x, fy)) row[x] = s->color;
            }
        } else {
            for (int x = min_x; x <= max_x; x++) {
                if (capsule_contains(c, (float)x, fy)) {
                    row[x] = bloub_blend_panel(row[x], s->color_cpu, s->alpha,
                                               s_config.swap_color_bytes);
                }
            }
        }
    }
}

/**
 * Filled circle. Opaque discs take the analytic span path (one sqrt a row,
 * then a flat 32-bit fill); translucent ones composite per pixel.
 */
static void draw_disc(uint16_t *buf, int band_y0, float cx, float cy, float r, uint16_t color_panel,
                      uint16_t color_cpu, uint8_t alpha)
{
    int min_x = (int)floorf(cx - r), max_x = (int)ceilf(cx + r);
    int min_y = (int)floorf(cy - r), max_y = (int)ceilf(cy + r);
    clip_x(&min_x, &max_x);
    clip_y(&min_y, &max_y, band_y0);
    if (min_x > max_x || min_y > max_y) return;

    float r2 = r * r;
    for (int y = min_y; y <= max_y; y++) {
        float dy = (float)y - cy;
        float half = r2 - dy * dy;
        if (half <= 0.0f) continue;
        // Solve the row's span directly instead of testing every pixel.
        half = sqrtf(half);
        int xa = (int)ceilf(cx - half - 0.5f);
        int xb = (int)ceilf(cx + half - 0.5f) - 1;
        if (xa < min_x) xa = min_x;
        if (xb > max_x) xb = max_x;
        if (xa > xb) continue;
        uint16_t *row = buf + (y - band_y0) * LCD_H_RES;
        if (alpha == 255) {
            fill_span(row, xa, xb, color_panel);
        } else {
            for (int x = xa; x <= xb; x++) {
                row[x] = bloub_blend_panel(row[x], color_cpu, alpha,
                                           s_config.swap_color_bytes);
            }
        }
    }
}

// --- Frame setup, done once per frame (not per band) --------------------

typedef struct {
    pose_t pose;
    body_poly_t poly;
    eye_render_t eyes[2];

    bool has_notif;
    float notif_cx, notif_cy, notif_r, notif_notch_r; // absolute screen px

    bool has_teardrop;
    float tear_cos, tear_sin, tear_cx, tear_cy; // absolute screen px
    uint8_t tear_alpha;

    int stamp_count;
    arc_stamp_t stamps[POSE_MAX_ARCS * ARC_MAX_STAMPS];
    bool any_behind, any_front;

    // Dots, resolved to screen pixels and colour.
    int dot_count;
    struct {
        float cx, cy, r;
        uint8_t alpha;
    } dots[POSE_MAX_DOTS];
    bool dots_behind;

    uint8_t eye_alpha; // eye holes dissolve rather than pop during a morph

    float off_x_px, off_y_px;

    int row_lo, row_hi; // screen rows anything in this frame can touch
} frame_t;

static frame_t s_frame; // too big for the render task's stack
static pose_t s_pose_cur;
static pose_t s_pose_prev;
static pose_t s_pose_blend;

// Bands touched by the previous frame. A frame only repaints the bands its
// own content reaches, so when the content shrinks (the sleeping dot bouncing
// away, the burst collapsing) the bands it has just left have to be blanked
// once, or they keep the previous frame's pixels.
static int s_prev_band_first = 0;
static int s_prev_band_last = 0;

/** Samples one state at its own clock, wrapped if it is a one-shot animation. */
static void sample_state(bloub_state_id_t id, float elapsed, pose_t *out)
{
    float loop = bloub_state_loop_period(id);
    if (loop > 0.0f) elapsed = fmodf(elapsed, loop);
    bloub_pose_sample(id, elapsed, out);
}

static void grow_rows(frame_t *f, float lo, float hi)
{
    if (lo < f->row_lo) f->row_lo = (int)floorf(lo);
    if (hi > f->row_hi) f->row_hi = (int)ceilf(hi);
}

static void prepare_frame(double now_s, float global_t, frame_t *f)
{
    // --- pose, including the cross-fade out of the previous state ---------
    float since = (float)(now_s - s_state_entered_s);
    sample_state(s_state, since, &s_pose_cur);

    pose_t *p = &s_pose_cur;
    float morph = bloub_state_morph(s_state);
    if (s_has_prev && since < morph) {
        sample_state(s_prev_state, (float)(now_s - s_prev_entered_s), &s_pose_prev);
        // Exponential ease-out, the curve measured off the reference video.
        float ratio = bloub_ease_out_quint(bloub_clamp01(since / morph));
        bloub_pose_blend(&s_pose_prev, &s_pose_cur, ratio, &s_pose_blend);
        p = &s_pose_blend;
    }
    f->pose = *p;
    p = &f->pose;

    // --- liveliness -------------------------------------------------------
    // Blink and gaze wander are gated on the face being substantially there,
    // but the holes themselves fade with eye_alpha so a face dissolving into
    // a "!" or a sleeping dot does not pop out of existence.
    f->eye_alpha = (uint8_t)(bloub_clamp01(p->eye_alpha) * 255.0f + 0.5f);
    bool visible_eyes = p->eye_alpha > MIN_VISIBLE;
    bool alive = p->eye_alpha > 0.5f;
    // `float` (breath/drift) is always on in the source; only blink and gaze
    // wander are gated by whether the face is actually showing.
    liveliness_t life = liveliness_sample(global_t, alive ? 1.0f : 0.0f, alive, true);

    float forced = bloub_clamp01((float)(global_t - s_blink_at_s) / 0.2f);
    float forced_lid = forced < 1.0f ? fabsf(forced * 2.0f - 1.0f) : 1.0f;
    float lid = fminf(life.lid, forced_lid);
    float k = blink_scale(lid);

    head_gaze_t gaze = {
        .yaw = p->gaze.yaw + life.dYaw,
        .pitch = p->gaze.pitch + life.dPitch,
        .roll = p->gaze.roll + life.dRoll,
    };

    f->off_x_px = (p->off_x + life.driftX) * BALL_R;
    f->off_y_px = (p->off_y + life.driftY) * BALL_R;

    // --- body -------------------------------------------------------------
    silhouette_t sil = p->sil;
    sil.cx += p->off_x + life.driftX;
    sil.cy += p->off_y + life.driftY;
    sil.sy *= life.breath;
    body_poly_build(&f->poly, &sil, BALL_R, BALL_CX, BALL_CY);

    f->row_lo = (int)floorf(f->poly.y_min);
    f->row_hi = (int)ceilf(f->poly.y_max);

    // --- eyes -------------------------------------------------------------
    f->eyes[0].active = false;
    f->eyes[1].active = false;
    if (visible_eyes) {
        eye_pose_t eps[2];
        eye_poses(gaze, BALL_R, p->split, eps);
        for (int i = 0; i < 2; i++) {
            if (eps[i].depth <= 0.02f) continue;
            float angle = atan2f(eps[i].y, eps[i].x) - p->sil.rot;
            if (angle < 0.0f) angle += BLOUB_TAU;
            float fit = profile_radius_at(p->sil.radii, angle);
            prepare_eye(&eps[i], &p->eyes[i], fit, f->off_x_px, f->off_y_px, k, &f->eyes[i]);
        }
    }
    // Eyes need no row growth: they are holes punched out of the body, so
    // they can never paint outside it.

    // --- dots -------------------------------------------------------------
    f->dot_count = 0;
    f->dots_behind = p->dots_behind;
    for (int i = 0; i < p->dot_count; i++) {
        const dot_render_t *d = &p->dots[i];
        // `depth` is the burst particles' haze — in the source it mixes the
        // dot toward the page colour as it recedes, which is the same thing
        // as fading it out, so the two multiply into one alpha.
        float bright = d->opacity * (d->has_depth ? d->depth : 1.0f);
        if (bright <= MIN_VISIBLE || d->r <= 0.0005f) continue;
        float cx = d->x * BALL_R + f->off_x_px + BALL_CX;
        float cy = d->y * BALL_R + f->off_y_px + BALL_CY;
        float r = d->r * BALL_R;
        f->dots[f->dot_count].cx = cx;
        f->dots[f->dot_count].cy = cy;
        f->dots[f->dot_count].r = r;
        f->dots[f->dot_count].alpha = (uint8_t)(bloub_clamp01(bright) * 255.0f + 0.5f);
        f->dot_count++;
        grow_rows(f, cy - r - 1.0f, cy + r + 1.0f);
    }

    // --- notification pastille + the notch it cuts into the body ----------
    f->has_notif = p->has_notif;
    if (f->has_notif) {
        float angle = atan2f(p->notif_y, p->notif_x) - p->sil.rot;
        if (angle < 0.0f) angle += BLOUB_TAU;
        float fit = profile_radius_at(p->sil.radii, angle);
        f->notif_cx = (p->notif_x * fit) * BALL_R + f->off_x_px + BALL_CX;
        f->notif_cy = (p->notif_y * fit) * BALL_R + f->off_y_px + BALL_CY;
        f->notif_r = p->notif_r * BALL_R;
        f->notif_notch_r = p->notif_notch_r * BALL_R;
        float reach = fmaxf(f->notif_r, f->notif_notch_r) + 1.0f;
        grow_rows(f, f->notif_cy - reach, f->notif_cy + reach);
    }

    // --- the leaning "!"'s teardrop ---------------------------------------
    f->has_teardrop = p->has_teardrop && p->tear_opacity > MIN_VISIBLE;
    if (f->has_teardrop) {
        f->tear_cos = cosf(p->tear_rot);
        f->tear_sin = sinf(p->tear_rot);
        f->tear_cx = p->tear_x * BALL_R + f->off_x_px + BALL_CX;
        f->tear_cy = p->tear_y * BALL_R + f->off_y_px + BALL_CY;
        f->tear_alpha = (uint8_t)(bloub_clamp01(p->tear_opacity) * 255.0f + 0.5f);
        const float reach = 0.20f * BALL_R;
        grow_rows(f, f->tear_cy - reach, f->tear_cy + reach);
    }

    // --- arcs -------------------------------------------------------------
    // Generated once, tagged front/back, reused by every band.
    f->stamp_count = 0;
    f->any_behind = false;
    f->any_front = false;
    for (int a = 0; a < p->arc_count; a++) {
        if (p->arc_opacity[a] <= MIN_VISIBLE) continue;
        arc_stamp_t tmp[ARC_MAX_STAMPS];
        int n = arc_generate_stamps(&p->arcs[a], p->arc_t, BALL_R, BALL_CX, BALL_CY,
                                    p->arc_opacity[a], s_config.swap_color_bytes, tmp);
        for (int i = 0; i < n && f->stamp_count < (int)(sizeof(f->stamps) / sizeof(f->stamps[0]));
             i++) {
            if (tmp[i].behind) f->any_behind = true;
            else f->any_front = true;
            f->stamps[f->stamp_count++] = tmp[i];
        }
        // An ellipse of semi-major axis `a` centred at `cy` cannot get further
        // from the ball's centre than |cy| + a + half the stroke. That spread
        // is wide: comet's ribbons stay inside 0.94 of the ball radius while
        // orbit's outermost ring runs past 1.52 of it.
        const arc_seed_t *s = &p->arcs[a];
        float reach = (fabsf(s->cy) + s->a + s->width * 0.5f) * BALL_R;
        grow_rows(f, BALL_CY - reach, BALL_CY + reach);
    }

    if (f->row_lo < 0) f->row_lo = 0;
    if (f->row_hi > LCD_V_RES - 1) f->row_hi = LCD_V_RES - 1;
}

// --- Per-band painting ---------------------------------------------------

static void paint_body(uint16_t *buf, int band_y0, const frame_t *f, int row_lo, int row_hi)
{
    float xs[BODY_MAX_CROSSINGS];
    for (int y = row_lo; y <= row_hi; y++) {
        float fy = (float)y + 0.5f;
        if (fy < f->poly.y_min || fy >= f->poly.y_max) continue;
        int n = body_poly_crossings(&f->poly, fy, xs);
        if (n < 2) continue;
        uint16_t *row = buf + (y - band_y0) * LCD_H_RES;
        for (int i = 0; i + 1 < n; i += 2) {
            int xa = (int)ceilf(xs[i] - 0.5f);
            int xb = (int)ceilf(xs[i + 1] - 0.5f) - 1;
            if (xa < 0) xa = 0;
            if (xb > LCD_H_RES - 1) xb = LCD_H_RES - 1;
            fill_span(row, xa, xb, COLOR_FG);
        }
    }
}

/**
 * Erases the eye holes and the notification notch.
 *
 * Only pixels the body itself just painted are erased (the COLOR_FG test):
 * that is what clips a hole against the silhouette, and it also protects the
 * back half of the rings, which is drawn under the body and must stay visible
 * where the body isn't — matching the source, where the body is backed by an
 * opaque page-coloured path precisely so a ring passing behind the ball does
 * not reappear inside the eyes.
 */
static void punch_holes(uint16_t *buf, int band_y0, const frame_t *f)
{
    for (int e = 0; e < 2; e++) {
        const eye_render_t *eye = &f->eyes[e];
        if (!eye->active) continue;
        int min_y = (int)floorf(eye->min_y), max_y = (int)ceilf(eye->max_y);
        clip_y(&min_y, &max_y, band_y0);
        if (min_y > max_y) continue;
        int min_x = (int)floorf(eye->min_x), max_x = (int)ceilf(eye->max_x);
        clip_x(&min_x, &max_x);
        if (min_x > max_x) continue;

        for (int y = min_y; y <= max_y; y++) {
            uint16_t *row = buf + (y - band_y0) * LCD_H_RES;
            float sy = (float)y - eye->ty;
            float lx_base = -eye->B * sy;
            float ly_base = eye->E * sy;
            for (int x = min_x; x <= max_x; x++) {
                if (row[x] != COLOR_FG) continue;
                float sx = (float)x - eye->tx;
                float lx = eye->A * sx + lx_base;
                float ly = ly_base - eye->C * sx;
                if (!capsule_contains(&eye->local, lx, ly)) continue;
                row[x] = (f->eye_alpha == 255)
                             ? COLOR_BG
                             : bloub_blend_panel(row[x], 0x0000, f->eye_alpha,
                                                 s_config.swap_color_bytes);
            }
        }
    }

    if (f->has_notif) {
        float r = f->notif_notch_r;
        int min_y = (int)floorf(f->notif_cy - r), max_y = (int)ceilf(f->notif_cy + r);
        clip_y(&min_y, &max_y, band_y0);
        int min_x = (int)floorf(f->notif_cx - r), max_x = (int)ceilf(f->notif_cx + r);
        clip_x(&min_x, &max_x);
        float r2 = r * r;
        for (int y = min_y; y <= max_y; y++) {
            uint16_t *row = buf + (y - band_y0) * LCD_H_RES;
            float dy = (float)y - f->notif_cy;
            for (int x = min_x; x <= max_x; x++) {
                if (row[x] != COLOR_FG) continue;
                float dx = (float)x - f->notif_cx;
                if (dx * dx + dy * dy <= r2) row[x] = COLOR_BG;
            }
        }
    }
}

static void paint_dots(uint16_t *buf, int band_y0, const frame_t *f)
{
    for (int i = 0; i < f->dot_count; i++) {
        draw_disc(buf, band_y0, f->dots[i].cx, f->dots[i].cy, f->dots[i].r, COLOR_FG, 0xFFFF,
                  f->dots[i].alpha);
    }
}

static void paint_teardrop(uint16_t *buf, int band_y0, const frame_t *f)
{
    // Circumradius of the hull (tip at 0.172 + its 0.012 cap), with slack.
    const float reach = 0.20f * BALL_R;
    int min_y = (int)floorf(f->tear_cy - reach), max_y = (int)ceilf(f->tear_cy + reach);
    clip_y(&min_y, &max_y, band_y0);
    if (min_y > max_y) return;
    int min_x = (int)floorf(f->tear_cx - reach), max_x = (int)ceilf(f->tear_cx + reach);
    clip_x(&min_x, &max_x);

    const float r1 = 0.118f * BALL_R, r2 = 0.012f * BALL_R, tip = 0.172f * BALL_R;
    for (int y = min_y; y <= max_y; y++) {
        uint16_t *row = buf + (y - band_y0) * LCD_H_RES;
        float uy = (float)y - f->tear_cy;
        for (int x = min_x; x <= max_x; x++) {
            float ux = (float)x - f->tear_cx;
            float lx = ux * f->tear_cos + uy * f->tear_sin;
            float ly = -ux * f->tear_sin + uy * f->tear_cos;
            if (!hull2_contains(0.0f, 0.0f, r1, 0.0f, tip, r2, lx, ly)) continue;
            row[x] = (f->tear_alpha == 255)
                         ? COLOR_FG
                         : bloub_blend_panel(row[x], 0xFFFF, f->tear_alpha,
                                             s_config.swap_color_bytes);
        }
    }
}

// --- Public API -----------------------------------------------------------

esp_err_t grok_face_init(const grok_face_config_t *config)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (config == NULL || config->width <= 0 || config->height <= 0 ||
        config->band_rows <= 0 || (config->width & 1) != 0 ||
        config->height % config->band_rows != 0 || config->radius <= 0.0f ||
        !isfinite(config->center_x) || !isfinite(config->center_y) ||
        !isfinite(config->radius) || config->acquire_buffer == NULL ||
        config->flush_band == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;

    double now_s = (double)esp_timer_get_time() * 1e-6;
    s_state = STATE_IDLE;
    s_prev_state = STATE_IDLE;
    s_has_prev = false;
    s_state_entered_s = now_s;
    s_prev_entered_s = now_s;
    s_blink_at_s = -10.0;
    s_prev_band_first = 0;
    s_prev_band_last = BAND_COUNT - 1;

    for (int band = 0; band < BAND_COUNT; band++) {
        uint16_t *buf = s_config.acquire_buffer(s_config.user_ctx);
        if (buf == NULL) {
            return ESP_ERR_NO_MEM;
        }
        memset(buf, 0, BAND_ROWS * LCD_H_RES * sizeof(uint16_t));
        esp_err_t err =
            s_config.flush_band(s_config.user_ctx, band * BAND_ROWS, BAND_ROWS, buf);
        if (err != ESP_OK) {
            return err;
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "ready: %dx%d, band_rows=%d, state=%s",
             LCD_H_RES, LCD_V_RES, BAND_ROWS, bloub_state_name(s_state));
    return ESP_OK;
}

esp_err_t grok_face_set_expression(grok_face_expression_t expression)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (expression < GROK_FACE_IDLE || expression >= GROK_FACE_EXPRESSION_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    double now_s = (double)esp_timer_get_time() * 1e-6;
    s_prev_state = s_state;
    s_prev_entered_s = s_state_entered_s;
    s_has_prev = true;
    s_state = expression;
    s_state_entered_s = now_s;
    // In the reference, every change of shape is masked by a blink.
    if (bloub_state_blink_in(s_state)) {
        s_blink_at_s = now_s;
    }
    ESP_LOGI(TAG, "-> %s", bloub_state_name(s_state));
    return ESP_OK;
}

esp_err_t grok_face_next_expression(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return grok_face_set_expression(
        (grok_face_expression_t)((s_state + 1) % STATE_COUNT));
}

grok_face_expression_t grok_face_get_expression(void)
{
    return s_state;
}

const char *grok_face_expression_name(grok_face_expression_t expression)
{
    if (expression < GROK_FACE_IDLE || expression >= GROK_FACE_EXPRESSION_COUNT) {
        return "unknown";
    }
    return bloub_state_name(expression);
}

esp_err_t grok_face_render_frame(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    double now_s = (double)esp_timer_get_time() * 1e-6;
    // Wrapped so float precision on the global (blink/drift) clock stays
    // tight even after many hours of continuous uptime; every periodic
    // function here has a period far shorter than the wrap.
    float global_t = (float)fmod(now_s, 100000.0);

    frame_t *f = &s_frame;
    prepare_frame(now_s, global_t, f);

    int band_first = f->row_lo / BAND_ROWS;
    int band_last = f->row_hi / BAND_ROWS;

    // Repaint the union with the previous frame's range, so bands the content
    // has just vacated get blanked exactly once instead of keeping stale pixels.
    int paint_first = band_first < s_prev_band_first ? band_first : s_prev_band_first;
    int paint_last = band_last > s_prev_band_last ? band_last : s_prev_band_last;
    s_prev_band_first = band_first;
    s_prev_band_last = band_last;

    for (int band = paint_first; band <= paint_last; band++) {
        int y0 = band * BAND_ROWS;
        int y1 = y0 + BAND_ROWS - 1;
        uint16_t *buf = s_config.acquire_buffer(s_config.user_ctx);
        if (buf == NULL) {
            return ESP_ERR_NO_MEM;
        }
        memset(buf, 0, BAND_ROWS * LCD_H_RES * sizeof(uint16_t));

        // 1. Back half of any orbit/swoosh/comet arcs (occluded by the body).
        if (f->any_behind) {
            for (int i = 0; i < f->stamp_count; i++) {
                if (f->stamps[i].behind) draw_stamp(buf, y0, &f->stamps[i]);
            }
        }

        // 2. Particles behind the body (burst).
        if (f->dots_behind) paint_dots(buf, y0, f);

        // 3. Body, as scanline spans.
        int row_lo = y0 > f->row_lo ? y0 : f->row_lo;
        int row_hi = y1 < f->row_hi ? y1 : f->row_hi;
        paint_body(buf, y0, f, row_lo, row_hi);

        // 4. Eye holes and the notification notch, erased back out of it.
        punch_holes(buf, y0, f);

        // 5. Front dots (thinking's three) and the alert teardrop.
        if (!f->dots_behind) paint_dots(buf, y0, f);
        if (f->has_teardrop) paint_teardrop(buf, y0, f);

        // 6. Notification pastille, on top.
        if (f->has_notif) {
            uint16_t cpu = bloub_rgb565(NOTIF_R8, NOTIF_G8, NOTIF_B8);
            draw_disc(buf, y0, f->notif_cx, f->notif_cy, f->notif_r,
                      bloub_order565(cpu, s_config.swap_color_bytes), cpu, 255);
        }

        // 7. Front half of the arcs, on top of everything.
        if (f->any_front) {
            for (int i = 0; i < f->stamp_count; i++) {
                if (!f->stamps[i].behind) draw_stamp(buf, y0, &f->stamps[i]);
            }
        }

        esp_err_t err =
            s_config.flush_band(s_config.user_ctx, y0, BAND_ROWS, buf);
        if (err != ESP_OK) {
            return err;
        }
    }

    // Frame rate, so the cost of any future change here is measurable rather
    // than guessed at.
    static uint32_t s_frames = 0;
    static double s_stats_at = 0.0;
    s_frames++;
    if (now_s - s_stats_at >= 2.0) {
        ESP_LOGI(TAG, "%.1f fps | %s | bands %d..%d | stamps %d",
                 (double)s_frames / (now_s - s_stats_at), bloub_state_name(s_state), paint_first,
                 paint_last, f->stamp_count);
        s_frames = 0;
        s_stats_at = now_s;
    }
    return ESP_OK;
}
