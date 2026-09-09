#!/usr/bin/env python3
"""Generates the two derived tables the firmware cannot afford to build at boot.

Both outputs are transliterations of `reference/bloub/src/bot/`:

  bloub_skins.c   the 8 customiser silhouettes (skins.ts) and the 12-colour
                  palette. The silhouettes are built analytically from
                  superellipses, unions of discs and rounded polygons; baking
                  them keeps those constructors out of the firmware entirely.

  bloub_eyefit.c  the eye-fit offsets (eyefit.ts). One entry per
                  (shape, base-body state, expression). Solving it on the ESP32
                  is out of the question -- roughly 500M float operations for
                  the whole table -- and eyefit.ts is emphatic that it must be a
                  table anyway: seven per-frame variants were written and every
                  one of them made the eyes tremble.

Usage:
    python3 tools/gen_bloub_tables.py                  # write both .c files
    python3 tools/gen_bloub_tables.py --check          # verify, write nothing
    python3 tools/gen_bloub_tables.py --preview p.ppm  # contact sheet

The numbers in here are measurements taken off the reference video, not
settings. Do not round them.
"""

from __future__ import annotations

import argparse
import math
import os
import sys

TAU = math.pi * 2
PROFILE_SAMPLES = 64

ANGLES = [(i / PROFILE_SAMPLES) * TAU for i in range(PROFILE_SAMPLES)]
COS = [math.cos(a) for a in ANGLES]
SIN = [math.sin(a) for a in ANGLES]


def clamp(v, lo=0.0, hi=1.0):
    return lo if v < lo else (hi if v > hi else v)


# --------------------------------------------------------------- shape.ts


def superellipse_profile(n, sx=1.0, sy=1.0):
    """|x/sx|^n + |y/sy|^n = 1. n=2 is an ellipse, n~4 the customiser squircle."""
    out = []
    for i in range(PROFILE_SAMPLES):
        c = abs(COS[i] / sx) ** n
        s = abs(SIN[i] / sy) ** n
        out.append((c + s) ** (-1.0 / n))
    return out


def union_of_circles_profile(circles):
    """r(theta) of a UNION of discs: the farthest ray/circle intersection.

    Exact as long as the origin lies inside the union, which is what gives the
    cloud its lobes without any path boolean.
    """
    out = [0.0] * PROFILE_SAMPLES
    for i in range(PROFILE_SAMPLES):
        dx, dy = COS[i], SIN[i]
        best = 0.0
        for cx, cy, cr in circles:
            b = dx * cx + dy * cy
            disc = b * b - (cx * cx + cy * cy - cr * cr)
            if disc < 0:
                continue
            t = b + math.sqrt(disc)
            if t > best:
                best = t
        out[i] = best
    return out


def profile_from_polygon(poly, cx, cy):
    """Arbitrary polygon -> radial profile, by ray casting from (cx, cy)."""
    out = [0.0] * PROFILE_SAMPLES
    n = len(poly)
    for k in range(PROFILE_SAMPLES):
        dx, dy = COS[k], SIN[k]
        best = 0.0
        for i in range(n):
            ax, ay = poly[i]
            bx, by = poly[(i + 1) % n]
            ex, ey = bx - ax, by - ay
            den = dx * ey - dy * ex
            if abs(den) < 1e-9:
                continue
            px, py = ax - cx, ay - cy
            t = (px * ey - py * ex) / den  # distance along the ray
            u = (px * dy - py * dx) / den  # position along the edge
            if t > best and 0.0 <= u <= 1.0:
                best = t
        out[k] = best
    return out


def hull_of_circles(x1, y1, r1, x2, y2, r2v, steps=96):
    """Convex hull of two discs, walked as a polygon along its common tangents."""
    dx, dy = x2 - x1, y2 - y1
    dist = math.hypot(dx, dy) or 1e-6
    base = math.atan2(dy, dx)
    spread = math.acos(max(-1.0, min(1.0, (r1 - r2v) / dist)))
    pts = []
    half = steps // 2
    for i in range(half + 1):
        a = base + spread + ((TAU - 2 * spread) * i) / half
        pts.append((x1 + math.cos(a) * r1, y1 + math.sin(a) * r1))
    for i in range(half + 1):
        a = base - spread + (2 * spread * i) / half
        pts.append((x2 + math.cos(a) * r2v, y2 + math.sin(a) * r2v))
    return pts


