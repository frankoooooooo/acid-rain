#pragma once
#include "rain_tables.h"
#include <opxy/q31.hpp>

// The humidity model, shared by the engine and the screen so the screen draws
// the accents and slides the audio plays. It depends only on the time between
// note starts, pH and dew: never on pitch, which the screen can't see. (The
// engine counts time only while it renders, so after a rest the two can differ.)
namespace acidrain {
// Two stores of humidity and the drizzle store (Q24), the rain accents still to
// fall, which slide the octave jump takes next, and whether the last note slid.
struct Rain { opxy::i32 fast, slow, drizzle; opxy::u32 pending, toggle, slid; };
// What one note start decides for the notes on auto.
struct RainNote { bool accent, slide, octave, short_gate; };

const opxy::i32 humidity_per_note = 1 << 22;  // 0.25 into the fast store and the drizzle
const opxy::i32 soak_per_note = 671089;       // 0.04 into the slow one, which builds over bars

// Acid from the pH control: 0 at pH 7.0 (fully clockwise), Q15 32767 at pH 2.0.
static inline opxy::i32 acidity(opxy::i32 ph) { return 32767 - (ph < 0 ? 0 : ph >> 16); }

// A Q31 knob read across a 33-entry table.
static inline opxy::i32 across(const opxy::i32 (&t)[33], opxy::i32 knob) {
    const opxy::i32 position = knob < 0 ? 0 : knob >> 16;  // Q15
    const opxy::i32 index = position >> 10, fraction = (position & 1023) << 21;
    return index >= 32 ? t[32] : t[index] + opxy::q31::multiply(t[index + 1] - t[index], fraction);
}

// One note start, `blocks` 64-sample blocks after the previous one.
// - accent: it rains on this note (dew sets how often; 0 never rains).
// - slide: the drizzle crossed the pH's threshold, so this note glides in;
//   dense playing slides more and gaps dry it out, rarely at pH 7.
// - octave: under pH 4.5, every other slide also jumps an octave.
// - short_gate: under pH 5.5, the note after a slide closes early.
static inline RainNote rain_note(Rain& rain, opxy::u32 blocks, opxy::i32 ph, opxy::i32 dew) {
    using opxy::q31::multiply;
    RainNote note = {false, false, false, false};
    if (blocks > 8191) blocks = 8191;  // twelve seconds: the fast store is long dry
    for (opxy::u32 b = 0; b < 13; ++b)
        if (blocks >> b & 1) {
            rain.fast = multiply(rain.fast, rain_tables::fast_evaporation[b]);
            rain.slow = multiply(rain.slow, rain_tables::slow_evaporation[b]);
            rain.drizzle = multiply(rain.drizzle, rain_tables::fast_evaporation[b]);
        }
    rain.fast += humidity_per_note;
    rain.slow += soak_per_note;
    rain.drizzle += humidity_per_note;

    const opxy::i32 acid = acidity(ph);
    const opxy::i32 drizzle = across(rain_tables::drizzle, ph);
    if (rain.drizzle >= drizzle) {
        rain.drizzle -= drizzle;
        note.slide = true;
        if (acid >= 16384) note.octave = (rain.toggle ^= 1) != 0;
    } else {
        note.short_gate = rain.slid && acid >= 9830;
    }
    rain.slid = note.slide;

    if (rain.pending) {
        --rain.pending;
        note.accent = true;
        return note;
    }
    if (dew <= 0 || !(dew >> 16)) return note;  // dew 0: it never rains
    const opxy::i32 threshold = across(rain_tables::threshold, dew);
    if (rain.fast + rain.slow < threshold) return note;
    // It rains: one accent, two in a row once the pH is under 4.5.
    rain.pending = acid >= 16384 ? 1 : 0;
    // The rain spends a threshold's worth of water, the fast store's first.
    rain.fast -= threshold;
    if (rain.fast < 0) {
        rain.slow += rain.fast;
        rain.fast = 0;
        if (rain.slow < 0) rain.slow = 0;
    }
    note.accent = true;
    return note;
}
}
