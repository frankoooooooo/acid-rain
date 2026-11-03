// Acid Rain's screen checks: every frame valid, still at rest, and the redraw
// (nodes changed from the previous frame) small while a pattern plays, at the
// default and the heaviest settings.
#include "../display.cpp"
#include <cstdio>
#include <cstring>

using acidrain::DisplayState;
typedef acidrain::Frame Screen;
using opxy::i32;
using opxy::u32;

static int failures = 0;
static void check(const char* name, bool ok) {
    if (!ok) {
        std::printf("FAIL %s\n", name);
        ++failures;
    }
}

// Why a frame is invalid, for the failure message.
static void diagnose(const Screen& f) {
    using namespace opxy::draw;
    for (u32 p = 0; p < Screen::path_count; ++p) {
        if (!f.paths[p].key) std::printf("  path %u has no key\n", p);
        for (u32 i = 0; i < Screen::point_count; ++i) {
            const Point& q = f.paths[p].points[i];
            if (q.x < -path_extent * unit || q.x > path_extent * unit || q.y < -path_extent * unit || q.y > path_extent * unit)
                std::printf("  path %u point %u at %d, %d\n", p, i, q.x >> 10, q.y >> 10);
        }
    }
    for (u32 i = 0; i < Screen::node_count; ++i) {
        const Node& n = f.nodes[i];
        if (!placement(n.x, n.y, n.scale) || !rotation_valid(n.cos, n.sin) || n.shade >= Screen::palette_count || n.paint > fill_and_stroke)
            std::printf("  node %u: %d, %d scale %d turn %d, %d shade %u\n", i, n.x, n.y, n.scale, n.cos, n.sin, n.shade);
    }
}

static bool same(const opxy::draw::Node& a, const opxy::draw::Node& b) { return std::memcmp(&a, &b, sizeof a) == 0; }

// Plays sixteenths at 120 bpm at 30 frames a second with these slots, then
// rests. Returns the average and peak nodes changed per frame while playing.
// The closest any smile or eye point comes to the border, in pixels squared.
static long long closest(const DisplayState& state) {
    long long best = 1LL << 40;
    for (u32 b = 0; b < 64; ++b)
        for (u32 k = 0; k < 32 + 128; ++k) {
            const i32* q = k < 32 ? state.smile[k] : state.eyes[(k - 32) / 64][(k - 32) % 64];
            const long long dx = (q[0] - state.border[b][0]) >> 10, dy = (q[1] - state.border[b][1]) >> 10;
            best = dx * dx + dy * dy < best ? dx * dx + dy * dy : best;
        }
    return best;
}

static long long nearest = 1LL << 40;  // over every frame played
static double bunching = 1;             // the tightest neighbouring border points, as a share of their spacing at rest
static double rest_spacing[64];
static double sharpest = 0;             // the most turn the water adds at any border point, degrees
static double rest_turn[64];
static double tightest_gap = 1e9, shortest_close = 1e9;  // over every outline and frame, path px

// The device merges outline points closer than 1/8 px and draws a sliver from a
// closing edge under 1/4 px (open-xy docs/display.md).
static void outlines(const Screen& frame) {
    for (u32 p = 0; p < Screen::path_count; ++p)
        for (u32 i = 0; i < Screen::point_count; ++i) {
            const opxy::draw::Point& a = frame.paths[p].points[i];
            const opxy::draw::Point& b = frame.paths[p].points[(i + 1) % Screen::point_count];
            const double dx = (b.x - a.x) / 1024.0, dy = (b.y - a.y) / 1024.0, gap = __builtin_sqrt(dx * dx + dy * dy);
            tightest_gap = gap < tightest_gap ? gap : tightest_gap;
            if (i + 1 == Screen::point_count) shortest_close = gap < shortest_close ? gap : shortest_close;
        }
}

static double turning(const DisplayState& state, u32 i) {
    const i32* a = state.border[(i + 63) % 64];
    const i32* b = state.border[i];
    const i32* c = state.border[(i + 1) % 64];
    const double ux = b[0] - a[0], uy = b[1] - a[1], vx = c[0] - b[0], vy = c[1] - b[1];
    const double cosine = (ux * vx + uy * vy) / __builtin_sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
    return __builtin_acos(cosine < -1 ? -1 : cosine > 1 ? 1 : cosine) * 57.29578;
}

static double spacing(const DisplayState& state, u32 i) {
    const double dx = (state.border[(i + 1) % 64][0] - state.border[i][0]) / 1024.0;
    const double dy = (state.border[(i + 1) % 64][1] - state.border[i][1]) / 1024.0;
    return __builtin_sqrt(dx * dx + dy * dy);
}