def rounded_polygon(verts, rc, arc_steps=10):
    """Minkowski sum of a polygon with a disc of radius `rc`.

    Every edge is pushed out by `rc` and every vertex becomes an arc, so the
    vertices must be placed at the wanted radius MINUS rc. Expects a clockwise
    polygon in screen coordinates (y down), where the outward normal is
    (dy, -dx).
    """
    n = len(verts)
    out = []

    def normal(a, b):
        dx, dy = b[0] - a[0], b[1] - a[1]
        ln = math.hypot(dx, dy) or 1.0
        return math.atan2(-dx / ln, dy / ln)

    for i in range(n):
        prev = verts[(i - 1 + n) % n]
        cur = verts[i]
        nxt = verts[(i + 1) % n]
        a0 = normal(prev, cur)
        a1 = normal(cur, nxt)
        d = a1 - a0
        while d > math.pi:
            d -= TAU
        while d < -math.pi:
            d += TAU
        for k in range(arc_steps + 1):
            a = a0 + (d * k) / arc_steps
            out.append((cur[0] + math.cos(a) * rc, cur[1] + math.sin(a) * rc))
    return out


def regular_polygon_profile(sides, radius, rc, rotation_deg=0.0):
    rot = math.radians(rotation_deg)
    verts = []
    for i in range(sides):
        a = rot + (i / sides) * TAU
        verts.append((math.cos(a) * (radius - rc), math.sin(a) * (radius - rc)))
    return profile_from_polygon(rounded_polygon(verts, rc), 0.0, 0.0)


def radius_at_angle(radii, angle):
    n = len(radii)
    t = ((((angle / TAU) % 1.0) + 1.0) % 1.0) * n
    i = int(math.floor(t))
    a = radii[i % n]
    b = radii[(i + 1) % n]
    return a + (b - a) * (t - i)


def to_points(radii, rot, cx, cy, sx, sy, scale):
    cr, sr = math.cos(rot), math.sin(rot)
    pts = []
    for i in range(PROFILE_SAMPLES):
        r = radii[i]
        x, y = r * COS[i], r * SIN[i]
        rx = x * cr - y * sr
        ry = x * sr + y * cr
        pts.append(((rx * sx + cx) * scale, (ry * sy + cy) * scale))
    return pts


# --------------------------------------------------------------- skins.ts


def normalize(radii, mx=1.0):
    """Brings the peak radius to `mx` so every shape weighs the same to the eye."""
    peak = max(radii)
    if peak <= 0:
        return radii
    k = mx / peak
    return [r * k for r in radii]


_pebble = normalize(
    [1 + 0.075 * math.cos(2 * a + 0.5) + 0.035 * math.cos(3 * a + 2.1) for a in ANGLES],
    1.02,
)

_cloud = normalize(
    union_of_circles_profile(
        [
            (-0.44, 0.2, 0.54),
            (0.46, 0.2, 0.5),
            (0.02, 0.3, 0.6),
            (-0.24, -0.3, 0.48),
            (0.3, -0.24, 0.44),
        ]
    ),
    1.02,
)

_droplet = normalize(
    profile_from_polygon(hull_of_circles(0, 0.28, 0.66, 0, -0.96, 0.05), 0, 0), 1.04
)

_capsule = profile_from_polygon(hull_of_circles(-0.42, 0, 0.62, 0.42, 0, 0.62), 0, 0)

SHAPES = [
    ("cercle", [1.0] * PROFILE_SAMPLES),
    ("galet", _pebble),
    # 1.15 and not 1.02: on a superellipse the largest radius is the diagonal,
    # so normalising on it yields a shape that reads smaller than the circle.
    ("squircle", normalize(superellipse_profile(4.2), 1.15)),
    ("capsule", _capsule),
    # -90deg: one vertex toward the top of the screen (y points down)
    ("triangle", regular_polygon_profile(3, 1.12, 0.34, -90)),
    # 0deg: vertices left and right, so the top and bottom edges are flat
    ("hexagone", regular_polygon_profile(6, 1.04, 0.26, 0)),
    ("nuage", _cloud),
    ("goutte", _droplet),
]

# Palette of the original customiser. Array order is the enum order.
COLORS = [
    ("encre", 0x0A0A0C),
    ("brun", 0x8B5E3C),
    ("rouge", 0xE8483F),
    ("orange", 0xF08A24),
    ("ambre", 0xF0B429),
    ("vert", 0x3ECF8E),
    ("turquoise", 0x2FBFA0),
    ("bleu", 0x3B93F0),
    ("violet", 0x8B5CF6),
    ("rose", 0xE152B0),
    ("gris", 0xA3A3A3),
    ("creme", 0xF1EFE9),
]


# ---------------------------------------------------------------- face.ts

EYE_SPLIT = 15.46
EYE_W = 0.186
EYE_H = 0.412
REST_GAZE = (28.49, 28.62, -13.0)


def _spin(u, v, angle):
    c, s = math.cos(angle), math.sin(angle)
    return (
        (u[0] * c + v[0] * s, u[1] * c + v[1] * s, u[2] * c + v[2] * s),
        (v[0] * c - u[0] * s, v[1] * c - u[1] * s, v[2] * c - u[2] * s),
    )


