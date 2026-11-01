#pragma once
#include <opxy/audio.hpp>
#include "rain.h"

namespace acidrain {
// Audio inside the voice is Q24 (1.0 = 1 << 24), so the resonant ladder has
// 128x headroom; tests/sound.cpp proves the corners stay inside it.
struct Voice {
    opxy::u32 phase;                 // free-running saw phase
    opxy::i32 offset;                // slide: the glide's distance from the played pitch, as an increment
    opxy::i32 y1, y2, y3, y4;        // TB-303 ladder stages
    opxy::i32 feedback_low, pre_low; // 150 Hz feedback and 44 Hz input highpasses (their lowpass halves)
    opxy::i32 hx1, hx2, hy1, hy2;    // the merged output highpass
    opxy::i32 envelope;              // filter envelope, Q31
    opxy::i32 sweep;                 // its 15 ms smoothed copy while accented: the accent sweep, Q31
    opxy::i32 amp;                   // amp envelope, Q31
    opxy::i32 gate;                  // 1 until a short gate closes, Q31
    opxy::i32 b0x2, feedback, level; // last block's coefficients, the start of this block's ramps
    opxy::u32 origin;                // the previous note's increment, for a slide locked a block late
    opxy::i32 origin_envelope;       // and its envelope
    opxy::u32 age;                   // blocks rendered
    opxy::u32 lane;                  // this note's lane: the bits below
    opxy::u32 hush;                  // blocks in a row ended under -84 dB
};
// A note's lane, latched on its first block (a lock can still arrive on its second).
const opxy::u32 lane_accent = 1, lane_slide = 2, lane_octave = 4, lane_short = 8, lane_quiet = 16;
const opxy::u32 lane_accent_locked = 32, lane_slide_locked = 64;  // set by a lock, not the rain lane

// State every voice shares. It spans every track that plays the machine, so
// slide and rain follow one line reliably only on one track (README).
struct Shared {
    opxy::u32 increment;       // the newest note's last increment, where a slide starts
    opxy::i32 envelope;        // and its filter envelope, which a slide carries on
    opxy::u32 clock;           // blocks rendered by the newest note
    opxy::u32 ticker;          // the owner advancing the clock: the newest note
    opxy::u32 note_clock;      // the clock at the last note start
    opxy::i32 build;           // accent build-up over consecutive accents (Open303's accent RC), Q31
    Rain rain;                 // humidity, which decides the rain accents
    opxy::u32 history[2];      // the previous two notes' semitones, for the rain lane's hash
    opxy::u32 notes;           // notes started: odd ones are off-beats to the rain lane
};
}
