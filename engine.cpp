#include "core.h"
#include "tables.h"
#include <opxy/q31.hpp>

// Acid Rain's voice: Robin Schmidt's Open303 (MIT, see NOTICE) in fixed point
// at 1x: a polyBLEP saw, Open303's TB_303 ladder with its feedback highpass and
// measured envelope-to-cutoff mapping, its accent and its amp envelope. Every
// coefficient is worked out once a block and ramped across it. On top: accent
// and slide lanes (off, on, or auto: the rain decides), and pH corrosion.
namespace acidrain {
using opxy::i32;
using opxy::u32;
using opxy::q31::multiply;
namespace t = tables;

const bool square_wave = false;  // build constant: the saw through Open303's square shaper
const i32 one = 1 << 24;
const i32 full = 0x7fffffff;
const i32 build_step = 858993459;   // 0.4 of the way to full on each accent
const i32 accent_level = 1 << 30;   // an accent adds 4 x the filter envelope to the amp, as Open303 at accent 100
const i32 envelope_level = 120795955;  // and every note 0.45 x, both Q31 / 8
const u32 short_blocks = 48;        // a short gate closes after 70 ms
const i32 silent = 1 << 16;         // the amp under -84 dB: nothing more to hear
enum Choice { off = 0, on = 1, automatic = 2 };

// An enum control's choice: off, on or auto.
inline u32 choice(i32 value) { return static_cast<u32>((value < 0 ? 0 : value >> 16) * 3 >> 15); }

// Linear interpolation in a table indexed by a Q31 knob over 32 steps.
inline i32 knob(const i32 (&table)[33], i32 value) {
    const i32 position = value < 0 ? 0 : value >> 16, index = position >> 10, fraction = (position & 1023) << 21;
    if (index >= 32) return table[32];
    return table[index] + multiply(table[index + 1] - table[index], fraction);
}

// A 257-entry shaper read with interpolation, x scaled so +-128 << shift spans the table.
inline i32 shape(const i32 (&table)[257], i32 x, unsigned shift) {
    const i32 index = (x >> shift) + 128;
    if (index < 0) return table[0];
    if (index > 255) return table[256];
    const i32 fraction = (x & ((1 << shift) - 1)) >> (shift - 8);
    return table[index] + ((table[index + 1] - table[index]) >> 8) * fraction;
}

// The semitone an increment plays, counting from the lowest: its octave and
// its place in it, so pitch bends and LFO wobble don't change the rain lane.
inline u32 semitone(u32 increment) {
    u32 top = 31;
    while (top && !(increment >> top)) --top;
    const u32 mantissa = (top >= 16 ? increment >> (top - 16) : increment << (16 - top)) & 0xffffu;
    u32 note = 0;
    while (note < 11 && mantissa >= t::semitone[note]) ++note;
    return top * 12 + note;
}

// polyBLEP residual at a wrap: (1 - t)^2 where t is the distance to it in increments, Q24.
inline i32 blep(u32 distance, u32 width, u32 reciprocal) {
    distance >>= 8;
    if (distance > width) distance = width;  // a glide inside the block can outrun the block's width
    const i32 rest = 32768 - static_cast<i32>((distance * reciprocal) >> 15);
    return (rest * rest) >> 6;
}
// Band-limited saw, Q24, falling like Open303's inverted SAW303.
inline i32 saw(u32 phase, u32 increment, u32 width, u32 reciprocal) {
    i32 value = one - static_cast<i32>(phase >> 7);
    if (phase < increment) value -= blep(phase, width, reciprocal);
    else if (phase > 0u - increment) value += blep(0u - phase, width, reciprocal);
    return value;
}

// A slide carries on from the previous note: its pitch glides here and its
// filter envelope keeps decaying, without a retrigger.
inline void slide(Voice& voice, u32 increment) {
    if (!voice.origin) return;  // nothing played before: nothing to slide from
    voice.offset = static_cast<i32>(voice.origin - increment);
    voice.envelope = voice.origin_envelope;
}
inline void accent(Shared& shared) { shared.build += multiply(full - shared.build, build_step); }

inline void initialize(Voice& voice, Shared&, u32) {
    voice = Voice();
    voice.lane = lane_quiet;
}
inline void start(Voice& voice, Shared& shared, u32 owner) {
    const u32 phase = voice.phase;
    voice = Voice();
    voice.phase = phase;  // a free-running phase, like the 303's oscillator
    voice.envelope = voice.amp = voice.gate = full;
    shared.ticker = owner;  // the newest note keeps time and carries the line
}
inline void destroy(Voice&, Shared& shared, u32 owner) {
    if (shared.ticker == owner) shared.ticker = 0;
}

// The note's first block: latch accent and slide, let the rain decide what's on auto.
inline void begin(Voice& voice, Shared& shared, opxy::Parameters parameters, u32 increment) {
    const u32 a = choice(parameters[4]), s = choice(parameters[5]);
    const bool rained = rain_note(shared.rain, shared.clock - shared.note_clock, parameters[6], parameters[7]);
    shared.note_clock = shared.clock;
    // The rain lane: the same three notes in a row decide the same way, at any pattern length.
    const u32 note = semitone(increment);
    u32 h = note * 0x9e3779b1u ^ shared.history[0] * 0x85ebca77u ^ shared.history[1] * 0xc2b2ae3du;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    shared.history[1] = shared.history[0];
    shared.history[0] = note;
    const u32 offbeat = shared.notes++ & 1;
    voice.origin = shared.increment;
    voice.origin_envelope = shared.envelope;

    u32 lane = 0;
    if (a == on) lane |= lane_accent | lane_accent_locked;
    else if (a == automatic && rained) lane |= lane_accent;
    if (s == on) lane |= lane_slide | lane_slide_locked;
    else if (s == automatic) {
        // Out of 1024: slides favour off-beats; octaves and short gates need acid.
        const i32 acid = acidity(parameters[6]), dew = parameters[7] < 0 ? 0 : parameters[7] >> 16;
        const i32 glide = 40 + (acid * 420 >> 15);
        if (static_cast<i32>(h & 1023) < (offbeat ? glide * 3 >> 1 : glide >> 1)) lane |= lane_slide;
        if (static_cast<i32>(h >> 10 & 1023) < (acid * 260 >> 15)) lane |= lane_octave;
        if (static_cast<i32>(h >> 20 & 1023) < ((acid * 150 + dew * 120) >> 15)) lane |= lane_short;
    }
    voice.lane = lane;
    const u32 octave = lane & lane_octave ? 1 : 0;
    if (lane & lane_slide) slide(voice, increment << octave);
    if (lane & lane_accent) accent(shared);
}

// The second block: a lock that arrived a block late still counts.
inline void relatch(Voice& voice, Shared& shared, opxy::Parameters parameters, u32 increment) {
    const u32 a = choice(parameters[4]), s = choice(parameters[5]);
    const u32 octave = voice.lane & lane_octave ? 1 : 0;
    if (a == on && !(voice.lane & lane_accent)) {
        voice.lane |= lane_accent | lane_accent_locked;
        accent(shared);
    } else if (a == off && (voice.lane & lane_accent) && !(voice.lane & lane_accent_locked)) {
        voice.lane &= ~lane_accent;
    }
    if (s == on && !(voice.lane & lane_slide)) {
        voice.lane |= lane_slide | lane_slide_locked;
        slide(voice, increment << octave);
    } else if (s == off && (voice.lane & lane_slide) && !(voice.lane & lane_slide_locked)) {
        voice.lane &= ~lane_slide;
        voice.offset = 0;
        voice.envelope = full;
    }
}

inline void render(Voice& voice, Shared& shared, u32 owner, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    if (!shared.ticker) shared.ticker = owner;
    const bool newest = shared.ticker == owner;
    if (newest) {
        ++shared.clock;
        shared.build = multiply(shared.build, t::build_decay);
    }
    if (voice.age == 0) begin(voice, shared, parameters, pitch[0]);
    else if (voice.age == 1) relatch(voice, shared, parameters, pitch[0]);
    ++voice.age;
    if (voice.lane & lane_quiet) {
        for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] = 0;
        return;
    }
    const u32 lane = voice.lane, octave = lane & lane_octave ? 1 : 0;
    const bool accented = (lane & lane_accent) != 0;