def eye_poses(gaze, scale, split):
    """Head frame then both eye frames. Index 0 = inner eye, 1 = outer eye."""
    yaw, pitch, roll = gaze
    f = (0.0, 0.0, 1.0)
    right = (1.0, 0.0, 0.0)
    down = (0.0, 1.0, 0.0)
    f, right = _spin(f, right, math.radians(yaw))
    down, f = _spin(down, f, math.radians(pitch))
    right, down = _spin(right, down, math.radians(roll))

    out = []
    for side in (-1, 1):
        ef, er = _spin(f, right, math.radians(split * side))
        out.append(
            {
                "x": ef[0] * scale,
                "y": ef[1] * scale,
                "a": er[0],
                "b": er[1],
                "c": down[0],
                "d": down[1],
                "depth": ef[2],
            }
        )
    return out


# --------------------------------------------------------- expressions.ts


def _eye(w, h, tilt=0.0, open_=1.0):
    return {"w": w, "h": h, "tilt": tilt, "open": open_}


def _pair(w, h, tilt=0.0, open_=1.0):
    """Both eyes alike, tilts mirrored when a tilt is given."""
    return [_eye(w, h, tilt, open_), _eye(w, h, -tilt, open_)]


EXPRESSIONS = [
    ("neutre", REST_GAZE, EYE_SPLIT, [_eye(EYE_W, EYE_H), _eye(EYE_W, EYE_H)]),
    ("attentif", (4, 5, -4), 16, _pair(0.21, 0.44)),
    ("surpris", (3, -3, 0), 19, _pair(0.45, 0.47)),
    ("excite", (6, -14, 0), 19.5, _pair(0.4, 0.56, -10)),
    # eyes narrowed into an arc: the tops converge slightly
    ("heureux", (5, 9, 0), 17, _pair(0.27, 0.17, 14)),
    ("hilare", (4, 14, 0), 18, _pair(0.34, 0.13, 20)),
    # tops of the eyes converging hard toward the centre + narrowed eyes
    ("colere", (3, 7, 0), 17, _pair(0.34, 0.15, 30)),
    # the reverse: the tops diverge, and the gaze falls
    ("triste", (3, -13, 0), 16, _pair(0.22, 0.4, -28)),
    ("effraye", (2, -20, 0), 20.5, _pair(0.4, 0.6)),
    # one eye distinctly more closed than the other
    ("mefiant", (12, 6, -6), 16, [_eye(0.21, 0.4), _eye(0.22, 0.15)]),
    # asymmetric on both axes: sizes AND tilts mismatched
    ("confus", (-14, 3, 8), 16.5, [_eye(0.2, 0.44, -18), _eye(0.28, 0.17, 14)]),
    # the head leans: it is the roll that carries the curiosity
    ("curieux", (16, -9, -15), 16.5, [_eye(0.24, 0.46, -8), _eye(0.2, 0.38, -8)]),
    ("fier", (5, 17, 0), 17, _pair(0.3, 0.15, 18)),
    ("timide", (-19, -14, -7), 14, _pair(0.17, 0.3)),
    # horizontal slits and the gaze off to the side
    ("blase", (-22, 2, 0), 16, _pair(0.3, 0.12)),
    # lids half dropped: this goes through `open`, the blink mechanism
    ("somnolent", (6, -9, -3), 16, _pair(0.2, 0.42, 0, 0.42)),
]


# ------------------------------------------------------------- states.ts
#
# Only the base-body states matter here: they are the only ones a customiser
# shape may replace. All five carry circle(1) and a face that does not move,
# so eyefit's `dates()` yields a single instant for each.

BASE_BODY_STATES = [
    # id, gaze, split, eyes, base_face
    ("idle", REST_GAZE, EYE_SPLIT, [_eye(EYE_W, EYE_H), _eye(EYE_W, EYE_H)], True),
    # the closed eye is a dash WIDER than the open one, not the open one squashed
    ("wink", (-5.37, 4.55, 6.7), 16.25, [_eye(0.236, 0.464), _eye(0.447, 0.089)], False),
    ("wide", (6.92, -21.96, 11.6), 18.43, _pair(0.356, 0.875), False),
    # the gaze goes opposite the pastille
    ("notify", (-21.94, -5.82, -12.2), 18.89, _pair(0.505, 0.498), False),
    ("swirl", REST_GAZE, EYE_SPLIT, [_eye(EYE_W, EYE_H), _eye(EYE_W, EYE_H)], True),
]

CIRCLE = [1.0] * PROFILE_SAMPLES


# -------------------------------------------------------------- eyefit.ts

R = 100.0

