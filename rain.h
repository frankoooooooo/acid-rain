#pragma once
#include "rain_tables.h"
#include <opxy/q31.hpp>

// The humidity model, shared by the engine and the screen so the screen's
// splashes fall on the notes the audio accents. It depends only on the time
// between note starts, pH and dew: never on pitch, which the screen can't see.
namespace acidrain {
// Two stores of humidity, Q24, and the rain accents still to fall.
struct Rain { opxy::i32 fast, slow; opxy::u32 pending; };

const opxy::i32 humidity_per_note = 1 << 22;  // 0.25 into the fast store
const opxy::i32 soak_per_note = 671089;       // 0.04 into the slow one, which builds over bars

// Acid from the pH control: 0 at pH 7.0 (fully clockwise), Q15 32767 at pH 2.0.
static inline opxy::i32 acidity(opxy::i32 ph) { return 32767 - (ph < 0 ? 0 : ph >> 16); }

// One note start, `blocks` 64-sample blocks after the previous one. True when
// it rains on this note: the note takes a rain accent if accent is on auto.
static inline bool rain_note(Rain& rain, opxy::u32 blocks, opxy::i32 ph, opxy::i32 dew) {
    using opxy::q31::multiply;
    if (blocks > 8191) blocks = 8191;  // twelve seconds: the fast store is long dry
    for (opxy::u32 b = 0; b < 13; ++b)
        if (blocks >> b & 1) {
            rain.fast = multiply(rain.fast, rain_tables::fast_evaporation[b]);
            rain.slow = multiply(rain.slow, rain_tables::slow_evaporation[b]);
        }
    rain.fast += humidity_per_note;
    rain.slow += soak_per_note;
    if (rain.pending) {
        --rain.pending;
        return true;
    }
    const opxy::i32 position = dew < 0 ? 0 : dew >> 16;  // Q15
    if (!position) return false;                        // dew 0: it never rains
    const opxy::i32 index = position >> 10, fraction = (position & 1023) << 21;
    const opxy::i32* const t = rain_tables::threshold;
    const opxy::i32 threshold = index >= 32 ? t[32] : t[index] + multiply(t[index + 1] - t[index], fraction);
    if (rain.fast + rain.slow < threshold) return false;
    // It rains: one accent, up to three in a cluster as the pH falls.
    rain.pending = static_cast<opxy::u32>(acidity(ph) * 3 >> 15);  // 0..2 more
    // The rain spends a threshold's worth of water, the fast store's first.
    rain.fast -= threshold;
    if (rain.fast < 0) {
        rain.slow += rain.fast;
        rain.fast = 0;
        if (rain.slow < 0) rain.slow = 0;
    }
    return true;
}
}