    // Envelopes. An accent shortens the filter envelope to Open303's 200 ms.
    voice.envelope = multiply(voice.envelope, accented ? t::accent_decay : knob(t::decay_coefficient, parameters[3]));
    voice.sweep += multiply((accented ? voice.envelope : 0) - voice.sweep, t::accent_smoothing);
    voice.amp = multiply(voice.amp, t::amp_decay);
    if ((lane & lane_short) && voice.age > short_blocks) voice.gate = multiply(voice.gate, t::gate_close);

    // Slide: the glide's distance to the played pitch shrinks over 60 ms,
    // ramped across the block (MNMD's Glide).
    const i32 offset = voice.offset;
    voice.offset = offset - multiply(offset, t::slide_step);
    const i32 slope = (voice.offset - offset) >> 5;  // per pair of samples

    // Cutoff in Q16 octaves above 20 Hz: the knob (220 Hz to 5.6 kHz), then
    // Open303's measured envelope mapping, then the accent sweep and its build-up.
    const i32 base = 226716 + multiply(306045, parameters[0]);
    i32 c = multiply(base - 260296, 732530000);  // the knob's place in Open303's 314 Hz..2.4 kHz, Q16
    c = c < 0 ? 0 : c > 65535 ? 65535 : c;
    const i32 low = 48298 + multiply(247333, parameters[2]), high = 56646 + multiply(274894, parameters[2]);
    const i32 scaler = low + multiply(high - low, c << 15);
    const i32 envelope_offset = 19293 + multiply(3165, c << 15);
    i32 position = base + multiply(scaler, ((voice.envelope >> 15) - envelope_offset) * 32768)
                 + (voice.sweep >> 15) + (multiply(shared.build, 1288490189) >> 15);  // build: up to 0.6 octave
    position = position < 0 ? 0 : position > (t::cutoff_steps << 13) - 1 ? (t::cutoff_steps << 13) - 1 : position;
    const i32 index = position >> 13, fraction = (position & 8191) << 18;
    const i32 b0x2 = t::cutoff_b0x2[index] + multiply(t::cutoff_b0x2[index + 1] - t::cutoff_b0x2[index], fraction);
    const i32 k16 = t::cutoff_k[index] + multiply(t::cutoff_k[index + 1] - t::cutoff_k[index], fraction);
    const i32 r = knob(t::resonance_skew, parameters[1]) << 16;  // Q31
    const i32 feedback = multiply(k16, r) << 8;                  // k / 128, Q31
    i32 gain = multiply(k16, 126322568) - 65536;                 // k / 17 - 1, Q16
    gain = multiply(gain, r) + 65536;
    gain = (gain + multiply(gain, r)) << 11;                     // 2 g / 32, Q31

