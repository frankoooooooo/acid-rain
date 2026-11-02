# Acid Rain report

Host measurements at commit time, against SDK `staging` 16de93a (SDK 1.8).
Nothing here is a device measurement: hardware CPU and the device checks below
are still to do.

## CPU

| | Acid Rain | ACIDXY |
| --- | --- | --- |
| `sdk.py test` estimate, heaviest setting | 31,900–36,700 on a Mac (varies run to run); 22,000 on GitHub CI | 30,500 on the same Mac |
| Hardware readout (`opxy-synth-c8-sweep-v1`) | not measured yet | 6.08% default, 7.72% heaviest (Wav Dist) |

The estimate does **not** put Acid Rain under ACIDXY. It is skewed by the
multiply: Acid Rain uses the SDK's `q31::multiply`, one instruction on the
Blackfin, which the host runs as a 64-bit sequence. Timed with a host-cheap
multiply instead, the same render costs about 10,000–12,500. ACIDXY uses its
own 32-bit split multiply, cheap on the host but several instructions on the
Blackfin, so the two estimates don't compare like for like.

Counting the pinned Blackfin compiler's output instead (`-O2 -mcpu=bf707`),
per 64-sample block:

| Stage | Acid Rain | Notes |
| --- | --- | --- |
| Oscillator | about 1,900 | PolyBLEP saw, 44 Hz input highpass, slide ramp; two samples a pass |
| Filter | about 2,560 | Open303 TB_303 ladder at 1x with its 150 Hz feedback highpass; b0 and k ramped |
| Output | about 1,300–1,600 | Merged highpass biquad, VCA ramp, half-scale ceiling |
| pH corrosion | about 0 at pH 7, 700–800 below | One interpolated table read and two multiplies a sample |
| Block work | about 300 | Envelopes, cutoff mapping, one divide (polyBLEP's reciprocal) |
| **Total** | **about 6,000 at pH 7, 7,000 at pH 2** | Approximate: GCC's branchy block layout blurs the count |

ACIDXY's render is too branchy to count the same way (its main loop body is
over 500 instructions with per-operation overflow branches), so the stage-by-
stage ACIDXY column is left to the hardware runs.

What Acid Rain takes from ACIDXY's lessons, and what it avoids:

- Coefficients worked out once a block and ramped (cutoff, resonance, VCA, slide).
- One shaper stage at most (pH), skipped outright at pH 7; no limiter pass;
  no per-sample overflow checks (headroom proved below instead).
