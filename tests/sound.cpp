// Acid Rain's sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>. Build with -fsanitize=undefined too:
// an overflow in the ladder is undefined behaviour the sanitizer reports.
#include "../engine.cpp"
#include <cstdio>

using acidrain::Voice;
using acidrain::Shared;
using opxy::i32;
using opxy::u32;

static int failures = 0;
static void check(const char* name, bool ok) {
    if (!ok) {
        std::printf("FAIL %s\n", name);
        ++failures;
    }
}

const i32 full = 0x7fffffff;
const i32 off = 0, on = full / 2, automatic = full;   // enum choices
const u32 a1 = 1217998u;     // 12.5 Hz, below the lowest key
const u32 a2 = 4870000u;     // about 50 Hz
const u32 a4 = 42852281u;    // 440 Hz
const u32 c7 = 204800000u;   // about 2.1 kHz

struct Line {
    Voice voices[2];
    Shared shared;
    u32 owner;
    i32 peak, state, last;  // loudest output, largest ladder state (y1..y3) and largest y4 seen
    bool bounded;
    Line() : shared(), owner(0), peak(0), state(0), last(0), bounded(true) {
        for (u32 v = 0; v < 2; ++v) acidrain::initialize(voices[v], shared, v + 1);
    }
    Voice& note() { return voices[(owner + 1) % 2]; }
    // Plays a note for `blocks`, like stock: the old voice ends as the new one starts.
    void play(const i32 (&parameters)[8], u32 increment, u32 blocks, i32* out = 0) {
        const u32 index = owner % 2;
        acidrain::destroy(voices[index], shared, index + 1);  // the voice this note takes
        owner = index + 1;
        acidrain::start(voices[index], shared, owner);
        u32 pitch[opxy::block_frames];
        i32 output[opxy::block_frames];
        for (u32 i = 0; i < opxy::block_frames; ++i) pitch[i] = increment;
        for (u32 b = 0; b < blocks; ++b) {
            acidrain::render(voices[index], shared, owner, output, parameters, pitch);
            const Voice& v = voices[index];
            for (u32 i = 0; i < opxy::block_frames; ++i) {
                const i32 a = output[i] < 0 ? -output[i] : output[i];
                peak = a > peak ? a : peak;
                bounded = bounded && output[i] >= -1073741824 && output[i] <= 1073741824;
                if (out) out[b * opxy::block_frames + i] = output[i];
            }
            const i32 s[3] = {v.y1, v.y2, v.y3};
            for (u32 j = 0; j < 3; ++j) {
                const i32 a = s[j] < 0 ? -s[j] : s[j];
                state = a > state ? a : state;
            }
            const i32 a = v.y4 < 0 ? -v.y4 : v.y4;
            last = a > last ? a : last;
        }
    }
};

// Energy of a rendered stretch, in units of 2^-24 (enough to compare).
static long long energy(const i32* x, u32 n) {
    long long sum = 0;
    for (u32 i = 0; i < n; ++i) sum += static_cast<long long>(x[i] >> 12) * (x[i] >> 12) >> 12;
    return sum;
}
// Zero crossings of the first difference: a crude brightness.
static u32 brightness(const i32* x, u32 n) {
    u32 count = 0;
    for (u32 i = 2; i < n; ++i) count += ((x[i] - x[i - 1]) ^ (x[i - 1] - x[i - 2])) < 0;
    return count;
}