# Bounds of the resting liveliness, read off `liveliness`: loopNoise is bounded
# to 1 in absolute value, so these sums are exact bounds. They have to be
# covered, otherwise the correction is right on the nominal pose and wrong a
# second later.
DERIVE_YAW = 5.5 + 1.6
DERIVE_PITCH = 4.2 + 1.3
DERIVE_X = 0.006
DERIVE_Y = 0.007
FLOTTEMENT = math.hypot(DERIVE_X, DERIVE_Y) * R

DIRECTIONS = 12
DICHOTOMIE = 8


def empreintes(gaze, split, eyes, sil_rot, radii):
    """The two eyes of a face, laid on a profile, ready to be measured.

    An eye is exactly a segment thickened by a disc of radius r. Its image
    under the tangent matrix is therefore a segment thickened by an ELLIPSE, so
    the radius to clear depends on the direction: the ellipse's support
    function, r * |A^T u|.

    Blinking is absent on purpose: a closed eye needs no room made for it.
    """
    out = []
    poses = eye_poses(gaze, R, split)
    for i in range(2):
        e = poses[i]
        if e["depth"] <= 0.02:
            continue
        cfg = eyes[i]
        phi = math.radians(cfg["tilt"])
        cp, sp = math.cos(phi), math.sin(phi)
        ax = e["a"] * cp + e["c"] * sp
        ay = e["b"] * cp + e["d"] * sp
        cx = -e["a"] * sp + e["c"] * cp
        cy = -e["b"] * sp + e["d"] * cp

        hw = max(cfg["w"] * R, 0.01) / 2
        hh = max(cfg["h"] * R, 0.01) / 2
        r = min(hw, hh)
        long = hh > hw  # the axis is that of the larger dimension
        demi = (hh - r) if long else (hw - r)
        # the pro-rata of the local radius, exactly as the engine does it
        fit = radius_at_angle(radii, math.atan2(e["y"], e["x"]) - sil_rot)
        out.append(
            {
                "x": e["x"] * fit,
                "y": e["y"] * fit,
                "ax": (cx if long else ax) * demi,
                "ay": (cy if long else ay) * demi,
                "r": r,
                "m": (ax, ay, cx, cy),
            }
        )
    return out


def approche(pts, x0, y0, x1, y1):
    """Closest approach between a contour and a segment.

    Returns the distance and the unit vector pointing FROM the contour TO the
    segment -- the direction that clears it. Both come out of the same sweep:
    computing them separately doubled this module's only real cost.
    """
    sx, sy = x1 - x0, y1 - y0
    len2 = sx * sx + sy * sy
    best = float("inf")
    vx = vy = 0.0
    for px, py in pts:
        t = ((px - x0) * sx + (py - y0) * sy) / len2 if len2 > 0 else 0.0
        t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
        ex = x0 + t * sx - px
        ey = y0 + t * sy - py
        d2 = ex * ex + ey * ey
        if d2 < best:
            best = d2
            vx, vy = ex, ey
    d = math.sqrt(best)
    if d > 1e-9:
        return d, vx / d, vy / d
    return d, 0.0, 0.0


def pire(pts, emps, tx, ty):
    """Margin of the tightest eye, and the direction that clears it."""
    marge = float("inf")
    ux = uy = 0.0
    for e in emps:
        x = e["x"] + tx
        y = e["y"] + ty
        d, aux, auy = approche(pts, x - e["ax"], y - e["ay"], x + e["ax"], y + e["ay"])
        # support function of the ellipse in the direction of the approach
        m0, m1, m2, m3 = e["m"]
        rayon = e["r"] * math.hypot(m0 * aux + m1 * auy, m2 * aux + m3 * auy) + FLOTTEMENT
        if d - rayon < marge:
            marge = d - rayon
            ux, uy = aux, auy
    return marge, ux, uy