- Silence: once the VCA falls under -84 dB the voice writes zeros and skips
  every stage. (It can't see note-offs, so this follows its own amp envelope,
  Open303's 1.23 s decay, or a short gate.)

The heaviest setting is pH 2.0 (corrosion on every sample) with accents.

## Headroom (no per-sample checks)

`tests/sound.cpp` plays every corner of cutoff, reso, env mod and decay, at
pH 2 and 7, at 12.5 Hz, 50 Hz, 440 Hz and 2.1 kHz, with accents locked on and
stacking every four blocks, under UBSan and ASan:

- Largest ladder state y1..y3: 26.4 (limit 64 of Q24's 128x headroom).
- Largest y4: 1.49 (the feedback product overflows above 7.5).
- Output never exceeds half scale.

No setting failed, so no per-block clamp was added.

## Against Open303

`acid-rain-work/reference` (outside this repository) renders the same scenes
through Open303's own `rosic_Open303` (4x oversampled, no sequencer) and
through Acid Rain, sixteenths at 120 bpm with a 75% gate, at the same level.

| Scene | Level vs Open303 | Brightness (spectral centroid) tracking | Notes |
| --- | --- | --- | --- |
| Cutoff sweep | +0.3 dB | r = 1.00 | Acid Rain about 6% darker throughout (1x vs 4x oversampled ladder) |
| Resonance sweep | -0.4 dB | r = 1.00 | 297 vs 338 Hz at reso 0, 1,344 vs 1,463 Hz at 100 |
| Env mod sweep | +0.3 dB | r = 1.00 | 6% darker |
| Decay sweep | +0.4 dB | r = 1.00 | |
| Accent | -0.4 dB | r = 0.99 | Accent over plain: +10.1 dB (Open303 +10.8 dB) |
| Slide | -0.1 dB | r = 0.98 | Same 60 ms glide time constant |
| Saw and square | +0.6 dB | r = 0.95 | Square is a build constant (off) |

Open303's own accent renders reach full scale; Acid Rain's pass a cubic soft
clip that reaches half scale, so its accents saturate a little (brighter than
Open303's by 15 to 20%) instead of clipping.

Low end: across the scenes Acid Rain's share of energy in every band from 20 Hz
to 5 kHz is within 1 dB of Open303's, with slightly more sub (20-45 Hz). Raising
the feedback highpass from 150 to 400 Hz moved the 40-160 Hz share by under
1 dB, so the filter is unchanged; the level was what was missing.

## Screen

`tests/display.cpp`, 30 frames a second, sixteenths at 120 bpm for 8 s, then
8 s of rest:

| Setting | Nodes changed a frame, average | Peak | At rest |
| --- | --- | --- | --- |
| Defaults | 14.5 | 18 | 0 |
| Heaviest (reso, env mod, decay 2.0 s, dew 100, pH 2, accent auto, slide on) | 21.4 | 34 | 0 |
| Highest cutoff, pH 2, accent on | 14.9 | 18 | 0 |
| Reduced motion | 0.6 | 5 | 0 |

The face is four outlines (border, smile, two eyes), each redrawn as one node.
One refraction field moves the floor and the face alike, at most 5 px however
many drops overlap, so the smile and eyes stay at least 7.1 px inside the
border (checked over the heaviest run). Each drop has its own ring tightness,
height and phase and two late satellite drops, so rings interfere unevenly.
The face's rings are never finer than 4 of its points (the floor's can be), the
border's shift is smoothed four times and its points are spaced evenly along
the ellipse, and a spacing guard keeps neighbours at 64% or more of their
resting spacing: the water adds at most 28 degrees of turn at any border point
(a sawtooth adds 60 or more). On every note a tilted, sliced teardrop falls
onto the impact over 0.1 s, its bands tearing sideways each frame; a splash
now marks only the rain.

To stay near 20: ripples reach less far, about 12 grid segments and splash
pixels redraw a frame (the joints furthest out of place first, the rest catch
up), a cluster of rain splashes once, and a splash throws 6 droplets, not 11.
The face's two outlines carry most of the water's motion: each redraws as one
node however many points move.

Every control at 0, 50 and 100 changes the picture, the four sound controls
subtly since the ripples were made smaller (reso least) (contact sheet in
`acid-rain-work/frames/contact.png`, not committed). Dew is the subtlest: it
shows only as how often splashes and bolt eyes come.

## Open risks and device checks

- **Hardware CPU**: not measured. Below 3.5% isn't proven until a profiling run.
- **Late locks**: accent and slide are latched on a note's first block and
  accepted on its second. Check on the device that an accent lock on every
  other step sounds accented from its first block.
- **Slide lock after a held note**: check it glides without retriggering.
- **Rain sync**: check the screen's splashes land on the audible rain accents
  (accent auto, high dew). The engine's clock pauses when nothing renders.
- **Stock portamento**: not tested; check whether it reaches the engine as a
  moving pitch buffer and whether stock starts a new voice.
- **Drawing**: whether stroke width scales with a node's scale (the face
  is drawn at scale 2 with half-weight strokes); the outlines across sweeps of
  every control pair; photos at rest, playing and heaviest.
- **Captions**: not measured in Univers Next Pro (the font isn't on this
  machine). The longest are `auto`, `5.6k`, `0.4s`, `803`.
- **Pre-filter drive** (ACIDXY-style): not built.