int main() {
    // Headroom: every corner of the four sound controls, accents locked on and
    // stacking every four blocks, pH 2 and 7, at the lowest and highest notes.
    // This ladder form carries a large y1 (about 26 at full resonance and
    // cutoff): it must stay under half of Q24's 128x headroom, since the
    // updates add two states. y4 feeds back through k * r <= 17, which
    // overflows int32 beyond 7.5, so it must stay under 4. Output: half scale.
    i32 worst = 0, worst_y4 = 0;
    for (u32 corner = 0; corner < 16; ++corner)
        for (u32 acid = 0; acid < 2; ++acid) {
            const u32 pitches[] = {a1, a2, a4, c7};
            for (u32 p = 0; p < 4; ++p) {
                i32 parameters[8] = {0, 0, 0, 0, on, off, acid ? 0 : full, full};
                for (u32 j = 0; j < 4; ++j) parameters[j] = (corner >> j & 1) ? full : 0;
                Line line;
                for (u32 n = 0; n < 24; ++n) line.play(parameters, pitches[p], 4);
                line.play(parameters, pitches[p], 400);
                check("corner bounded at half scale", line.bounded);
                check("ladder y1..y3 inside 64", line.state < (64 << 24));
                check("ladder y4 inside 4", line.last < (4 << 24));
                worst = line.state > worst ? line.state : worst;
                worst_y4 = line.last > worst_y4 ? line.last : worst_y4;
            }
        }
    std::printf("largest ladder state y1..y3 %.1f (limit 64), y4 %.2f (limit 4)\n", worst / 16777216.0, worst_y4 / 16777216.0);

    // The default patch sounds; a closed filter is quieter and duller.
    const i32 init[8] = {858993459, 858993459, 1073741824, 646464646, off, off, full, 0};
    {
        static i32 open[345 * 64], shut[345 * 64];
        Line a, b;
        a.play(init, a2 * 2, 345, open);
        i32 closed[8] = {0, 0, 0, 0, off, off, full, 0};
        b.play(closed, a2 * 2, 345, shut);
        check("default sounds", a.peak > (1 << 26));
        check("closed filter is quieter", energy(shut, 345 * 64) < energy(open, 345 * 64));
        check("closed filter is duller", brightness(shut, 8000) < brightness(open, 8000));
        std::printf("default peak %.3f of full scale\n", a.peak / 2147483648.0);
    }

    // Accent: louder and brighter than the plain note, and accents in a row build.
    {
        static i32 plain[64 * 64], loud[64 * 64];
        Line a, b;
        a.play(init, a2 * 2, 64, plain);
        i32 accented[8] = {init[0], init[1], init[2], init[3], on, off, full, 0};
        b.play(accented, a2 * 2, 64, loud);
        check("accent is louder", energy(loud, 4096) > energy(plain, 4096) * 3 / 2);
        Line c;
        c.play(accented, a2 * 2, 8);
        const i32 first = c.shared.build;
        for (u32 n = 0; n < 6; ++n) c.play(accented, a2 * 2, 8);
        check("accents build up", c.shared.build > first + first / 2);
    }

    // Slide locked on: the note starts at the previous pitch, arrives within
    // about 60 ms x 4, and its filter envelope carries on instead of retriggering.
    {
        Line line;
        const i32 plain[8] = {init[0], init[1], init[2], init[3], off, off, full, 0};
        i32 sliding[8] = {init[0], init[1], init[2], init[3], off, on, full, 0};
        line.play(plain, a2, 40);
        const i32 envelope = line.shared.envelope;
        line.play(sliding, a2 * 2, 1);
        Voice& v = line.voices[(line.owner - 1) % 2];
        check("slide starts at the previous pitch", static_cast<u32>(static_cast<i32>(a2 * 2) + v.offset) < a2 + a2 / 4);
        check("slide doesn't retrigger", v.envelope <= envelope);
        Line glide;
        glide.play(plain, a2, 40);
        glide.play(sliding, a2 * 2, 170);  // 250 ms
        const Voice& g = glide.voices[(glide.owner - 1) % 2];
        check("slide arrives", g.offset > -static_cast<i32>(a2 / 50) && g.offset <= 0);
    }

    // A lock a block late still counts; a late "off" overrides a rain accent.
    {
        Line line;
        u32 pitch[opxy::block_frames];
        i32 output[opxy::block_frames];
        for (u32 i = 0; i < opxy::block_frames; ++i) pitch[i] = a2;
        i32 parameters[8] = {init[0], init[1], init[2], init[3], off, off, full, 0};
        acidrain::start(line.voices[0], line.shared, 1);
        acidrain::render(line.voices[0], line.shared, 1, output, parameters, pitch);
        parameters[4] = on;
        acidrain::render(line.voices[0], line.shared, 1, output, parameters, pitch);
        check("accent lock a block late", (line.voices[0].lane & acidrain::lane_accent) != 0);
        parameters[4] = off;
        acidrain::render(line.voices[0], line.shared, 1, output, parameters, pitch);
        check("a lock latches for the note", (line.voices[0].lane & acidrain::lane_accent) != 0);
    }

    // Rain: dew 0 never rains; dew 100 rains; the same line rains on the same notes.
    {
        const u32 melody[8] = {a2, a2 * 2, a2, a2 * 3 / 2, a2, a2 * 2, a2 * 4 / 3, a2};
        u32 lanes[2][64];
        for (u32 run = 0; run < 2; ++run) {
            Line line;
            i32 parameters[8] = {init[0], init[1], init[2], init[3], automatic, automatic, 0, full};
            for (u32 n = 0; n < 64; ++n) {
                line.play(parameters, melody[n % 8], 86);  // 16ths at 120 bpm
                lanes[run][n] = line.voices[(line.owner - 1) % 2].lane;
            }
        }
        u32 same = 1, accents = 0, slides = 0, octaves = 0;
        for (u32 n = 0; n < 64; ++n) {
            same &= (lanes[0][n] & 15) == (lanes[1][n] & 15);
            accents += (lanes[0][n] & acidrain::lane_accent) != 0;
            slides += (lanes[0][n] & acidrain::lane_slide) != 0;
            octaves += (lanes[0][n] & acidrain::lane_octave) != 0;
        }
        check("the rain repeats", same);
        check("dew 100 at pH 2 rains often", accents >= 12 && accents < 56);
        check("pH 2 slides and jumps octaves", slides >= 8 && octaves >= 4);
        std::printf("dew 100 pH 2: %u accents, %u slides, %u octaves in 64 sixteenths\n", accents, slides, octaves);

        Line dry;
        i32 clean[8] = {init[0], init[1], init[2], init[3], automatic, automatic, full, 0};
        u32 rained = 0, jumped = 0;
        for (u32 n = 0; n < 64; ++n) {
            dry.play(clean, melody[n % 8], 86);
            rained += (dry.voices[(dry.owner - 1) % 2].lane & acidrain::lane_accent) != 0;
            jumped += (dry.voices[(dry.owner - 1) % 2].lane & acidrain::lane_octave) != 0;
        }
        check("dew 0 never rains", rained == 0);
        check("pH 7 never jumps octaves", jumped == 0);
    }

    // The voice goes quiet once its amp has decayed under -84 dB, and quickly after a short gate.
    {
        Line line;
        line.play(init, a2, 9000);  // 13 s
        check("a long note falls silent", (line.voices[0].lane & acidrain::lane_quiet) != 0);
        Line early;
        early.play(init, a2, 2000);
        check("a sounding note isn't silent", (early.voices[0].lane & acidrain::lane_quiet) == 0);
    }

    // pH 2 corrodes what pH 7 leaves clean.
    {
        static i32 x[64 * 64], z[64 * 64];
        Line a, c;
        const i32 clean[8] = {init[0], init[1], init[2], init[3], on, off, full, 0};
        const i32 acid[8] = {init[0], init[1], init[2], init[3], on, off, 0, 0};
        a.play(clean, a2 * 2, 64, x);
        c.play(acid, a2 * 2, 64, z);
        bool differs = false;
        for (u32 i = 0; i < 4096; ++i) differs = differs || x[i] != z[i];
        check("pH 2 corrodes", differs);
    }
    return failures ? 1 : 0;
}
