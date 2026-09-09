#include "grok_face_render.h"

#include <math.h>
#include <string.h>

#include "bloub_math.h"

// Anything dimmer than this is not worth a draw call.
#define MIN_VISIBLE 0.02f

// The notification pastille's own colour, measured off the reference video.
// It belongs to the pastille, not to the palette: it is a status light, and it
// has to stay legible whatever the body is painted.
#define NOTIF_R8 46
#define NOTIF_G8 232
#define NOTIF_B8 106

static render_config_t s_cfg;
static uint16_t s_body_panel = 0xFFFF; // transport order
static uint16_t s_body_cpu = 0xFFFF;   // CPU order, for alpha blending
static uint16_t s_bg_panel;
static uint16_t s_bg_cpu;

#define LCD_H_RES (s_cfg.width)
#define LCD_V_RES (s_cfg.height)
#define BAND_ROWS (s_cfg.band_rows)
#define BALL_CX (s_cfg.center_x)
#define BALL_CY (s_cfg.center_y)
#define BALL_R (s_cfg.radius)

void render_init(const render_config_t *config)
{
    s_cfg = *config;
    s_bg_cpu = config->background;
    s_bg_panel = bloub_order565(s_bg_cpu, s_cfg.swap_color_bytes);
    render_set_body_color(0xFFFF);
}

void render_set_body_color(uint16_t rgb565_cpu)
{
    s_body_cpu = rgb565_cpu;
    s_body_panel = bloub_order565(rgb565_cpu, s_cfg.swap_color_bytes);
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

/** Flat horizontal run, written 32 bits at a time. Rows are 4-byte aligned. */
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

void render_clear_band(uint16_t *buf)
{
    size_t px = (size_t)BAND_ROWS * (size_t)LCD_H_RES;
    if (s_bg_panel == 0) {
        memset(buf, 0, px * sizeof(uint16_t));
        return;
    }
    fill_span(buf, 0, (int)px - 1, s_bg_panel);
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
                                               s_cfg.swap_color_bytes);
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
                row[x] = bloub_blend_panel(row[x], color_cpu, alpha, s_cfg.swap_color_bytes);
            }
        }
    }
}

// --- Frame resolution, done once per frame (not per band) ---------------

/**
 * Prepares one eye.
 *
 * The stadium is drawn through the sphere's tangent frame, composed with the
 * eye's OWN lean and then with the blink squash, and pinned to the silhouette's
 * real radius in its direction. Mirrored leans are what anger and sadness are
 * made of, and they are only reachable per eye: head roll leans both the same
 * way.
 *
 * The lean multiplies the tangent frame (Basis x Rot); the blink comes AFTER
 * all of it, because it is a vertical squash in SCREEN space, not a squash
 * along the capsule's own axis.
 */