def resous(epreuves):
    """The offset to apply to both eyes for one (shape, state, expression).

    A TRANSLATION common to both eyes, hence an isometry: spacing, sizes and
    tilts are preserved to the pixel. The face simply sits a little lower on a
    body that has no room up there.

    DIRECTIONAL SEARCH, not a descent. We want the translation of smallest norm
    that fits, so we probe a ring of directions and bisect the distance along
    each. A gradient descent was written first and does not converge: clearing
    the pair from one edge brings it closer to another.
    """
    if not epreuves:
        return 0.0, 0.0

    def marge(tx, ty):
        m = float("inf")
        for ep in epreuves:
            m = min(m, pire(ep["contour"], ep["empreintes"], tx, ty)[0])
        return m

    # Required margin: the tightest the ORIGINAL profile tolerates. The target
    # is not a strict clearance -- on the circle the outer eye already grazes
    # the edge, and that is what gives the volume.
    requis = float("inf")
    for ep in epreuves:
        requis = min(requis, pire(ep["calContour"], ep["reference"], 0.0, 0.0)[0])

    # The travel must be able to reach the body's centre: `wide` has eyes 87
    # units long, and on a triangle they only fit around the middle.
    mx = my = 0.0
    emps = epreuves[0]["empreintes"]
    if emps:
        for e in emps:
            mx -= e["x"] / len(emps)
            my -= e["y"] / len(emps)
    course = max(0.35 * R, math.hypot(mx, my) * 1.25)

    # Ceiling on the demand: what the shape offers at its centre, always reachable.
    requis = min(requis, marge(mx, my))

    # Already good: the circle, and any shape wide enough. The eye must FIT as
    # well as being no tighter than on the original profile -- without that
    # second condition a shape where nothing fits satisfies the first
    # degenerately and we would give up.
    depart = marge(0.0, 0.0)
    if depart >= requis and depart >= 0:
        return 0.0, 0.0
    cible = max(requis, 0.0)

    meilleur_x = meilleur_y = 0.0
    meilleure_norme = float("inf")
    # fallback when nothing fits: the translation that clears the most
    secours_x = secours_y = 0.0
    secours = depart

    for d in range(DIRECTIONS):
        a = (d / DIRECTIONS) * TAU
        ux, uy = math.cos(a), math.sin(a)
        if marge(ux * course, uy * course) < cible:
            # no solution that way, but perhaps a better clearance
            for k in (0.3, 0.6, 1.0):
                m = marge(ux * course * k, uy * course * k)
                if m > secours:
                    secours = m
                    secours_x = ux * course * k
                    secours_y = uy * course * k
            continue
        # shortest distance that fits, along this direction
        bas, haut = 0.0, course
        for _ in range(DICHOTOMIE):
            mid = (bas + haut) / 2
            if marge(ux * mid, uy * mid) >= cible:
                haut = mid
            else:
                bas = mid
        if haut < meilleure_norme:
            meilleure_norme = haut
            meilleur_x = ux * haut
            meilleur_y = uy * haut

    x = secours_x if meilleure_norme == float("inf") else meilleur_x
    y = secours_y if meilleure_norme == float("inf") else meilleur_y
    # returned in BALL-RADIUS units: the engine puts it back to scale
    return round(x / R, 6), round(y / R, 6)


def face_of(state, expr):
    """The face to cover: the expression's if the state takes one, its own otherwise."""
    _id, gaze, split, eyes, base_face = state
    if base_face and expr is not None:
        return expr[1], expr[2], expr[3]
    return gaze, split, eyes


def decalage_pour(state, radii, expr):
    """One entry: offset for this state and expression on this shape, drift included."""
    gaze, split, eyes = face_of(state, expr)
    contour = to_points(radii, 0.0, 0.0, 0.0, 1.0, 1.0, R)
    cal_contour = to_points(CIRCLE, 0.0, 0.0, 0.0, 1.0, 1.0, R)
    epreuves = []
    # The four corners of the drift bound the nominal pose, which is their
    # centre: testing it as well would change no margin and costs one trial in five.
    for dy in (-DERIVE_YAW, DERIVE_YAW):
        for dp in (-DERIVE_PITCH, DERIVE_PITCH):
            g = (gaze[0] + dy, gaze[1] + dp, gaze[2])
            epreuves.append(
                {
                    "empreintes": empreintes(g, split, eyes, 0.0, radii),
                    "reference": empreintes(g, split, eyes, 0.0, CIRCLE),
                    "contour": contour,
                    "calContour": cal_contour,
                }
            )
    return resous(epreuves)


def build_table(verbose=False):
    """One entry per (shape, base-body state, expression), as eyefit's `batir`.

    Only the base-face states decline by expression; the others carry a face
    measured off the video and get a single entry.
    """
    table = {}
    for si, (shape_name, radii) in enumerate(SHAPES):
        for state in BASE_BODY_STATES:
            exprs = [None] + EXPRESSIONS if state[4] else [None]
            for expr in exprs:
                key = (si, state[0], expr[0] if expr else None)
                table[key] = decalage_pour(state, radii, expr)
            if verbose:
                print(f"  {shape_name}/{state[0]}", file=sys.stderr)
    return table


# --------------------------------------------------------------- emitters

BANNER = """// Generated by tools/gen_bloub_tables.py -- do not edit by hand.
// Regenerate with: python3 tools/gen_bloub_tables.py
//
// Source of truth: reference/bloub/src/bot/{src}.
"""


def fmt_floats(values, per_line=6, indent="    "):
    lines = []
    for i in range(0, len(values), per_line):
        chunk = ", ".join(f"{v:+.6f}f" for v in values[i : i + per_line])
        lines.append(indent + chunk + ",")
    return "\n".join(lines)


