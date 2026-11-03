#!/usr/bin/env python3
"""Writes panel.svg.fragment (Acid Rain's artwork) and display_tables.h (the
same geometry for display.cpp), so the static grid and the moving one meet.
Run it after changing a number here, and commit all three files:

    python3 tools/make_panel.py

The floor is a plane seen from above at a slant: floor units (u, v) map to the
screen as x = 240 + S u, y = Y0 + S K v. Ripples are circles on the floor.
"""
import math
import re
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
S, K, Y0 = 160.0, 0.42, 116.0     # pixels per floor unit, slant, the face's centre
LATTICE = 0.344                  # floor spacing between parallel grid lines
# Build constants for the face, tried out with the tuner (acid-rain-work/tuner):
OUTLINE, MOUTH, EYES = 3.5, 2.0, 1.5   # line weights on screen, px
ROTATION = -45.0                       # the face turned on the floor, degrees clockwise from above
TOP, BOTTOM = 40.0, 220.0        # the grid's band, below the two-page header
CENTRE = 5                       # moving joints: |m + n| <= CENTRE and |m - n| <= CENTRE


def screen(u, v):
    return 240 + S * u, Y0 + S * K * v


def warp(x, y):
    """The floor at rest: flat, so the grid's lines are straight until a ripple passes."""
    return 0.0, 0.0
    return (2.6 * math.sin(0.037 * x + 0.10 * y + 0.7) + 1.4 * math.sin(0.083 * y - 0.029 * x + 2.1),
            2.2 * math.sin(0.046 * x - 0.055 * y + 1.9) + 1.2 * math.sin(0.11 * x + 0.4 + 0.03 * y))


def joint(m, n):
    u, v = LATTICE * (m + n) / 2, LATTICE * (m - n) / 2
    x, y = screen(u, v)
    wx, wy = warp(x, y)
    return x + wx, y + wy, u, v


