#pragma once
#include <opxy/page_header.hpp>
#include "rain.h"

namespace acidrain {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<2, 64, 213, 16, 8>;

// A drop on the water: its centre (floor Q10 units), where a slide drags it
// from, its age (Q10 s), strength (Q10) and ring tightness (0: the cutoff's).
struct Drop { opxy::i32 u, v, from_u, from_v, glide, age, strength, tight; };
// A splash: where, its age (Q10 s, negative when none), strength, a seed for
// its droplets, and which droplets have landed.
struct Splash { opxy::i32 u, v, age, strength; opxy::u32 seed, landed; };

struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    opxy::u32 started, track, serial;
    opxy::i32 since_note;      // Q10 s since the last note start: the humidity's clock
    opxy::i32 trickle;         // Q10 s since the held note's last trickle
    opxy::i32 since_accent;    // Q10 s since the eyes turned to bolts
    opxy::i32 bolt;            // how far the eyes have turned, Q10
    opxy::i32 since_turn;      // Q10 s since the last preview ripple
    opxy::i32 fall;            // Q10 s into the beads' fall into a splash, negative when none
    opxy::i32 fall_u, fall_v;  // where they fall
    opxy::u32 beads, fallen;   // beads shown, and how many are falling
    opxy::i32 last_u, last_v;  // the newest note's drop
    opxy::i32 knobs[4];        // the sound controls last frame
    Rain rain;
    Drop drops[8];
    Splash splashes[2];
    opxy::u32 next_drop, next_splash, head;
    opxy::u32 splash_cost;     // splash pixels moved last frame
    opxy::u8 lane[16];
    opxy::i32 joint_x[61], joint_y[61];         // as last placed, Q10 px
    opxy::i32 border[64][2], smile[32][2];      // as last placed, Q10 px
    opxy::u32 border_key, smile_key;
};
}