    // The VCA: Open303's amp envelope, plus 0.45 of the filter envelope, plus
    // more under accent; Q31 / 8. The short gate closes all of it.
    i32 vca = (voice.amp >> 3) + multiply(voice.envelope, envelope_level) + (accented ? multiply(voice.envelope, accent_level) : 0);
    vca = multiply(vca, voice.gate);
    if (vca < silent) {  // under -84 dB, and it only falls from here
        voice.lane |= lane_quiet;
        for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] = 0;
        return;
    }
    const i32 level = multiply(gain, vca);  // 2 g vca / 256

    // pH corrosion: deeper as the pH falls and over consecutive accents. pH 7 skips it.
    const i32 acid = acidity(parameters[6]);
    const i32 corrosion = acid ? multiply(acid << 16, (1 << 29) + multiply(shared.build, 1610612736)) : 0;  // acid (0.25 + 0.75 build)

    // Separate passes keep each loop small enough for the Blackfin's registers.
    // Oscillator and its 44 Hz highpass, staged in the output buffer.
    const u32 played = (pitch[0] << octave) + static_cast<u32>(offset);
    u32 width = played >> 8;
    if (!width) width = 1;
    const u32 reciprocal = 0x40000000u / width;  // the block's one divide
    u32 phase = voice.phase;
    i32 pre_low = voice.pre_low, glide = offset;
    for (unsigned i = 0; i < opxy::block_frames; i += 2) {
        u32 increment = (pitch[i] << octave) + static_cast<u32>(glide);
        i32 x = saw(phase, increment, width, reciprocal);
        if (square_wave) x = shape(t::square, x, 17);
        pre_low += multiply(x - pre_low, t::input_highpass);
        output[i] = x - pre_low;
        phase += increment;
        increment = (pitch[i + 1] << octave) + static_cast<u32>(glide);
        x = saw(phase, increment, width, reciprocal);
        if (square_wave) x = shape(t::square, x, 17);
        pre_low += multiply(x - pre_low, t::input_highpass);
        output[i + 1] = x - pre_low;
        phase += increment;
        glide += slope;
    }
    voice.phase = phase;
    voice.pre_low = pre_low;

    // The TB-303 ladder, its coefficients ramped from the last block's.
    i32 y1 = voice.y1, y2 = voice.y2, y3 = voice.y3, y4 = voice.y4, feedback_low = voice.feedback_low;
    i32 b2 = voice.b0x2, fb = voice.feedback;
    const i32 db2 = (b0x2 - b2) >> 5, dfb = (feedback - fb) >> 5;
    for (unsigned i = 0; i < opxy::block_frames; i += 2) {
        b2 += db2;
        fb += dfb;
        const i32 b0 = b2 >> 1;
        i32 fed = multiply(y4, fb) * 128;
        feedback_low += multiply(fed - feedback_low, t::feedback_highpass);
        i32 y0 = output[i] - (fed - feedback_low);
        y1 += multiply(b2, y0 - y1 + y2);
        y2 += multiply(b0, y1 - 2 * y2 + y3);
        y3 += multiply(b0, y2 - 2 * y3 + y4);
        y4 += multiply(b0, y3 - 2 * y4);
        output[i] = y4;
        fed = multiply(y4, fb) * 128;
        feedback_low += multiply(fed - feedback_low, t::feedback_highpass);
        y0 = output[i + 1] - (fed - feedback_low);
        y1 += multiply(b2, y0 - y1 + y2);
        y2 += multiply(b0, y1 - 2 * y2 + y3);
        y3 += multiply(b0, y2 - 2 * y3 + y4);
        y4 += multiply(b0, y3 - 2 * y4);
        output[i + 1] = y4;
    }
    voice.y1 = y1; voice.y2 = y2; voice.y3 = y3; voice.y4 = y4;
    voice.feedback_low = feedback_low;
    voice.b0x2 = b0x2;
    voice.feedback = feedback;

    // Output highpass (Open303's highpass2 and notch, merged), the VCA ramp,
    // corrosion, and the half-scale ceiling.
    i32 hx1 = voice.hx1, hx2 = voice.hx2, hy1 = voice.hy1, hy2 = voice.hy2;
    i32 gain_now = voice.level;
    const i32 dgain = (level - gain_now) >> 6;
    const i32 ceiling = 1 << 18;  // 2 g vca y / 256 at this, << 12, is half scale
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const i32 x = output[i];
        const i32 y = (multiply(t::hp_b0, x - 2 * hx1 + hx2) - multiply(t::hp_a1, hy1) - multiply(t::hp_a2, hy2)) * 2;
        hx2 = hx1; hx1 = x; hy2 = hy1; hy1 = y;
        gain_now += dgain;
        i32 m = multiply(y, gain_now);
        if (corrosion) {
            const i32 folded = shape(t::fold, m + multiply(m, acid << 16), 10) >> 8;
            m += multiply(folded - m, corrosion);
        }
        m = m > ceiling ? ceiling : m < -ceiling ? -ceiling : m;
        output[i] = m * 4096;  // 6 dB under Open303 at -12 dB, so its accents fit under half scale
    }
    voice.hx1 = hx1; voice.hx2 = hx2; voice.hy1 = hy1; voice.hy2 = hy2;
    voice.level = level;
    if (newest) {
        shared.increment = (pitch[opxy::block_frames - 1] << octave) + static_cast<u32>(voice.offset);
        shared.envelope = voice.envelope;
    }
}
}