static void prepare_eye(const eye_pose_t *ep, const eye_cfg_t *cfg, float fit, float off_x_px,
                        float off_y_px, float lid, eye_render_t *out)
{
    float phi = cfg->tilt * (float)M_PI / 180.0f;
    float cp = cosf(phi), sp = sinf(phi);
    float ax = ep->a * cp + ep->c * sp;
    float ay = ep->b * cp + ep->d * sp;
    float cx2 = -ep->a * sp + ep->c * cp;
    float cy2 = -ep->b * sp + ep->d * cp;

    float k = blink_scale(fminf(lid, cfg->open));
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

static void grow_rows(render_frame_t *f, float lo, float hi)
{
    if (lo < f->row_lo) f->row_lo = (int)floorf(lo);
    if (hi > f->row_hi) f->row_hi = (int)ceilf(hi);
}

void render_prepare(const engine_frame_t *frame, render_frame_t *f)
{
    const pose_t *p = &frame->pose;

    f->eye_alpha = (uint8_t)(bloub_clamp01(p->eye_alpha) * 255.0f + 0.5f);
    float off_x_px = frame->off_x * BALL_R;
    float off_y_px = frame->off_y * BALL_R;

    // --- body -------------------------------------------------------------
    // The silhouette already carries the drift and the breath: the engine
    // folded them in.
    body_poly_build(&f->poly, &p->sil, BALL_R, BALL_CX, BALL_CY);
    f->row_lo = (int)floorf(f->poly.y_min);
    f->row_hi = (int)ceilf(f->poly.y_max);

    // --- eyes -------------------------------------------------------------
    f->eyes[0].active = false;
    f->eyes[1].active = false;
    if (p->eye_alpha > MIN_VISIBLE) {
        eye_pose_t eps[2];
        eye_poses(frame->gaze, BALL_R, p->split, eps);
        // The eye-fit offset moves the PAIR and nothing else, so their spacing,
        // sizes and leans come through untouched.
        float eye_x_px = off_x_px + frame->eye_dx * BALL_R;
        float eye_y_px = off_y_px + frame->eye_dy * BALL_R;
        for (int i = 0; i < 2; i++) {
            if (eps[i].depth <= 0.02f) continue;
            // The eyes live on a sphere; as soon as the silhouette stops being
            // a circle they are brought back pro rata of its real radius in
            // their direction, or they leave the body and get clipped.
            float angle = atan2f(eps[i].y, eps[i].x) - p->sil.rot;
            if (angle < 0.0f) angle += BLOUB_TAU;
            float fit = profile_radius_at(p->sil.radii, angle);
            prepare_eye(&eps[i], &p->eyes[i], fit, eye_x_px, eye_y_px, frame->lid, &f->eyes[i]);
        }
    }
    // Eyes need no row growth: they are holes punched out of the body, so
    // they can never paint outside it.

    // --- dots -------------------------------------------------------------
    f->dot_count = 0;
    f->dots_behind = p->dots_behind;
    for (int i = 0; i < p->dot_count; i++) {
        const dot_render_t *d = &p->dots[i];
        // `depth` is the burst particles' haze -- in the source it mixes the
        // dot toward the page colour as it recedes, which is the same thing
        // as fading it out, so the two multiply into one alpha.
        float bright = d->opacity * (d->has_depth ? d->depth : 1.0f);
        if (bright <= MIN_VISIBLE || d->r <= 0.0005f) continue;
        float cx = d->x * BALL_R + off_x_px + BALL_CX;
        float cy = d->y * BALL_R + off_y_px + BALL_CY;
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
        // The pastille sits on the outline, so it follows the shape too.
        float angle = atan2f(p->notif_y, p->notif_x) - p->sil.rot;
        if (angle < 0.0f) angle += BLOUB_TAU;
        float fit = profile_radius_at(p->sil.radii, angle);
        f->notif_cx = (p->notif_x * fit) * BALL_R + off_x_px + BALL_CX;
        f->notif_cy = (p->notif_y * fit) * BALL_R + off_y_px + BALL_CY;
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
        f->tear_cx = p->tear_x * BALL_R + off_x_px + BALL_CX;
        f->tear_cy = p->tear_y * BALL_R + off_y_px + BALL_CY;
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
                                    p->arc_opacity[a], s_cfg.swap_color_bytes, tmp);
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

static void paint_body(uint16_t *buf, int band_y0, const render_frame_t *f, int row_lo, int row_hi)
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
            fill_span(row, xa, xb, s_body_panel);
        }
    }
}

/**
 * Erases the eye holes and the notification notch.
 *
 * The eyes are HOLES in the body, not shapes laid on top, and that is what
 * clips them against the silhouette for free: only pixels inside a body span
 * are erased. It also protects the back half of the rings, drawn under the body
 * and still visible where the body isn't -- matching the source, where the body
 * is backed by an opaque page-coloured path precisely so a ring passing behind
 * the ball does not reappear inside the eyes.
 *
 * The body's own scanline crossings are recomputed here rather than being
 * remembered. Sixty-four edge tests on the handful of rows an eye spans is
 * cheaper than carrying a coverage buffer, and unlike the colour test it used
 * to do ("is this pixel body-coloured?") it stays correct once the body can be
 * any colour in the palette.
 */
