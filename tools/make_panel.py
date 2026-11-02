#!/usr/bin/env python3
"""Writes panel.svg.fragment (Acid Rain's artwork) and display_tables.h (the
same geometry for display.cpp), so the static grid and the moving one meet.
Run it after changing a number here, and commit all three files:

    python3 tools/make_panel.py

The floor is a plane seen from above at a slant: floor units (u, v) map to the
screen as x = 240 + S u, y = Y0 + S K v. Ripples are circles on the floor.
"""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
S, K, Y0 = 160.0, 0.42, 116.0     # pixels per floor unit, slant, the face's centre
LATTICE = 0.344                  # floor spacing between parallel grid lines
WEIGHT = 5                       # build constant: the face line, 2.92, 5 (Bold) or 8 px
TOP, BOTTOM = 40.0, 220.0        # the grid's band, below the two-page header
CENTRE = 5                       # moving joints: |m + n| <= CENTRE and |m - n| <= CENTRE


def screen(u, v):
    return 240 + S * u, Y0 + S * K * v


def warp(x, y):
    """The floor's resting unevenness: a still, gentle wobble in the grid."""
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
def border_radius(theta):
    # A hand-drawn blob, not an ellipse: a few slow harmonics.
    return 0.6 * (1 + 0.07 * math.sin(2 * theta + 0.6) + 0.06 * math.sin(3 * theta + 1.0)
                  + 0.035 * math.sin(5 * theta + 2.2) + 0.02 * math.sin(7 * theta + 0.3))


border = []
for i in range(64):
    th = 2 * math.pi * i / 64 + 0.05
    r = border_radius(th)
    border.append((r * math.cos(th), 1.25 * r * math.sin(th)))  # taller than round, as drawn
smile = []
for i in range(32):
    th = math.radians(28 + (152 - 28) * i / 31)
    smile.append((0.5 * math.cos(th), -0.2 + 0.5 * math.sin(th)))
eyes = [(-0.24, -0.27), (0.24, -0.27)]
# Pseudo-random pits for the pH corrosion, -1..1: alternating, so the line goes jagged.
pits = [((-1) ** i) * (0.45 + 0.55 * abs(math.sin(i * 12.9898) * 43758.5453 % 1)) for i in range(64)]

# Half-size pixel offsets (the face's nodes sit at scale 2 on its centre).
half = lambda u, v: (S * u / 2, S * K * v / 2)
smile_half = [half(u, v) for u, v in smile]
smile_edges = ribbon(smile_half, 0.3)
# The ticks: a short dash at each end of the smile, up and outward.
ticks = []
for end, sign in ((smile_half[0], 1), (smile_half[-1], -1)):
    x, y = end
    ticks.append(ribbon([(x - sign * 2.0, y + 3.0), (x + sign * 3.0, y - 3.5)], 0.3))

# Beads of dew, gathered around the face.
beads = []
for i in range(16):
    th = 2 * math.pi * (i + 0.37 * math.sin(i * 7.1)) / 16
    r = 0.7 + 0.11 * abs(math.sin(i * 3.7))
    x, y = screen(r * math.cos(th), r * math.sin(th))
    beads.append((round(x * 2) / 2, round(y * 2) / 2))

# --- Nodes ---------------------------------------------------------------------
HEADER = 2
OVALS, BOLTS, TICKS = 10, 12, 14
GRID = 16
SPLASH = GRID + len(segments)
CROWN, DROPLETS = 10, 6       # per splash; 6 droplets, not 11, keep the redraw near 20 nodes
SPLASH_NODES = 2 * (CROWN + DROPLETS)
LANE = SPLASH + SPLASH_NODES
LANE_NODES = 49
BEADS = LANE + LANE_NODES
NODES = BEADS + 16
assert NODES <= 224, NODES

SEGMENT = 32.0  # a grid segment's authored length; its node scales it to the joints

palette = [  # fill, stroke width
    ("#000000", 0.5), ("#5A5A5A", 0.6), ("#B4B4B4", WEIGHT / 2), ("#B4B4B4", (WEIGHT - 1) / 2),
    ("#C8C8C8", 0.5), ("#828282", 0.5), ("#4A4A4A", 0.5), ("#B4B4B4", 0.5),
]

