#!/usr/bin/env python3
"""Writes tables.h (the voice's constants) and rain_tables.h (the humidity
model's constants, shared by the engine and the screen). Run it after changing
a number here, and commit all three files:

    python3 tools/make_tables.py
"""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
FS = 44100.0
BLOCK = 64
OCT_STEPS = 8          # cutoff table entries per octave
OCTAVES = 10           # from 20 Hz up to 20.48 kHz


def q(x, bits):
    return int(round(x * (1 << bits)))


def q31(x):
    return max(-2**31, min(2**31 - 1, q(x, 31)))


def row(name, ctype, values):
    return f"const {ctype} {name}[{len(values)}] = {{{', '.join(str(v) for v in values)}}};"


def tb303(f):
    # rosic_TeeBeeFilter.h calculateCoefficientsApprox4, TB_303 mode, with r = 1
    fx = 2 * math.pi * f / FS / math.sqrt(2) / (2 * math.pi)
    b0 = (0.00045522346 + 6.1922189 * fx) / (1.0 + 12.358354 * fx + 4.4156345 * fx * fx)
    k = fx * (fx * (fx * (fx * (fx * (fx + 7198.6997) - 5837.7917) - 476.47308) + 614.95611) + 213.87126) + 16.998792
    return b0, k


def per_block(tau):
    """Per-block decay of an exponential with time constant tau (s), Q31."""
    return q31(math.exp(-BLOCK / (FS * tau)))


cut = [tb303(min(20.0 * 2 ** (i / OCT_STEPS), FS * 0.45)) for i in range(OCTAVES * OCT_STEPS + 1)]
skew = [q((1 - math.exp(-3 * i / 32)) / (1 - math.exp(-3)), 15) for i in range(33)]
# Decay knob: the filter envelope's time constant, 0.2 s to 2.0 s, exponential.
decay = [per_block(0.2 * 10 ** (i / 32)) for i in range(33)]

# Corrosion (pH): a gentle wavefold with grit, over x in [-2, 2). The steps
# are the grit: the fold's value held to 1/40ths of full scale.
FOLD = []
for i in range(257):
    x = (i - 128) / 64.0
    y = 0.92 * math.sin(1.35 * x) + 0.12 * math.sin(4.1 * x)
    FOLD.append(q(round(y * 40) / 40, 24))
# The square (a build constant): the saw through a steep shaper, Open303's
# SQUARE303 recipe, over x in [-1, 1).
SQUARE = [q(math.tanh(6.0 * math.sin(math.pi * (i - 128) / 128.0)) * 0.9, 24) for i in range(257)]

# Highpass2 (24.167 Hz one-pole) and the 7.5 Hz, 4.7-octave notch merged into
# one fixed biquad: a second-order highpass at 30 Hz, Q 0.55, fitted by ear
# and by the reference comparison (REPORT.md).
w = 2 * math.pi * 30.0 / FS
alpha = math.sin(w) / (2 * 0.55)
a0 = 1 + alpha
hp = [(1 + math.cos(w)) / 2 / a0, -(1 + math.cos(w)) / a0, (1 + math.cos(w)) / 2 / a0,
      -2 * math.cos(w) / a0, (1 - alpha) / a0]

# Semitone boundaries inside an octave, for the rain lane's pitch hash: the
# top 16 bits under an increment's leading one, at each half-semitone.
SEMITONE = [q(2 ** ((s + 0.5) / 12) - 1, 16) for s in range(11)]

engine = f'''#pragma once
// Written by tools/make_tables.py: edit that and run it again.
#include <opxy/types.hpp>

namespace acidrain {{
namespace tables {{
using opxy::i32;
using opxy::u32;

const i32 cutoff_steps = {OCTAVES * OCT_STEPS};  // {OCT_STEPS} per octave above 20 Hz
// 2 b0 in Q31 and the r = 1 feedback k in Q16: Open303's TB_303 mode at 44.1 kHz.
{row("cutoff_b0x2", "i32", [q31(2 * b) for b, _ in cut])}
{row("cutoff_k", "i32", [q(k, 16) for _, k in cut])}
// Open303's resonance skew, (1 - e^-3r) / (1 - e^-3), Q15 over 32 steps.
{row("resonance_skew", "i32", skew)}
// The filter envelope's per-block decay over the decay knob (0.2 s to 2.0 s), Q31.
{row("decay_coefficient", "i32", decay)}
// pH corrosion: fold with grit over [-2, 2), Q24.
{row("fold", "i32", FOLD)}
// The square, from the saw, over [-1, 1), Q24.
{row("square", "i32", SQUARE)}
// Open303's accent decay (200 ms), its amp envelope (1.23 s), its 15 ms accent
// smoother, the 60 ms slide, the 5 ms short-gate close, per block, Q31.
const i32 accent_decay = {per_block(0.2)};
const i32 amp_decay = {per_block(1.23)};
const i32 accent_smoothing = {q31(1 - math.exp(-BLOCK / (FS * 0.015)))};
const i32 slide_step = {q31(1 - math.exp(-BLOCK / (FS * 0.060)))};
const i32 gate_close = {per_block(0.005)};
// The build-up between accents fades with a 0.4 s time constant.
const i32 build_decay = {per_block(0.4)};
// Pre-filter highpass (Open303's highpass1, 44.486 Hz) and the feedback highpass
// (150 Hz): their lowpass halves, Q31.
const i32 input_highpass = {q31(2 * math.pi * 44.486 / FS)};
const i32 feedback_highpass = {q31(2 * math.pi * 150 / FS)};
// The merged output highpass: b0, b1, b2, a1, a2 in Q30.
const i32 hp_b0 = {q(hp[0], 30)}, hp_b1 = {q(hp[1], 30)}, hp_b2 = {q(hp[2], 30)};
const i32 hp_a1 = {q(hp[3], 30)}, hp_a2 = {q(hp[4], 30)};
{row("semitone", "u32", SEMITONE)}
}}
}}
'''

# The humidity model. Two stores evaporate with time constants of 0.6 s and
# 6 s. Elapsed blocks are decomposed into powers of two, so the screen (which
# counts seconds) and the engine (which counts blocks) evaporate alike.
FAST_TAU, SLOW_TAU = 0.6, 6.0
fast_pow = [q31(math.exp(-(2 ** b) * BLOCK / (FS * FAST_TAU))) for b in range(13)]
slow_pow = [q31(math.exp(-(2 ** b) * BLOCK / (FS * SLOW_TAU))) for b in range(13)]
# Dew 1..100 sets the rain threshold, 3.2 down to 1.3 (Q24), exponential; 0 never rains.
# Sixteenths at 120 bpm hold about 1.3 in the fast store and build 1.9 in the slow one.
dew = [q(3.2 * (1.3 / 3.2) ** (i / 32), 24) for i in range(33)]

rain = f'''#pragma once
// Written by tools/make_tables.py: edit that and run it again.
#include <opxy/types.hpp>

namespace acidrain {{
namespace rain_tables {{
using opxy::i32;

// Evaporation over 2^b blocks for b = 0..12: the fast store ({FAST_TAU} s) and the slow one ({SLOW_TAU} s), Q31.
{row("fast_evaporation", "i32", fast_pow)}
{row("slow_evaporation", "i32", slow_pow)}
// The rain threshold over the dew knob, Q24 over 32 steps.
{row("threshold", "i32", dew)}
}}
}}
'''

(ROOT / "tables.h").write_text(engine)
(ROOT / "rain_tables.h").write_text(rain)