def rgb565(hexv):
    r, g, b = (hexv >> 16) & 0xFF, (hexv >> 8) & 0xFF, hexv & 0xFF
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def emit_skins():
    out = [BANNER.format(src="skins.ts"), '#include "bloub_skins.h"', ""]
    out.append("const float BLOUB_SHAPE_RADII[GROK_SHAPE_COUNT][PROFILE_SAMPLES] = {")
    for name, radii in SHAPES:
        out.append(f"    // {name}")
        out.append("    {")
        out.append(fmt_floats(radii, indent="        "))
        out.append("    },")
    out.append("};")
    out.append("")
    out.append("const char *const BLOUB_SHAPE_NAMES[GROK_SHAPE_COUNT] = {")
    out.append("    " + ", ".join(f'"{n}"' for n, _ in SHAPES))
    out.append("};")
    out.append("")
    out.append("const bloub_color_t BLOUB_COLORS[GROK_COLOR_COUNT] = {")
    for name, hexv in COLORS:
        r, g, b = (hexv >> 16) & 0xFF, (hexv >> 8) & 0xFF, hexv & 0xFF
        out.append(
            f"    {{0x{rgb565(hexv):04X}, {r:3d}, {g:3d}, {b:3d}}}, // {name} #{hexv:06x}"
        )
    out.append("};")
    out.append("")
    out.append("const char *const BLOUB_COLOR_NAMES[GROK_COLOR_COUNT] = {")
    out.append("    " + ", ".join(f'"{n}"' for n, _ in COLORS))
    out.append("};")
    out.append("")
    return "\n".join(out)


def emit_eyefit(table):
    """One row per shape, plus the lookup that turns a state and an expression
    into a column.

    The lookup is generated with the table because it is the column order: a
    hand-written one would silently drift the day a base-body state is added.
    """
    order = []
    columns = {}
    for state in BASE_BODY_STATES:
        columns[state[0]] = (len(order), state[4])
        exprs = [None] + [e[0] for e in EXPRESSIONS] if state[4] else [None]
        for e in exprs:
            order.append((state[0], e))

    out = [BANNER.format(src="eyefit.ts"), '#include "bloub_eyefit.h"', ""]
    out.append(f"// {len(order)} columns per shape, in this order:")
    for i, (s, e) in enumerate(order):
        out.append(f"//   {i:2d} {s}" + (f" / {e}" if e else ""))
    out.append("")
    out.append(
        "const float BLOUB_EYEFIT[GROK_SHAPE_COUNT][BLOUB_EYEFIT_COLUMNS][2] = {"
    )
    for si, (shape_name, _) in enumerate(SHAPES):
        out.append(f"    // {shape_name}")
        out.append("    {")
        for state_id, expr_id in order:
            x, y = table[(si, state_id, expr_id)]
            label = state_id + (f"/{expr_id}" if expr_id else "")
            out.append(f"        {{{x:+.6f}f, {y:+.6f}f}}, // {label}")
        out.append("    },")
    out.append("};")
    out.append("")

    out.append("bloub_vec2_t bloub_eyefit(int shape, bloub_state_id_t state, int expr)")
    out.append("{")
    out.append("    const bloub_vec2_t nul = {0.0f, 0.0f};")
    out.append("    if (shape < 0 || shape >= GROK_SHAPE_COUNT) return nul;")
    out.append("")
    out.append("    int column;")
    out.append("    switch (state) {")
    for state_id, (base, base_face) in columns.items():
        enum = "STATE_" + state_id.upper()
        out.append(f"    case {enum}:")
        if base_face:
            out.append(f"        // the resting face, so one column per expression")
            out.append(
                f"        column = (expr >= 0 && expr < BLOUB_EYEFIT_EXPRESSIONS)"
            )
            out.append(f"                     ? {base} + 1 + expr")
            out.append(f"                     : {base};")
        else:
            out.append("        // a face measured off the video: one column, whatever the expression")
            out.append(f"        column = {base};")
        out.append("        break;")
    out.append("    default:")
    out.append("        // not a base-body state: the silhouette IS the animation")
    out.append("        return nul;")
    out.append("    }")
    out.append("")
    out.append("    bloub_vec2_t v = {BLOUB_EYEFIT[shape][column][0], BLOUB_EYEFIT[shape][column][1]};")
    out.append("    return v;")
    out.append("}")
    out.append("")
    return "\n".join(out), order


