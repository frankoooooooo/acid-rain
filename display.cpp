#include "display.h"
#include "display_tables.h"

// The face on the floor under water. Each note drops into the water; its rings
// bend the floor's grid and the face drawn on it. Cutoff sets the rings'
// spacing, reso how many each drop carries, env mod their height and decay how
// long they last. Accent turns the eyes to bolts, a slide drags the drop, pH
// corrodes the face's line as the water moves it, and dew sets how often it
// rains: a splash, and the eyes turn to bolts on the accents it brings. The lane
// along the bottom keeps the last sixteen notes.
namespace acidrain {
using opxy::i32;
using opxy::u32;
using opxy::u8;
using namespace opxy::draw;
namespace s = screen;

const i32 second = 1024;                 // Q10 seconds
const u32 drop_count = 8, splash_count = 2;
const i32 lane_left = 60 * unit, lane_step = 24 * unit, lane_y = 205 * unit;
const i32 hold = 461;                    // the eyes hold as bolts for 0.15 s + 0.3 s
const u32 ripple_turn = 154;
const i32 reach = 63 * unit;              // a path's points stay within 64 px of its node (x 2 on screen)
// The water moves a face point at most 4 px and a grid joint 8 px, however
// many drops overlap: the face's lines, 12 px apart or more (9 under a bolt),
// never meet, and neighbouring joints never fold over.
const i32 face_reach_u = 26, face_reach_v = 61;    // 4 px, floor Q10 units
const i32 grid_reach = 8 * unit;                    // Q10 px
const u32 frame_budget = 12;             // grid segments and splash pixels redrawn a frame, about
const u8 lane_used = 1, lane_accented = 2, lane_slid = 4, lane_auto = 8;

enum Shade : u32 { black = 0, grey = 1, line = 2, smile_line = 3, bright = 4, mid = 5, dim = 6, face = 7 };

// Integer square root.
static inline u32 root(u32 x) {
    u32 r = 0, bit = 1u << 30;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= r + bit) {
            x -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}
// e^-x for x in Q10, Q15.
static inline i32 decay(i32 x) {
    const i32 index = x < 0 ? 0 : x >> 7;
    return index >= 64 ? 0 : s::exp_neg[index];
}
// 2^x for x in Q16, Q14 times 2^whole; x within -14..15 octaves.
static inline i32 power(i32 x) {
    i32 whole = x >> 16;
    const i32 position = x & 65535, index = position >> 11, fraction = position & 2047;
    i32 value = s::exp2[index] + ((s::exp2[index + 1] - s::exp2[index]) * fraction >> 11);
    while (whole > 0) { value <<= 1; --whole; }
    while (whole < 0) { value >>= 1; ++whole; }
    return value;
}
static inline u32 mix(u32 x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    return x ^ (x >> 16);
}
static inline i32 snap(i32 value) { return (value + 256) & ~511; }  // whole half-pixels
static inline i32 magnitude(i32 v) { return v < 0 ? -v : v; }
static inline u32 choice(i32 value) { return static_cast<u32>((value < 0 ? 0 : value >> 16) * 3 >> 15); }

// --- Captions ------------------------------------------------------------------
static inline void put(char* out, u32& at, u32 n) {
    char digits[6];
    u32 count = 0;
    do { digits[count++] = static_cast<char>('0' + n % 10); n /= 10; } while (n && count < 6);
    while (count) out[at++] = digits[--count];
}
// Cutoff in Hz: 220, 802, 1.2k, 5.6k.
static inline void hertz(char* out, i32 value) {
    const i32 knob = value < 0 ? 0 : value >> 15;                  // Q16
    const u32 hz = static_cast<u32>(220 * power(static_cast<i32>((static_cast<u32>(knob) * 4782u) >> 10)) >> 14);  // 220 x 2^(4.67 knob)
    u32 at = 0;
    if (hz < 1000) {
        put(out, at, hz);
    } else {
        const u32 tenths = (hz + 50) / 100;
        put(out, at, tenths / 10);
        out[at++] = '.';
        out[at++] = static_cast<char>('0' + tenths % 10);
        out[at++] = 'k';
    }
    out[at] = 0;
}
// Decay in seconds: 0.2s to 2.0s.
static inline void seconds(char* out, i32 value) {
    const i32 knob = value < 0 ? 0 : value >> 15;
    const u32 tenths = static_cast<u32>((2 * power(static_cast<i32>((static_cast<u32>(knob) * 3402u) >> 10)) + 8192) >> 14);
    u32 at = 0;
    put(out, at, tenths / 10);
    out[at++] = '.';
    out[at++] = static_cast<char>('0' + tenths % 10);
    out[at++] = 's';
    out[at] = 0;
}
// pH: 2.0 to 7.0.
static inline void acidity_caption(char* out, i32 value) {
    const u32 tenths = 20 + ((static_cast<u32>(value < 0 ? 0 : value) >> 16) * 50 + 16384) / 32767;
    u32 at = 0;
    put(out, at, tenths / 10);
    out[at++] = '.';
    out[at++] = static_cast<char>('0' + tenths % 10);
    out[at] = 0;
}
static inline void enum_caption(char* out, i32 value) {
    const char* const labels[3] = {"off", "on", "auto"};
    copy(out, header_capacity, labels[choice(value)]);
}

// --- Water ---------------------------------------------------------------------
// The rings this frame, worked out once from the controls.
struct Water {
    i32 front_speed;  // Q10 units per second
    i32 tight;        // a played drop's ring tightness, rad per unit
    i32 rings;        // reso: how many wavelengths a drop's train carries, Q10
    i32 height;       // env mod: shift per unit of tightness and strength, Q10 units (refraction x height)
    i32 fade;         // decay: e-folding rate, Q10 per second
};

static inline Water water(const Input& input) {
    Water w;
    w.front_speed = 1024;  // 1 unit/s: across the face in 0.6 s
    const i32 cutoff = input.slots[0] < 0 ? 0 : input.slots[0] >> 15;  // Q16
    w.tight = (20 * power(2 * cutoff - 52429)) >> 14;                  // 20 x 2^(2 (c - 0.4)): 11..46
    const i32 reso = input.slots[1] < 0 ? 0 : input.slots[1] >> 21;    // Q10
    w.rings = 1024 + 7 * reso;                                          // 1 to 8 wavelengths
    const i32 env = input.slots[2] < 0 ? 0 : input.slots[2] >> 21;
    w.height = 154 + (env * 870 >> 10);                                 // 0.15 .. 1.0 of the tallest ring, Q10
    const i32 knob = input.slots[3] < 0 ? 0 : input.slots[3] >> 15;
    const i32 tau = (205 * power(static_cast<i32>((static_cast<u32>(knob) * 3402u) >> 10))) >> 14;  // 0.2 .. 2.0 s, Q10
    w.fade = 369 * 1024 / (tau < 205 ? 205 : tau);  // 0.36 / tau: 0.9 at the default 0.4 s, 1.8 at 0.2 s, 0.18 at 2.0 s
    return w;
}

// The floor's shift at (u, v), floor Q10 units, from every drop's rings.
static inline void shift(const DisplayState& state, const Water& w, const i32 (&peak)[drop_count],
                         const i32 (&train)[drop_count], const i32 (&inverse)[drop_count], const i32 (&turns)[drop_count],
                         i32 u, i32 v, i32& du, i32& dv) {
    du = dv = 0;
    for (u32 d = 0; d < drop_count; ++d) {
        if (!peak[d]) continue;
        const Drop& drop = state.drops[d];
        const i32 dx = u - drop.u, dy = v - drop.v;
        if (magnitude(dx) > 3072 || magnitude(dy) > 3072) continue;
        const i32 front = drop.age * w.front_speed >> 10;
        const u32 square = static_cast<u32>(dx * dx + dy * dy);
        const i32 r = static_cast<i32>(root(square));
        const i32 behind = front - r;
        if (behind < 0 || behind >= train[d] || r < 16) continue;
        const i32 envelope = decay((drop.age * w.fade >> 10) + (r * 307 >> 10));  // e^-(fade t) e^-(0.3 r), Q15
        const i32 window = s::wave[(behind * inverse[d]) >> 16 & 255];                 // a half-sine over the train
        const i32 phase = s::wave[(((turns[d] * -behind) >> 10) + 64) & 255];          // cos k (r - front)
        const i32 m = ((((peak[d] * envelope) >> 15) * window >> 10) * phase) >> 10;   // Q10 units
        const i32 inv = s::reciprocal[r >> 4 > 255 ? 255 : r >> 4];
        du += (m * ((dx * inv) >> 10)) >> 10;
        dv += (m * ((dy * inv) >> 10)) >> 10;
    }
}

// The water's shift at a point of the face, held to 5 px.
static inline void face_shift(const DisplayState& state, const Water& w, const i32 (&peak)[drop_count],
                              const i32 (&train)[drop_count], const i32 (&inverse)[drop_count], const i32 (&turns)[drop_count],
                              i32 u, i32 v, i32& du, i32& dv) {
    shift(state, w, peak, train, inverse, turns, u, v, du, dv);
    du = clamp(du, -face_reach_u, face_reach_u);
    dv = clamp(dv, -face_reach_v, face_reach_v);
}

// Two passes of a 1-2-1 average over an outline's shifts, so neighbouring
// points move together. A closed outline wraps; an open one keeps its ends.
static inline void smooth(i32 (*d)[2], u32 count, bool closed) {
    for (u32 pass = 0; pass < 2; ++pass)
        for (u32 c = 0; c < 2; ++c) {
            const i32 first = d[0][c];
            i32 previous = closed ? d[count - 1][c] : d[0][c];
            for (u32 i = 0; i < count; ++i) {
                const i32 next = i + 1 < count ? d[i + 1][c] : closed ? first : d[i][c];
                const i32 here = d[i][c];
                d[i][c] = (previous + 2 * here + next) >> 2;
                previous = here;
            }
        }
}

// Takes an outline's new points (Q10 px) when any moved half a pixel or more;
// true when it changed, so its path redraws. Points stay exact (unsnapped), so
// a ribbon's two edges keep their spacing.
static inline bool commit(i32 (*placed)[2], const i32 (*points)[2], u32 count, bool fresh) {
    bool moved = fresh;
    for (u32 i = 0; i < count && !moved; ++i)
        moved = magnitude(points[i][0] - placed[i][0]) >= 512 || magnitude(points[i][1] - placed[i][1]) >= 512;
    if (moved)
        for (u32 i = 0; i < count; ++i) {
            placed[i][0] = points[i][0];
            placed[i][1] = points[i][1];
        }
    return moved;
}

static inline Drop& add_drop(DisplayState& state, i32 u, i32 v, i32 strength, i32 tight) {
    // Take a free slot, or the oldest.
    u32 slot = drop_count;
    i32 oldest = -1;
    for (u32 d = 0; d < drop_count; ++d) {
        if (!state.drops[d].strength) { slot = d; break; }
        if (state.drops[d].age > oldest) { oldest = state.drops[d].age; slot = d; }
    }
    Drop& drop = state.drops[slot];
    drop.u = drop.from_u = u;
    drop.v = drop.from_v = v;
    drop.glide = 0;
    drop.age = 0;
    drop.strength = strength;
    drop.tight = tight;
    return drop;
}

static inline void screen_point(i32 u, i32 v, i32 du, i32 dv, i32& x, i32& y) {
    x = s::centre_x + (u + du) * s::scale;
    y = s::centre_y + (((v + dv) * s::scale) * s::slant >> 10);
}

// --- Reset ---------------------------------------------------------------------
inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    state.started = 0;
}