header_block = (ROOT / "panel.svg.fragment").read_text().split("<!-- SDK two-page header")[1]
header_block = "<!-- SDK two-page header" + header_block.split("\n\n")[0]

out = ['<g id="acidrain_panel">', header_block.rstrip(), "", "<!-- The floor's still grid; the centre's segments are nodes. -->"]
for line in static_lines:
    for start in range(0, len(line) - 1, 30):
        part = line[start:start + 31]
        out.append(f'<path d="{path_d(ribbon(part, 0.3))}" fill="none" stroke="#5A5A5A" stroke-width="0.6"/>')
join = ' stroke-linejoin="round"' if WEIGHT >= 5 else ""
cx, cy = screen(0, 0)
border_half = [half(u, v) for u, v in border]
out.append(f'<path id="acidrain_node_0" transform="matrix(1 0 0 1 {fmt(cx)} {fmt(cy)})" d="{path_d(border_half)}" fill="none" stroke="#B4B4B4" stroke-width="{fmt(WEIGHT / 2)}"{join}/>')
out.append(f'<path id="acidrain_node_1" transform="matrix(1 0 0 1 {fmt(cx)} {fmt(cy)})" d="{path_d(smile_edges)}" fill="none" stroke="#B4B4B4" stroke-width="{fmt((WEIGHT - 1) / 2)}"{join}/>')


def oval():
    return [(3.6 * math.cos(2 * math.pi * i / 16), 5.2 * math.sin(2 * math.pi * i / 16)) for i in range(16)]


def bolt():
    # A lightning bolt as one filled outline with flat tips, about 22 px tall.
    return [(-4.5, -11), (3.5, -11), (0.5, -2.5), (5.5, -2.5), (-2.5, 11), (-4.5, 11), (-1.0, 1.5), (-6.0, 1.5)]


for i, (u, v) in enumerate(eyes):
    x, y = screen(u, v)
    out.append(f'<path id="acidrain_node_{OVALS + i}" transform="matrix(1 0 0 1 {fmt(x)} {fmt(y)})" d="{path_d(oval())}" fill="#B4B4B4" stroke="#B4B4B4" stroke-width="0.5"/>')
for i, (u, v) in enumerate(eyes):
    x, y = screen(u, v)
    shape = bolt() if i == 0 else [(-px, py) for px, py in bolt()][::-1]
    out.append(f'<path id="acidrain_node_{BOLTS + i}" transform="matrix(1 0 0 1 640 320)" d="{path_d(shape)}" fill="#000000" stroke="#000000" stroke-width="0.5"/>')
for i in range(2):
    out.append(f'<path id="acidrain_node_{TICKS + i}" transform="matrix(1 0 0 1 {fmt(cx)} {fmt(cy)})" d="{path_d(ticks[i])}" fill="none" stroke="#B4B4B4" stroke-width="{fmt((WEIGHT - 1) / 2)}"/>')
seg = path_d([(0, -0.3), (SEGMENT, -0.3), (SEGMENT, 0.3), (0, 0.3)])
for i, (a, b) in enumerate(segments):
    out.append(f'<path id="acidrain_node_{GRID + i}" d="{seg}" fill="none" stroke="#5A5A5A" stroke-width="0.6"/>')
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
for i in range(16):
    out.append(f'<path id="acidrain_node_{BEADS + i}" transform="matrix(1 0 0 1 640 320)" d="{square}" fill="#C8C8C8" stroke="#C8C8C8" stroke-width="0.5"/>')
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
const unsigned header = {HEADER}, ovals = {OVALS}, bolts = {BOLTS}, ticks = {TICKS}, grid = {GRID};
const unsigned splash = {SPLASH}, lane = {LANE}, beads = {BEADS};
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
{row("bead_x", "i32", [q10(x) for x, y in beads])}
{row("bead_y", "i32", [q10(y) for x, y in beads])}

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