static void play(const char* name, const i32 (&slots)[8], double& average, u32& peak, u32& at_rest, bool reduced = false) {
    static DisplayState state;
    static Screen frame, last;
    std::memset(&state, 0, sizeof state);
    std::memset(&frame, 0, sizeof frame);
    acidrain::display_reset(state);
    opxy::draw::Input input = {};
    for (u32 i = 0; i < 8; ++i) input.slots[i] = slots[i];
    for (u32 i = 0; i < 4; ++i) { input.parameters[i] = slots[i]; input.shown[i] = static_cast<opxy::u8>(i); }
    input.track = 1;
    input.reduced_motion = reduced;
    input.elapsed = 34;  // 30 frames a second, Q10
    u32 changed = 0, frames = 0;
    peak = 0;
    at_rest = 0;
    const u32 playing = 30 * 8, total = playing + 30 * 8;  // 8 s of notes, then 8 s of rest
    double clock = 0, next = 0;
    for (u32 n = 0; n < total; ++n) {
        if (n < playing) {
            clock += 1.0 / 30;
            while (clock >= next) {
                ++input.note_on_serial;
                input.held_notes = 1;
                next += 0.125;
            }
            if (clock - (next - 0.125) > 0.09 && input.held_notes) { input.held_notes = 0; ++input.note_off_serial; }
        } else {
            input.held_notes = 0;
        }
        std::memcpy(&last, &frame, sizeof frame);
        acidrain::display(state, input, frame);
        if (!opxy::draw::valid(frame)) {
            std::printf("FAIL %s: invalid frame %u\n", name, n);
            diagnose(frame);
            ++failures;
            return;
        }
        outlines(frame);
        const long long gap = closest(state);
        nearest = gap < nearest ? gap : nearest;
        for (u32 i = 0; i < 64; ++i) {
            if (n == 0) { rest_spacing[i] = spacing(state, i); rest_turn[i] = turning(state, i); continue; }
            else if (spacing(state, i) / rest_spacing[i] < bunching) bunching = spacing(state, i) / rest_spacing[i];
            const double added = __builtin_fabs(turning(state, i) - rest_turn[i]);
            sharpest = added > sharpest ? added : sharpest;
        }
        if (n == 0) continue;
        u32 count = 0;
        for (u32 i = 0; i < Screen::node_count; ++i) count += !same(frame.nodes[i], last.nodes[i]);
        for (u32 p = 0; p < Screen::path_count; ++p) count += frame.paths[p].key != last.paths[p].key;
        if (n < playing) {
            changed += count;
            ++frames;
            peak = count > peak ? count : peak;
        } else if (n >= total - 30) {
            at_rest += count;  // the last second: even decay 2.0's rings (about 6 s) have settled
        }
    }
    average = frames ? static_cast<double>(changed) / frames : 0;
    std::printf("%s: %.1f nodes a frame while playing, peak %u, %u in the last second of rest\n", name, average, peak, at_rest);
}

int main() {
    const i32 full = 0x7fffffff;
    const i32 defaults[8] = {858993459, 858993459, 1073741824, 646464646, 0, 0, full, 0};
    const i32 heaviest[8] = {858993459, full, full, full, full, full / 2, 0, full};
    double average;
    u32 peak, rest;
    play("defaults", defaults, average, peak, rest);
    check("defaults: about 20 nodes a frame or fewer", average <= 22);
    check("defaults: still at rest", rest == 0);
    play("heaviest", heaviest, average, peak, rest);
    check("heaviest: about 20 nodes a frame or fewer", average <= 22);
    check("heaviest: still at rest", rest == 0);
    play("reduced motion", heaviest, average, peak, rest, true);
    const i32 jagged[8] = {full, full, full, 646464646, full / 2, 0, 0, 0};  // the tightest rings, pH 2
    play("high cutoff, pH 2", jagged, average, peak, rest);
    check("high cutoff, pH 2: about 20 nodes a frame or fewer", average <= 22);
    // The face's lines never meet: smile and eyes stay 3 px or more inside the border.
    std::printf("closest approach of smile or eye to the border: %.1f px\n", __builtin_sqrt(static_cast<double>(nearest)));
    check("the face's lines never meet", nearest >= 9);
    // The border's points never bunch up: neighbours keep half their spacing at rest.
    std::printf("tightest border spacing: %.0f%% of rest\n", bunching * 100);
    check("the border never bunches", bunching >= 0.5);
    // Nor zigzags: the water adds 30 degrees or less of turn at any point (a sawtooth adds 60 or more).
    std::printf("most turn the water adds along the border: %.0f degrees\n", sharpest);
    check("the border never zigzags", sharpest <= 30);
    std::printf("outline points at least %.3f px apart, closing edges at least %.3f px\n", tightest_gap, shortest_close);
    check("outline points stay 1/8 px apart", tightest_gap >= 0.125);
    check("closing edges stay 1/4 px or longer", shortest_close >= 0.25);
    check("reduced motion: still at rest", rest == 0);
    return failures ? 1 : 0;
}