static inline void restart(DisplayState& state, const Input& input) {
    const PageHeader header = state.header;
    const NoteTracker notes = state.notes;
    state = DisplayState();
    state.header = header;
    state.notes = notes;
    state.started = 1;
    state.track = input.track;
    state.serial = input.note_on_serial;
    state.since_accent = 10 * second;
    state.since_turn = 10 * second;
    for (u32 i = 0; i < splash_count; ++i) state.splashes[i].age = -1;
    for (u32 i = 0; i < 4; ++i) state.knobs[i] = input.slots[i];
    for (u32 i = 0; i < 4; ++i) state.keys[i] = 1;
}

// --- The frame -----------------------------------------------------------------
inline void display(DisplayState& state, const Input& input, Frame& frame) {
    // The header: both pages, captions in real units.
    const char* const titles[8] = {"cutoff", "reso", "env mod", "decay", "accent", "slide", "ph", "dew"};
    char values[8][header_capacity];
    hertz(values[0], input.slots[0]);
    header_amount(values[1], header_capacity, input.slots[1]);
    header_amount(values[2], header_capacity, input.slots[2]);
    seconds(values[3], input.slots[3]);
    enum_caption(values[4], input.slots[4]);
    enum_caption(values[5], input.slots[5]);
    acidity_caption(values[6], input.slots[6]);
    header_amount(values[7], header_capacity, input.slots[7]);
    page_header(state.header, input, frame, titles, values, s::header);

    const NoteEvents events = state.notes.update(input);
    (void)events;
    const bool fresh = !state.started || state.track != input.track;
    if (fresh) restart(state, input);
    const bool still = input.reduced_motion != 0;
    const i32 dt = fresh ? 0 : clamp(input.elapsed, 0, 205);
    const bool held = input.held_notes != 0 && input.held_notes <= 128;
    const i32 acid = acidity(input.slots[6]) >> 5;  // Q10

    // Note starts: the same humidity as the engine decides the rain.
    u32 started = fresh ? 0 : input.note_on_serial - state.serial;
    state.serial = input.note_on_serial;
    if (started > 4) started = 4;
    state.since_note += dt;
    for (u32 n = 0; n < started; ++n) {
        const u32 serial = input.note_on_serial - (started - 1 - n);
        const u32 blocks = static_cast<u32>(state.since_note) * 689u >> 10;  // 44100 / 64 blocks a second
        state.since_note = 0;
        const bool rained = rain_note(state.rain, blocks, input.slots[6], input.slots[7]);
        const u32 a = choice(input.slots[4]), sl = choice(input.slots[5]);
        const bool accented = a == 1 || (a == 2 && rained);
        // Where it lands: a hash of the note counter, inside the face.
        const u32 h = mix(serial * 0x9e3779b9u + 17);
        const i32 angle = static_cast<i32>(h & 255), radius = static_cast<i32>((h >> 8 & 1023) * 440 >> 10);
        const i32 u = (s::wave[(angle + 64) & 255] * radius) >> 10, v = (s::wave[angle] * radius) >> 10;
        if (!still) {
            Drop& drop = add_drop(state, u, v, 1024, 0);  // every note alike: accents show in the eyes and splash
            if (sl == 1 && n + state.head) {  // a slide drags the drop from the last one
                drop.from_u = state.last_u;
                drop.from_v = state.last_v;
                drop.glide = 154;
            }
        }
        state.last_u = u;
        state.last_v = v;
        state.trickle = 0;
        if (accented) state.since_accent = 0;
        if (rained || a == 1) {  // it rains, or accent is locked on
            // A cluster of rain splashes once: a new splash waits until the last is 0.4 s old.
            const Splash& previous = state.splashes[(state.next_splash + splash_count - 1) % splash_count];
            if (!still && (previous.age < 0 || previous.age >= 410)) {
                Splash& splash = state.splashes[state.next_splash++ % splash_count];
                splash.u = u;
                splash.v = v;
                splash.age = 0;
                splash.strength = accented ? 1024 : 768;
                splash.seed = mix(serial);
                splash.landed = 0;
            }
        }
        // The lane: accents are exact; auto slides depend on pitch, so they show as auto.
        u8 cell = lane_used;
        if (accented) cell |= lane_accented;
        if (sl == 1) cell |= lane_slid;
        if (sl == 2) cell |= lane_auto;
        state.lane[state.head % 16] = cell;
        ++state.head;
    }

    // A held note keeps a slow trickle going: one small ripple every 0.4 s.
    if (held && !still) {
        state.trickle += dt;
        if (state.trickle >= 410) {
            state.trickle -= 410;
            add_drop(state, state.last_u, state.last_v, 360, 0);
        }
    }
    // Turning a sound control drops one quiet ripple at the face's centre.
    state.since_turn += dt;
    bool turned = false;
    for (u32 i = 0; i < 4; ++i) {
        turned = turned || state.knobs[i] != input.slots[i];
        state.knobs[i] = input.slots[i];
    }
    if (turned && !fresh && !still && state.since_turn >= static_cast<i32>(ripple_turn)) {
        state.since_turn = 0;
        add_drop(state, 0, 0, 614, 0);
    }

    // Age the drops and splashes.
    const Water w = water(input);
    for (u32 d = 0; d < drop_count; ++d) {
        Drop& drop = state.drops[d];
        if (!drop.strength) continue;
        drop.age += dt;
        // Gone under 8%, or once its last ring has passed the centre grid.
        const i32 tight = drop.tight ? drop.tight : w.tight;
        const i32 last = (drop.age * w.front_speed >> 10) - 6434 * w.rings / (tight * 1024);
        if (still || (drop.age * w.fade >> 10) > 2560 || last > 1700) drop.strength = 0;
        if (drop.glide > 0) drop.glide = drop.glide > dt ? drop.glide - dt : 0;
    }
    i32 peak[drop_count], train[drop_count], inverse[drop_count], turns[drop_count];
    for (u32 d = 0; d < drop_count; ++d) {
        Drop& drop = state.drops[d];
        peak[d] = 0;
        if (!drop.strength) continue;
        const i32 tight = drop.tight ? drop.tight : w.tight;
        // Ring height: refraction x amplitude x tightness, capped at 12 px; then env mod.
        i32 p = (4 * tight * drop.strength) >> 10;
        if (p > 77) p = 77;
        peak[d] = (p * w.height) >> 10;
        train[d] = 6434 * w.rings / (tight * 1024);  // the rings' wavelength 2 pi / k, times their count
        if (train[d] < 64) train[d] = 64;
        inverse[d] = (128 << 16) / train[d];
        turns[d] = tight * 41;  // k / 2 pi in 1/256 turns, Q10 of a unit
    }
    // A slide: the glide's drop is drawn on its way from the last one.
    Drop saved[drop_count];
    for (u32 d = 0; d < drop_count; ++d) {
        saved[d] = state.drops[d];
        Drop& drop = state.drops[d];
        if (drop.strength && drop.glide > 0) {
            const i32 left = (drop.glide * drop.glide) / 154;  // Q10 s, eases out
            drop.u += ((drop.from_u - drop.u) * left) / 154;
            drop.v += ((drop.from_v - drop.v) * left) / 154;
        }
    }

    // The face's four outlines share their pivot: the face's centre, which
    // stays put while the water moves their points.
    i32 du, dv;
    const i32 px = snap(s::centre_x), py = snap(s::centre_y);

    // The border: refracted, and pitted where the water moves it as the pH
    // falls (up to 3 px). At rest it is smooth. It moves only across its line
    // (radially), never along it, and its shift is smoothed over neighbouring
    // points, so tight rings can't bunch the points up.
    i32 points[64][2], shifts[64][2];
    for (u32 i = 0; i < 64; ++i) {
        face_shift(state, w, peak, train, inverse, turns, s::border_u[i], s::border_v[i], du, dv);
        const i32 across = ((du * s::border_u[i] + dv * s::border_v[i]) * 1708) >> 20;  // along the radius, / 0.6 units
        shifts[i][0] = (across * s::border_u[i] * 1708) >> 20;
        shifts[i][1] = (across * s::border_v[i] * 1708) >> 20;
    }
    smooth(shifts, 64, true);
    for (u32 i = 0; i < 64; ++i) {
        du = shifts[i][0];
        dv = shifts[i][1];
        const i32 moving = clamp(magnitude(du) + magnitude(dv), 0, 32) * 32;  // Q10: 1 once it moves about 4 px
        const i32 pit = (((s::pits[i] * acid) >> 10) * moving) >> 10;  // Q10, -1..1
        const i32 u = s::border_u[i] + du + ((s::border_u[i] * pit) >> 10) * 3 / 99;
        const i32 v = s::border_v[i] + dv + ((s::border_v[i] * pit) >> 10) * 3 / 99;
        screen_point(u, v, 0, 0, points[i][0], points[i][1]);
    }
    if (commit(state.border, points, 64, fresh)) {
        state.keys[0] = state.keys[0] % 0xfffffff0u + 1;
        frame.paths[0].key = state.keys[0];
        for (u32 i = 0; i < 64; ++i) {
            frame.paths[0].points[i].x = clamp((state.border[i][0] - px) >> 1, -reach, reach);
            frame.paths[0].points[i].y = clamp((state.border[i][1] - py) >> 1, -reach, reach);
        }
    }
    // The smile: a thin ribbon around its refracted, smoothed centre line.
    for (u32 i = 0; i < 32; ++i)
        face_shift(state, w, peak, train, inverse, turns, s::smile_u[i], s::smile_v[i], shifts[i][0], shifts[i][1]);
    smooth(shifts, 32, false);
    for (u32 i = 0; i < 32; ++i)
        screen_point(s::smile_u[i], s::smile_v[i], shifts[i][0], shifts[i][1], points[i][0], points[i][1]);
    if (commit(state.smile, points, 32, fresh)) {
        state.keys[1] = state.keys[1] % 0xfffffff0u + 1;
        frame.paths[1].key = state.keys[1];
        for (u32 i = 0; i < 32; ++i) {
            const i32 x = clamp((state.smile[i][0] - px) >> 1, -reach, reach), y = clamp((state.smile[i][1] - py) >> 1, -reach, reach);
            frame.paths[1].points[i].x = x + s::smile_dx[i];
            frame.paths[1].points[i].y = y + s::smile_dy[i];
            frame.paths[1].points[63 - i].x = x - s::smile_dx[i];
            frame.paths[1].points[63 - i].y = y - s::smile_dy[i];
        }
    }

    // The eyes: outlines of tall ovals that morph into bolts on an accent,
    // easing over 0.15 s, hold, then ease back.
    state.since_accent += dt;
    const i32 target = state.since_accent < hold ? unit : 0;
    if (still || fresh) state.bolt = target;
    else state.bolt += ((target - state.bolt) * (dt > 154 ? 1024 : dt * 1024 / 51 > 1024 ? 1024 : dt * 1024 / 51)) >> 10;
    if (magnitude(target - state.bolt) < 16) state.bolt = target;
    for (u32 e = 0; e < 2; ++e) {
        i32 rest[64][2];
        for (u32 i = 0; i < 64; ++i) {
            const u32 k = e * 64 + i;
            rest[i][0] = s::eye_u[e] + s::oval_u[k] + (((s::bolt_u[k] - s::oval_u[k]) * state.bolt) >> 10);
            rest[i][1] = s::eye_v[e] + s::oval_v[k] + (((s::bolt_v[k] - s::oval_v[k]) * state.bolt) >> 10);
            face_shift(state, w, peak, train, inverse, turns, rest[i][0], rest[i][1], shifts[i][0], shifts[i][1]);
        }
        smooth(shifts, 64, true);
        for (u32 i = 0; i < 64; ++i)
            screen_point(rest[i][0], rest[i][1], shifts[i][0], shifts[i][1], points[i][0], points[i][1]);
        if (commit(state.eyes[e], points, 64, fresh)) {
            state.keys[2 + e] = state.keys[2 + e] % 0xfffffff0u + 1;
            frame.paths[2 + e].key = state.keys[2 + e];
            for (u32 i = 0; i < 64; ++i) {
                frame.paths[2 + e].points[i].x = clamp((state.eyes[e][i][0] - px) >> 1, -reach, reach);
                frame.paths[2 + e].points[i].y = clamp((state.eyes[e][i][1] - py) >> 1, -reach, reach);
            }
        }
    }
    if (fresh) {
        place(frame, 0, px, py, 2 * unit, line, stroke);
        place(frame, 1, px, py, 2 * unit, smile_line, stroke);
        for (u32 n = 2; n < 4; ++n) place(frame, n, px, py, 2 * unit, face, stroke);  // the eyes' own weight
    }

    // The grid's centre: each joint refracted, each segment turned and scaled
    // between its joints. A joint moves only by half a pixel or more.
    // About `frame_budget` segments redraw a frame: the joints furthest
    // from where the water puts them go first, the rest catch up after.
    bool joint_moved[61];
    i32 want_x[61], want_y[61], error[61];
    for (u32 j = 0; j < s::joint_count; ++j) {
        i32 x = s::joint_x[j], y = s::joint_y[j];
        if (!s::joint_rim[j]) {
            shift(state, w, peak, train, inverse, turns, s::joint_u[j], s::joint_v[j], du, dv);
            x += clamp(du * s::scale, -grid_reach, grid_reach);
            y += clamp((dv * s::scale) * s::slant >> 10, -grid_reach, grid_reach);
        }
        want_x[j] = x;
        want_y[j] = y;
        const i32 ex = magnitude(x - state.joint_x[j]), ey = magnitude(y - state.joint_y[j]);
        error[j] = ex > ey ? ex : ey;
        joint_moved[j] = fresh;
    }
    if (fresh) {
        for (u32 j = 0; j < s::joint_count; ++j) {
            state.joint_x[j] = snap(want_x[j]);
            state.joint_y[j] = snap(want_y[j]);
        }
    } else {
        // The splash spends first; the grid gets the rest, at least four.
        const u32 budget = state.splash_cost + 4 < frame_budget ? frame_budget - state.splash_cost : 4;
        u32 spent = 0;
        while (spent < budget) {
            u32 worst = s::joint_count;
            i32 most = 511;
            for (u32 j = 0; j < s::joint_count; ++j)
                if (!joint_moved[j] && error[j] > most) { most = error[j]; worst = j; }
            if (worst == s::joint_count) break;
            u32 cost = 0;
            for (u32 g = 0; g < s::segment_count; ++g) {
                const u32 a = s::segment_from[g], b = s::segment_to[g];
                if ((a == worst && !joint_moved[b]) || (b == worst && !joint_moved[a])) ++cost;
            }
            if (spent && spent + cost > budget) break;  // it waits for the next frame
            spent += cost;
            joint_moved[worst] = true;
            state.joint_x[worst] = snap(want_x[worst]);
            state.joint_y[worst] = snap(want_y[worst]);
        }
    }
    for (u32 g = 0; g < s::segment_count; ++g) {
        const u32 a = s::segment_from[g], b = s::segment_to[g];
        if (!joint_moved[a] && !joint_moved[b]) continue;
        const i32 dx = state.joint_x[b] - state.joint_x[a], dy = state.joint_y[b] - state.joint_y[a];
        const i32 length = static_cast<i32>(root(static_cast<u32>((dx >> 4) * (dx >> 4) + (dy >> 4) * (dy >> 4)))) << 4;
        const i32 safe = length < unit ? unit : length;
        place(frame, s::grid + g, state.joint_x[a], state.joint_y[a], clamp(safe / s::segment, 64, 2 * unit), grey, stroke);
        const Turn along = {dx * unit / safe, dy * unit / safe};
        turn(frame, s::grid + g, along);
    }
    for (u32 d = 0; d < drop_count; ++d) state.drops[d] = saved[d];

    // Splashes: a crown of pixels spreading over 0.35 s, and droplets thrown
    // up and out that land 0.5 to 0.8 s later, each starting a small ripple.
    state.splash_cost = 0;
    for (u32 k = 0; k < splash_count; ++k) {
        Splash& splash = state.splashes[k];
        const u32 first = s::splash + (s::crown + s::droplets) * k;
        if (splash.age < 0) {
            if (fresh) for (u32 i = 0; i < s::crown + s::droplets; ++i) hide(frame, first + i);
            continue;
        }
        splash.age += dt;
        const i32 t = splash.age;
        // The crown: radius 0.03 to 0.13 units, easing out.
        for (u32 i = 0; i < s::crown; ++i) {
            if (t > 461) { hide(frame, first + i); continue; }
            const i32 ease = 1024 - (decay(t * 10) >> 5);  // 1 - e^-(t / 0.1), Q10
            const i32 radius = 31 + (102 * ease >> 10);
            const u32 angle = (i * 256 / s::crown + (splash.seed >> (i * 3) & 7)) & 255;
            const i32 u = splash.u + ((s::wave[(angle + 64) & 255] * radius) >> 10);
            const i32 v = splash.v + ((s::wave[angle] * radius) >> 10);
            i32 x, y;
            screen_point(u, v, 0, 0, x, y);
            place(frame, first + i, snap(x), snap(y), unit, bright, fill_and_stroke);
            if (t < 205) ++state.splash_cost;  // the crown spreads for 0.2 s, then holds
        }
        // The droplets: out at 0.18 to 0.44 units/s, up at 5.5 to 9.5, gravity 24.
        bool flying = t <= 461;
        for (u32 i = 0; i < s::droplets; ++i) {
            const u32 node = first + s::crown + i;
            if (splash.landed >> i & 1) continue;
            const u32 h = mix(splash.seed + i * 0x9e37u);
            const u32 angle = (i * 256 / s::droplets + (h & 15)) & 255;
            const i32 out = ((184 + static_cast<i32>(h >> 4 & 255)) * splash.strength) >> 10;  // Q10 units/s
            const i32 up = 5632 + static_cast<i32>(h >> 12 & 4095);                              // Q10 per second
            const i32 flight = up * 1024 / (12 * 1024);                                          // Q10 s: up / 12
            if (t >= flight) {
                splash.landed |= 1u << i;
                hide(frame, node);
                const i32 travel = out * flight >> 10;
                if (!still) add_drop(state, splash.u + ((s::wave[(angle + 64) & 255] * travel) >> 10),
                                     splash.v + ((s::wave[angle] * travel) >> 10), 123, 40);
                continue;
            }
            flying = true;
            const i32 travel = out * t >> 10;
            const i32 height = (up * t >> 10) - (12 * t * t >> 10);  // Q10 units
            const i32 u = splash.u + ((s::wave[(angle + 64) & 255] * travel) >> 10);
            const i32 v = splash.v + ((s::wave[angle] * travel) >> 10);
            i32 x, y;
            screen_point(u, v, 0, 0, x, y);
            place(frame, node, snap(x), snap(y - height * 16), unit, bright, fill_and_stroke);
            ++state.splash_cost;
        }
        if (!flying) {
            splash.age = -1;
            for (u32 i = 0; i < s::crown + s::droplets; ++i) hide(frame, first + i);
        }
    }

    // The lane: a fixed ring of sixteen cells, written in turn, with a cursor
    // under the newest. Only the written cell and the cursor change.
    if (fresh || started) {
        const u32 count = fresh ? 16 : started;
        for (u32 n = 0; n < count; ++n) {
            const u32 i = fresh ? n : (state.head - started + n) % 16;
            const u8 cell = state.lane[i];
            const i32 x = lane_left + static_cast<i32>(i) * lane_step;
            if (cell & lane_used) place(frame, s::lane + i, x, lane_y, cell & lane_accented ? 2 * unit : unit,
                                        cell & lane_accented ? bright : mid, fill_and_stroke);
            else hide(frame, s::lane + i);
            if (cell & lane_slid) place(frame, s::lane + 16 + i, x - 2 * unit, lane_y, unit, bright, fill_and_stroke);
            else hide(frame, s::lane + 16 + i);
            if (cell & lane_auto) place(frame, s::lane + 32 + i, x, lane_y - 10 * unit, unit, dim, fill_and_stroke);
            else hide(frame, s::lane + 32 + i);
        }
        if (state.head) place(frame, s::lane + 48, lane_left + static_cast<i32>((state.head - 1) % 16) * lane_step,
                              lane_y + 10 * unit, unit, mid, fill_and_stroke);
        else hide(frame, s::lane + 48);
    }
}
}