def fmt(v):
    s = f"{v:.3f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def ribbon(points, half):
    """A thin closed outline around a polyline: out along one edge, back along the
    other, `half` pixels either side, starting mid-edge where both edges are long."""
    out, back = [], []
    for i, (x, y) in enumerate(points):
        a = points[max(i - 1, 0)]
        b = points[min(i + 1, len(points) - 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy) or 1
        nx, ny = -dy / length * half, dx / length * half
        out.append((x + nx, y + ny))
        back.append((x - nx, y - ny))
    return out + back[::-1]


def path_d(points):
    return "M" + " L".join(f"{fmt(x)},{fmt(y)}" for x, y in points) + " Z"


def clip(a, b):
    """The part of segment a-b inside the grid's band and the screen, or None."""
    t0, t1 = 0.0, 1.0
    for p, q in ((a[1] - TOP, b[1] - TOP), (BOTTOM - a[1], BOTTOM - b[1]), (a[0] + 2, b[0] + 2), (482 - a[0], 482 - b[0])):
        if p < 0 and q < 0:
            return None
        if p < 0 or q < 0:
            t = p / (p - q)
            if p < 0:
                t0 = max(t0, t)
            else:
                t1 = min(t1, t)
    if t0 >= t1:
        return None
    lerp = lambda t: (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
    return lerp(t0), lerp(t1)


# --- The grid ------------------------------------------------------------------
R = 11
joints, index = [], {}
for m in range(-CENTRE, CENTRE + 1):
    for n in range(-CENTRE, CENTRE + 1):
        if abs(m + n) <= CENTRE and abs(m - n) <= CENTRE:
            index[(m, n)] = len(joints)
            joints.append((m, n))
moving = lambda m, n: (m, n) in index
segments = []
for (m, n) in joints:
    for dm, dn in ((1, 0), (0, 1)):
        if moving(m + dm, n + dn):
            segments.append((index[(m, n)], index[(m + dm, n + dn)]))
# A joint on the rim stays put, so the static grid meets the moving one.
rim = [abs(m + n) == CENTRE or abs(m - n) == CENTRE for m, n in joints]

static_lines = []
for family in range(2):
    for fixed in range(-2 * R, 2 * R + 1):
        run = []
        for step in range(-2 * R, 2 * R + 1):
            m, n = (fixed, step) if family == 0 else (step, fixed)
            m2, n2 = (m, n + 1) if family == 0 else (m + 1, n)
            dynamic = moving(m, n) and moving(m2, n2)
            piece = None if dynamic else clip(joint(m, n)[:2], joint(m2, n2)[:2])
            if piece:
                if not run or math.dist(run[-1], piece[0]) > 0.01:
                    if len(run) >= 2:
                        static_lines.append(run)
                    run = [piece[0]]
                run.append(piece[1])
            elif len(run) >= 2:
                static_lines.append(run)
                run = []
            else:
                run = []
        if len(run) >= 2:
            static_lines.append(run)

# --- The face (floor units) ----------------------------------------------------
# A round smiley painted on the floor: a circle, a wide smile with upturned
# corners, and two tall eyes drawn as outlines.
# Its 64 points are spaced evenly along the ellipse it makes on screen, so the
# narrow ends get segments as long as the rest and stay smooth when bent.
_fine = [(0.6 * math.cos(2 * math.pi * k / 4096 + 0.05), 0.6 * math.sin(2 * math.pi * k / 4096 + 0.05)) for k in range(4096)]
_screen = [(S * u, S * K * v) for u, v in _fine]
_lengths = [math.dist(_screen[k], _screen[(k + 1) % 4096]) for k in range(4096)]
_total, _walk, border = sum(_lengths), 0.0, []
for k in range(4096):
    if _walk >= _total * len(border) / 64:
        border.append(_fine[k])
    _walk += _lengths[k]
assert len(border) == 64
# The smile's centre line: 30 points of arc with an upturned tick at each
# corner, all one outline so the corners bend with the water.
arc = []
for i in range(30):
    th = math.radians(20 + (160 - 20) * i / 29)
    arc.append((0.38 * math.cos(th), -0.05 + 0.38 * math.sin(th)))
tick = lambda end, sign: (end[0] + sign * 0.03, end[1] - 0.12)  # up and a little outward, floor units
smile = [tick(arc[0], 1)] + arc + [tick(arc[-1], -1)]
eyes = [(-0.2, -0.22), (0.2, -0.22)]


def turn(u, v):
    """Turns a floor point about the face's centre by ROTATION."""
    c, s_ = math.cos(math.radians(ROTATION)), math.sin(math.radians(ROTATION))
    return u * c - v * s_, u * s_ + v * c


smile = [turn(u, v) for u, v in smile]
eyes = [turn(u, v) for u, v in eyes]


def resample(polygon, count):
    """`count` points evenly along a closed polygon's perimeter, from its first point."""
    edges = [(polygon[i], polygon[(i + 1) % len(polygon)]) for i in range(len(polygon))]
    lengths = [math.dist(a, b) for a, b in edges]
    total, points, at, start = sum(lengths), [], 0, 0.0
    for k in range(count):
        d = total * k / count
        while d > start + lengths[at] and at < len(edges) - 1:
            start += lengths[at]
            at += 1
        (ax, ay), (bx, by) = edges[at]
        t = (d - start) / lengths[at]
        points.append((ax + (bx - ax) * t, ay + (by - ay) * t))
    return points


# An eye at rest: a tall oval, 13 x 16 px on screen, clockwise from its top.
eye_oval = [(0.04 * math.sin(2 * math.pi * i / 64), -0.12 * math.cos(2 * math.pi * i / 64)) for i in range(64)]
# On an accent it morphs into a bolt: a thin outline around a zigzag line, so
# the bold stroke draws a crisp lightning stroke (screen px about the eye's
# centre; every turn well away from straight back).
zigzag = [(4.5, -10), (-2.5, -1.5), (3.0, 1.5), (-4.5, 10)]
eye_bolt = [(x / S, y / (S * K)) for x, y in resample(ribbon(zigzag, 0.4), 64)]
# Each eye's outlines about its centre, turned with the face; the right eye mirrors the left.
outline = lambda shape, eye: [turn(du if eye == 0 else -du, dv) for du, dv in shape]
ovals = [outline(eye_oval, e) for e in range(2)]
bolts = [outline(eye_bolt, e) for e in range(2)]
# Pseudo-random pits for the pH corrosion, -1..1: alternating, so the line goes jagged.
# Corrosion lumps for the pH, -1..1 along the border: two slow waves (12 and 7
# a turn), so the line goes lumpy, never a point-to-point sawtooth.
pits = [0.55 * math.sin(2 * math.pi * 12 * i / 64 + 1.0) + 0.45 * math.sin(2 * math.pi * 7 * i / 64 + 2.0) for i in range(64)]


# Half-size pixel offsets (the face's nodes sit at scale 2 on its centre).
half = lambda u, v: (S * u / 2, S * K * v / 2)
smile_half = [half(u, v) for u, v in smile]
smile_edges = ribbon(smile_half, 0.15)  # 0.6 px apart on screen: the stroke draws over the gap

# --- Nodes ---------------------------------------------------------------------
PATHS = 4                     # border, smile and two eyes: nodes 0..3
HEADER = PATHS
GRID = HEADER + 8
SPLASH = GRID + len(segments)
CROWN, DROPLETS = 10, 6       # per splash; 6 droplets, not 11, keep the redraw near 20 nodes
SPLASH_NODES = 2 * (CROWN + DROPLETS)
LANE = SPLASH + SPLASH_NODES
LANE_NODES = 49
NODES = LANE + LANE_NODES
assert NODES <= 224, NODES

SEGMENT = 32.0  # a grid segment's authored length; its node scales it to the joints

palette = [  # fill, stroke width
    ("#000000", 0.5), ("#5A5A5A", 0.6), ("#B4B4B4", OUTLINE / 2), ("#B4B4B4", max(MOUTH - 0.6, 0.5) / 2),
    ("#C8C8C8", 0.5), ("#828282", 0.5), ("#4A4A4A", 0.5), ("#B4B4B4", EYES / 2),
]

header_block = (ROOT / "panel.svg.fragment").read_text().split("<!-- SDK two-page header")[1]
header_block = "<!-- SDK two-page header" + header_block.split("\n\n")[0]
# The header's eight bar nodes follow the paths (the generated block names 2..9 first).
bars = re.findall(r'acidrain_node_(\d+)"', header_block)
for k, old in enumerate(bars):
    header_block = header_block.replace(f'acidrain_node_{old}"', f'acidrain_node_bar{k}"')
for k in range(len(bars)):
    header_block = header_block.replace(f'acidrain_node_bar{k}"', f'acidrain_node_{HEADER + k}"')

out = ['<g id="acidrain_panel">', header_block.rstrip(), "", "<!-- The floor's still grid; the centre's segments are nodes. -->"]
for line in static_lines:
    for start in range(0, len(line) - 1, 30):
        part = line[start:start + 31]
        out.append(f'<path d="{path_d(ribbon(part, 0.3))}" fill="none" stroke="#5A5A5A" stroke-width="0.6"/>')
seg = path_d([(0, -0.3), (SEGMENT, -0.3), (SEGMENT, 0.3), (0, 0.3)])
for i, (a, b) in enumerate(segments):
    out.append(f'<path id="acidrain_node_{GRID + i}" d="{seg}" fill="none" stroke="#5A5A5A" stroke-width="0.6"/>')
out.append("<!-- The face, painted on the floor over the grid. -->")
join = ' stroke-linejoin="round"' if OUTLINE >= 5 else ""
cx, cy = screen(0, 0)
border_half = [half(u, v) for u, v in border]
out.append(f'<path id="acidrain_node_0" transform="matrix(1 0 0 1 {fmt(cx)} {fmt(cy)})" d="{path_d(border_half)}" fill="none" stroke="#B4B4B4" stroke-width="{fmt(OUTLINE / 2)}"{join}/>')
out.append(f'<path id="acidrain_node_1" transform="matrix(1 0 0 1 {fmt(cx)} {fmt(cy)})" d="{path_d(smile_edges)}" fill="none" stroke="#B4B4B4" stroke-width="{fmt(max(MOUTH - 0.6, 0.5) / 2)}"{join}/>')


for i, (u, v) in enumerate(eyes):
    points = [half(u + du, v + dv) for du, dv in ovals[i]]
    out.append(f'<path id="acidrain_node_{2 + i}" transform="matrix(1 0 0 1 {fmt(cx)} {fmt(cy)})" d="{path_d(points)}" fill="none" stroke="#B4B4B4" stroke-width="{fmt(EYES / 2)}"{join}/>')
square = path_d([(-2, -2), (2, -2), (2, 2), (-2, 2)])
bar = path_d([(-20, -1.5), (0, -1.5), (0, 1.5), (-20, 1.5)])
dash = path_d([(-4, -1), (4, -1), (4, 1), (-4, 1)])
for i in range(SPLASH_NODES):
    out.append(f'<path id="acidrain_node_{SPLASH + i}" transform="matrix(1 0 0 1 640 320)" d="{square}" fill="#C8C8C8" stroke="#C8C8C8" stroke-width="0.5"/>')
for i in range(16):
    out.append(f'<path id="acidrain_node_{LANE + i}" transform="matrix(1 0 0 1 640 320)" d="{square}" fill="#828282" stroke="#828282" stroke-width="0.5"/>')
for i in range(16):
    out.append(f'<path id="acidrain_node_{LANE + 16 + i}" transform="matrix(1 0 0 1 640 320)" d="{bar}" fill="#C8C8C8" stroke="#C8C8C8" stroke-width="0.5"/>')
for i in range(16):
    out.append(f'<path id="acidrain_node_{LANE + 32 + i}" transform="matrix(1 0 0 1 640 320)" d="{square}" fill="#4A4A4A" stroke="#4A4A4A" stroke-width="0.5"/>')
out.append(f'<path id="acidrain_node_{LANE + 48}" transform="matrix(1 0 0 1 640 320)" d="{dash}" fill="#828282" stroke="#828282" stroke-width="0.5"/>')
for i, (colour, width) in enumerate(palette):
    out.append(f'<circle id="acidrain_palette_{i}" cx="640" cy="0" r="1" fill="{colour}" stroke="{colour}" stroke-width="{fmt(width)}"/>')
out.append("</g>")
(ROOT / "panel.svg.fragment").write_text("\n".join(out) + "\n")

# --- Tables --------------------------------------------------------------------
q10 = lambda v: int(round(v * 1024))


def row(name, ctype, values):
    return f"const {ctype} {name}[{len(values)}] = {{{', '.join(str(v) for v in values)}}};"


J = [joint(m, n) for m, n in joints]
# e^-x for x = i / 8, i = 0..64, Q15; 2^(i / 32) for i = 0..32, Q15.
exp_neg = [int(round(math.exp(-i / 8) * 32767)) for i in range(65)]
exp2 = [int(round(2 ** (i / 32) * 16384)) for i in range(33)]
# 1 / r for r = (i + 0.5) / 64 floor units, i = 0..255, Q10 of units^-1 scaled by 64.
wave = [int(round(math.sin(2 * math.pi * i / 256) * 1024)) for i in range(256)]
recip = [min(65535, int(round(1024 / ((i + 0.5) / 64)))) for i in range(256)]

tables = f'''#pragma once
// Written by tools/make_panel.py with panel.svg.fragment: edit that and run it again.
#include <opxy/types.hpp>

namespace acidrain {{
namespace screen {{
using opxy::i32;
using opxy::u16;
using opxy::u8;

const i32 scale = {int(S)};           // pixels per floor unit across
const i32 slant = {q10(K)};           // and down, Q10 of that
const i32 centre_x = {q10(cx)}, centre_y = {q10(cy)};  // the face, Q10 px
const u32_t_placeholder = 0;
const i32 segment = {int(SEGMENT)};       // a grid segment's authored length, px
const unsigned header = {HEADER}, grid = {GRID};
const unsigned splash = {SPLASH}, lane = {LANE};
const unsigned crown = {CROWN}, droplets = {DROPLETS};  // pixels per splash
const unsigned joint_count = {len(J)}, segment_count = {len(segments)};

// The moving joints: resting screen position (Q10 px), floor position (Q10 units), 1 on the rim.
{row("joint_x", "i32", [q10(j[0]) for j in J])}
{row("joint_y", "i32", [q10(j[1]) for j in J])}
{row("joint_u", "i32", [q10(j[2]) for j in J])}
{row("joint_v", "i32", [q10(j[3]) for j in J])}
{row("joint_rim", "u8", [int(r) for r in rim])}
{row("segment_from", "u8", [a for a, b in segments])}
{row("segment_to", "u8", [b for a, b in segments])}

// The face: the border's 64 points and the smile's 32, floor Q10; the smile
// ribbon's two edges as half-size offsets from its centre line, Q10 px.
{row("border_u", "i32", [q10(u) for u, v in border])}
{row("border_v", "i32", [q10(v) for u, v in border])}
{row("pits", "i32", [q10(p) for p in pits])}
{row("smile_u", "i32", [q10(u) for u, v in smile])}
{row("smile_v", "i32", [q10(v) for u, v in smile])}
{row("smile_dx", "i32", [q10(smile_edges[i][0] - smile_half[i][0]) for i in range(32)])}
{row("smile_dy", "i32", [q10(smile_edges[i][1] - smile_half[i][1]) for i in range(32)])}
{row("eye_u", "i32", [q10(u) for u, v in eyes])}
{row("eye_v", "i32", [q10(v) for u, v in eyes])}
// Each eye's outline about its centre, floor Q10, 64 points an eye: the oval at
// rest, the bolt on an accent.
{row("oval_u", "i32", [q10(u) for e in range(2) for u, v in ovals[e]])}
{row("oval_v", "i32", [q10(v) for e in range(2) for u, v in ovals[e]])}
{row("bolt_u", "i32", [q10(u) for e in range(2) for u, v in bolts[e]])}
{row("bolt_v", "i32", [q10(v) for e in range(2) for u, v in bolts[e]])}

// e^-(i / 8), Q15; 2^(i / 32), Q14; 64 / ((i + 0.5) / 64 units) as Q10 per unit.
{row("exp_neg", "i32", exp_neg)}
{row("exp2", "i32", exp2)}
{row("reciprocal", "u16", recip)}
// sin of i / 256 turn, Q10.
{row("wave", "i32", wave)}
}}
}}
'''.replace("const u32_t_placeholder = 0;\n", "")
(ROOT / "display_tables.h").write_text(tables)
print(f"{len(J)} joints, {len(segments)} segments, {len(static_lines)} static lines, {NODES} nodes")