static void punch_holes(uint16_t *buf, int band_y0, const render_frame_t *f)
{
    float xs[BODY_MAX_CROSSINGS];

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
            float fy = (float)y + 0.5f;
            if (fy < f->poly.y_min || fy >= f->poly.y_max) continue;
            int n = body_poly_crossings(&f->poly, fy, xs);
            if (n < 2) continue;

            uint16_t *row = buf + (y - band_y0) * LCD_H_RES;
            float sy = (float)y - eye->ty;
            float lx_base = -eye->B * sy;
            float ly_base = eye->E * sy;

            for (int i = 0; i + 1 < n; i += 2) {
                int xa = (int)ceilf(xs[i] - 0.5f);
                int xb = (int)ceilf(xs[i + 1] - 0.5f) - 1;
                if (xa < min_x) xa = min_x;
                if (xb > max_x) xb = max_x;
                for (int x = xa; x <= xb; x++) {
                    float sx = (float)x - eye->tx;
                    float lx = eye->A * sx + lx_base;
                    float ly = ly_base - eye->C * sx;
                    if (!capsule_contains(&eye->local, lx, ly)) continue;
                    row[x] = (f->eye_alpha == 255)
                                 ? s_bg_panel
                                 : bloub_blend_panel(row[x], s_bg_cpu, f->eye_alpha,
                                                     s_cfg.swap_color_bytes);
                }
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
            float fy = (float)y + 0.5f;
            if (fy < f->poly.y_min || fy >= f->poly.y_max) continue;
            int n = body_poly_crossings(&f->poly, fy, xs);
            if (n < 2) continue;
            uint16_t *row = buf + (y - band_y0) * LCD_H_RES;
            float dy = (float)y - f->notif_cy;
            for (int i = 0; i + 1 < n; i += 2) {
                int xa = (int)ceilf(xs[i] - 0.5f);
                int xb = (int)ceilf(xs[i + 1] - 0.5f) - 1;
                if (xa < min_x) xa = min_x;
                if (xb > max_x) xb = max_x;
                for (int x = xa; x <= xb; x++) {
                    float dx = (float)x - f->notif_cx;
                    if (dx * dx + dy * dy <= r2) row[x] = s_bg_panel;
                }
            }
        }
    }
}

static void paint_dots(uint16_t *buf, int band_y0, const render_frame_t *f)
{
    for (int i = 0; i < f->dot_count; i++) {
        draw_disc(buf, band_y0, f->dots[i].cx, f->dots[i].cy, f->dots[i].r, s_body_panel,
                  s_body_cpu, f->dots[i].alpha);
    }
}

static void paint_teardrop(uint16_t *buf, int band_y0, const render_frame_t *f)
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
                         ? s_body_panel
                         : bloub_blend_panel(row[x], s_body_cpu, f->tear_alpha,
                                             s_cfg.swap_color_bytes);
        }
    }
}

void render_band(uint16_t *buf, int band_y0, const render_frame_t *f)
{
    int y1 = band_y0 + BAND_ROWS - 1;

    // 1. Back half of any orbit/swoosh/comet arcs (occluded by the body).
    if (f->any_behind) {
        for (int i = 0; i < f->stamp_count; i++) {
            if (f->stamps[i].behind) draw_stamp(buf, band_y0, &f->stamps[i]);
        }
    }

    // 2. Particles behind the body (burst).
    if (f->dots_behind) paint_dots(buf, band_y0, f);

    // 3. Body, as scanline spans.
    int row_lo = band_y0 > f->row_lo ? band_y0 : f->row_lo;
    int row_hi = y1 < f->row_hi ? y1 : f->row_hi;
    paint_body(buf, band_y0, f, row_lo, row_hi);

    // 4. Eye holes and the notification notch, erased back out of it.
    punch_holes(buf, band_y0, f);

    // 5. Front dots (thinking's three, exclaim's one) and the alert teardrop.
    if (!f->dots_behind) paint_dots(buf, band_y0, f);
    if (f->has_teardrop) paint_teardrop(buf, band_y0, f);

    // 6. Notification pastille, on top.
    if (f->has_notif) {
        uint16_t cpu = bloub_rgb565(NOTIF_R8, NOTIF_G8, NOTIF_B8);
        draw_disc(buf, band_y0, f->notif_cx, f->notif_cy, f->notif_r,
                  bloub_order565(cpu, s_cfg.swap_color_bytes), cpu, 255);
    }

    // 7. Front half of the arcs, on top of everything.
    if (f->any_front) {
        for (int i = 0; i < f->stamp_count; i++) {
            if (!f->stamps[i].behind) draw_stamp(buf, band_y0, &f->stamps[i]);
        }
    }
}