def emit_eyefit_header(order):
    out = [
        BANNER.format(src="eyefit.ts"),
        "#pragma once",
        "",
        '#include "bloub_skins.h"',
        '#include "bloub_states.h"',
        "",
        "// Eye-fit offsets, in ball-radius units.",
        "//",
        "// The eyes live on a sphere and `profile_radius_at` sticks them back onto the",
        "// real outline pro rata of the local radius. That pro-rata places their CENTRE",
        "// correctly, but an eye has a size, and the margin it has left in front of the",
        "// edge is multiplied by the same factor -- so a silhouette that is narrow in the",
        "// eye's direction pushes it through the edge. This table holds the common",
        "// translation to apply to BOTH eyes, which is an isometry: spacing, sizes and",
        "// tilts survive to the pixel.",
        "//",
        "// It is a table and not a solver, and that distinction IS the fix. Solved in the",
        "// render loop it reacts to everything that moves at sixty frames a second, and",
        "// every one of the seven variants written that way trembled. See eyefit.ts.",
        "",
        f"#define BLOUB_EYEFIT_COLUMNS {len(order)}",
        f"#define BLOUB_EYEFIT_EXPRESSIONS {len(EXPRESSIONS)}",
        "",
        "extern const float BLOUB_EYEFIT[GROK_SHAPE_COUNT][BLOUB_EYEFIT_COLUMNS][2];",
        "",
        "typedef struct {",
        "    float x, y;",
        "} bloub_vec2_t;",
        "",
        "/**",
        " * Offset for this shape on this state and expression.",
        " *",
        " * Zero as soon as the shape is not one of the customiser's (which covers",
        " * GROK_FACE_SHAPE_NONE and the circle, whose two profiles are the same), and zero",
        " * on any state that does not carry the resting body. A state without the resting",
        " * FACE has a single entry, whatever the expression.",
        " */",
        "bloub_vec2_t bloub_eyefit(int shape, bloub_state_id_t state, int expr);",
        "",
    ]
    return "\n".join(out)

# ----------------------------------------------------------------- checking


def worst_margin(radii, gaze, split, eyes, offset, samples=5):
    """Tightest margin over a sweep of the resting drift, offset applied.

    Solving used the four corners of the drift box; checking sweeps its
    interior too. Sweeping is what caught `capsule` + `effraye` in the
    reference, where a measurement at a single instant declared it good.
    """
    contour = to_points(radii, 0.0, 0.0, 0.0, 1.0, 1.0, R)
    worst = float("inf")
    for i in range(samples):
        dy = -DERIVE_YAW + (2 * DERIVE_YAW * i) / (samples - 1)
        for j in range(samples):
            dp = -DERIVE_PITCH + (2 * DERIVE_PITCH * j) / (samples - 1)
            g = (gaze[0] + dy, gaze[1] + dp, gaze[2])
            emps = empreintes(g, split, eyes, 0.0, radii)
            m = pire(contour, emps, offset[0] * R, offset[1] * R)[0]
            worst = min(worst, m)
    return worst


def run_check(table):
    """Port of skins.test.ts: no eye may leave its silhouette.

    Where the reference itself cannot fit -- `wide`'s 87-unit eyes on a
    triangle -- the requirement is that the offset still IMPROVES on doing
    nothing, which is what eyefit's fallback branch aims for.
    """
    failures = []
    improved = []
    worst_overall = ("", float("inf"))

    for si, (shape_name, radii) in enumerate(SHAPES):
        for state in BASE_BODY_STATES:
            exprs = [None] + EXPRESSIONS if state[4] else [None]
            for expr in exprs:
                expr_id = expr[0] if expr else None
                gaze, split, eyes = face_of(state, expr)
                offset = table[(si, state[0], expr_id)]
                label = f"{shape_name}/{state[0]}" + (f"/{expr_id}" if expr_id else "")

                after = worst_margin(radii, gaze, split, eyes, offset)
                if after < worst_overall[1]:
                    worst_overall = (label, after)
                if after >= 0.0:
                    continue
                before = worst_margin(radii, gaze, split, eyes, (0.0, 0.0))
                if after > before + 1e-6:
                    improved.append((label, before, after))
                else:
                    failures.append((label, before, after))

    total = sum(
        len(EXPRESSIONS) + 1 if s[4] else 1 for s in BASE_BODY_STATES
    ) * len(SHAPES)
    print(f"checked {total} (shape, state, expression) combinations")
    print(f"tightest margin overall: {worst_overall[1]:+.2f} units on {worst_overall[0]}")

    if improved:
        print(f"\n{len(improved)} combinations cannot fit at all; the offset still helps:")
        for label, before, after in sorted(improved, key=lambda r: r[2]):
            print(f"  {label:40s} {before:+7.2f} -> {after:+7.2f}")

    if failures:
        print(f"\nFAIL: {len(failures)} combinations overflow and are not improved:")
        for label, before, after in sorted(failures, key=lambda r: r[2]):
            print(f"  {label:40s} {before:+7.2f} -> {after:+7.2f}")
        return 1

    print("\nOK: every eye is inside its silhouette, or as close as the shape allows.")
    return 0


# ----------------------------------------------------------------- preview


def render_cell(radii, gaze, split, eyes, offset, size):
    """One cell of the contact sheet: filled silhouette with the eyes punched out.

    Deliberately naive -- a per-pixel test, no scanline pass. This is a
    proofing tool, not the firmware's rasteriser.
    """
    scale = size * 0.42
    cx = cy = size / 2.0
    px = [[0] * size for _ in range(size)]

    # eye frames, exactly as engine.ts composes them
    frames = []
    for i, e in enumerate(eye_poses(gaze, scale, split)):
        if e["depth"] <= 0.02:
            continue
        cfg = eyes[i]
        phi = math.radians(cfg["tilt"])
        cp, sp = math.cos(phi), math.sin(phi)
        ax = e["a"] * cp + e["c"] * sp
        ay = e["b"] * cp + e["d"] * sp
        c2 = -e["a"] * sp + e["c"] * cp
        d2 = -e["b"] * sp + e["d"] * cp
        k = 0.06 + 0.94 * clamp(cfg["open"])
        ay, d2 = ay * k, d2 * k
        fit = radius_at_angle(radii, math.atan2(e["y"], e["x"]))
        tx = e["x"] * fit + offset[0] * scale + cx
        ty = e["y"] * fit + offset[1] * scale + cy
        det = ax * d2 - c2 * ay
        if abs(det) < 1e-6:
            continue
        hw = max(cfg["w"] * scale, 0.01) / 2
        hh = max(cfg["h"] * scale, 0.01) / 2
        r = min(hw, hh)
        frames.append((d2 / det, -c2 / det, -ay / det, ax / det, tx, ty, hw, hh, r))

    for y in range(size):
        for x in range(size):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            rad = math.hypot(dx, dy)
            if rad > radius_at_angle(radii, math.atan2(dy, dx)) * scale:
                continue
            v = 255
            for ia, ib, ic, idd, tx, ty, hw, hh, r in frames:
                sx, sy = x + 0.5 - tx, y + 0.5 - ty
                lx = ia * sx + ib * sy
                ly = ic * sx + idd * sy
                # stadium: distance to its axis segment
                if hh >= hw:
                    qy = min(max(ly, -(hh - r)), hh - r)
                    if lx * lx + (ly - qy) ** 2 <= r * r:
                        v = 0
                        break
                else:
                    qx = min(max(lx, -(hw - r)), hw - r)
                    if (lx - qx) ** 2 + ly * ly <= r * r:
                        v = 0
                        break
            px[y][x] = v
    return px


def run_preview(table, path, state_id="idle"):
    state = next(s for s in BASE_BODY_STATES if s[0] == state_id)
    cell = 72
    cols, rows = len(SHAPES), len(EXPRESSIONS) if state[4] else 1
    w, h = cols * cell, rows * cell
    img = [[40] * w for _ in range(h)]

    for si, (_shape_name, radii) in enumerate(SHAPES):
        exprs = EXPRESSIONS if state[4] else [None]
        for ei, expr in enumerate(exprs):
            expr_id = expr[0] if expr else None
            gaze, split, eyes = face_of(state, expr)
            offset = table[(si, state_id, expr_id)]
            block = render_cell(radii, gaze, split, eyes, offset, cell)
            for y in range(cell):
                row = img[ei * cell + y]
                for x in range(cell):
                    row[si * cell + x] = block[y][x]

    write_gray_png(path, img, w, h)
    print(f"wrote {path} ({w}x{h}) -- columns are shapes, rows are expressions")


def write_gray_png(path, rows, w, h):
    """8-bit greyscale PNG, hand-rolled: stdlib zlib is all it takes."""
    import struct
    import zlib

    raw = b"".join(b"\x00" + bytes(r) for r in rows)  # filter 0 per scanline

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


# -------------------------------------------------------------------- main


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="verify the table, write nothing")
    ap.add_argument("--preview", metavar="PATH", help="write a contact sheet (PGM)")
    ap.add_argument(
        "--out",
        default=os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            "..",
            "components",
            "grok_face",
            "src",
        ),
        help="directory to write the generated .c/.h files into",
    )
    args = ap.parse_args()

    print("solving the eye-fit table...", file=sys.stderr)
    table = build_table(verbose=args.check)

    if args.check:
        rc = run_check(table)
        if args.preview:
            run_preview(table, args.preview)
        return rc

    if args.preview:
        run_preview(table, args.preview)
        return 0

    out = os.path.normpath(args.out)
    body, order = emit_eyefit(table)
    for name, text in (
        ("bloub_skins.c", emit_skins()),
        ("bloub_eyefit.c", body),
        ("bloub_eyefit.h", emit_eyefit_header(order)),
    ):
        path = os.path.join(out, name)
        with open(path, "w") as f:
            f.write(text)
        print(f"wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
