/* The cookbook, compiled and run: the acceptance suite.
 *
 * Every recipe from API-SKETCH.md's "The cookbook as code" appears here
 * verbatim, and cmake/stripseq_cookbook_check.cmake fails the build's tests if
 * one drifts from the sketch. A recipe that needs an edit to compile is a
 * finding about the sketch, never a reason to edit the copy.
 *
 * The recipes are fragments, so the harness supplies only what they leave out:
 * a now() reading a fake clock, each recipe's declarations at file scope, and
 * each recipe's statements in a function its test case calls exactly once.
 * They share the one seq and strip, so every test case begins with begin(),
 * which removes every segment and puts the clock back at its origin. No
 * SUBCASE appears below: doctest re-runs a test case from the top for each
 * one, and a recipe's add() would then run twice.
 *
 * Each recipe is checked twice over. Property assertions compare frames with
 * each other, or with the recipe's own arithmetic, so they hold in every
 * variant and say what broke. A golden hash per recipe then pins the exact
 * bytes and holds over a table of timestamps, one table of hashes per
 * behavioural configuration.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>   /* range-for over a braced list; gcc insists */

#include <stripseq/stripseq.h>

#include "support.h"

/* ------------------------------------------------------------------------ */
/* The setup, verbatim                                                      */
/* ------------------------------------------------------------------------ */

using namespace stripseq;

static constexpr uint16_t kPixels = 30;

static uint8_t   buffer[kPixels * Strip::bytes_per_pixel];
static Sequencer seq;

constinit static Strip strip(buffer);   // pixel count deduced from the buffer;
                                        // segment capacity from STRIPSEQ_MAX_SEGMENTS

/* ------------------------------------------------------------------------ */
/* The harness                                                              */
/* ------------------------------------------------------------------------ */

namespace {

/* A second before 2^32 ms, so every table of timestamps below crosses the
 * point where a uint32_t reading would wrap. A narrowing anywhere between
 * now() and the playhead cancels out of any difference taken on one side of
 * it; across it, the narrowing is a 2^32 ms jump mid-recipe. */
constexpr int64_t kOrigin = (int64_t{1} << 32) - 1000;

int64_t clock_ms = kOrigin;

constexpr size_t kBytes = sizeof(buffer);

}  // namespace

static int64_t now() { return clock_ms; }

/* Every test case starts here: an empty strip, and the clock at its origin.
 * load() resets a sequencer, so nothing else carries over between recipes. */
static void begin() {
    while (strip.segment_count() > 0)
        strip.remove(0);
    clock_ms = kOrigin;
}

/* Moves the clock to `offset` ms after the origin and renders there. */
static uint32_t render_at(int64_t offset) {
    clock_ms = kOrigin + offset;
    return strip.render(now());
}

static void copy_frame(uint8_t (&to)[kBytes]) {
    for (size_t k = 0; k < kBytes; ++k)
        to[k] = buffer[k];
}

/* Whether pixel i of frame a holds the same bytes as pixel j of frame b. */
static bool same_pixel(const uint8_t* a, uint16_t i, const uint8_t* b, uint16_t j) {
    return same_bytes(a + size_t{i} * Strip::bytes_per_pixel,
                      b + size_t{j} * Strip::bytes_per_pixel, Strip::bytes_per_pixel);
}

/* Whether every channel of pixel i in frame a is no brighter than pixel j in
 * frame b. Per byte, so it holds whatever gamma and the channel count do. */
static bool no_brighter(const uint8_t* a, uint16_t i, const uint8_t* b, uint16_t j) {
    for (size_t k = 0; k < Strip::bytes_per_pixel; ++k)
        if (a[size_t{i} * Strip::bytes_per_pixel + k] > b[size_t{j} * Strip::bytes_per_pixel + k])
            return false;
    return true;
}

static bool all_pixels_alike() {
    for (uint16_t i = 1; i < kPixels; ++i)
        if (!same_pixel(buffer, i, buffer, 0))
            return false;
    return true;
}

/* ------------------------------------------------------------------------ */
/* The recipes, verbatim                                                    */
/* ------------------------------------------------------------------------ */

static constexpr Sequence kBlink =
    SequenceBuilder{}.hold(colors::red, 500).hold(colors::black, 500).build_const();

static void recipe_blink() {
    seq.load(kBlink);
    seq.play(now());
    strip.add(Segment(0, kPixels, seq));      // pixel_delay defaults to 0
}

static constexpr Sequence kBreathe = SequenceBuilder{}
    .fade_to(colors::rgbwhite, 1000, Transition::EaseInOut)
    .fade_to(colors::black,    1000, Transition::EaseInOut)
    .build_const();

static void recipe_breathe() {
    seq.load(kBreathe);
    seq.play(now());
    strip.add(Segment(0, kPixels, seq));
}

static constexpr Sequence kComet = SequenceBuilder{}
    .hold(colors::rgbwhite, 50)
    .fade_to(colors::black, 450)
    .build_const();

static void recipe_comet() {
    seq.load(kComet);
    seq.play(now());

    Segment s(0, kPixels, seq);
    s.set_pixel_delay(16);        // speed intent: 16 ms per pixel whatever the length
    strip.add(s);
}

inline constexpr Sequence kRainbow = SequenceBuilder{}
    .fade_to(Color::hsv(0x2AAA, 255, 255), 500)   // red   -> yellow
    .fade_to(Color::hsv(0x5555, 255, 255), 500)   //       -> green
    .fade_to(Color::hsv(0x8000, 255, 255), 500)   //       -> cyan
    .fade_to(Color::hsv(0xAAAA, 255, 255), 500)   //       -> blue
    .fade_to(Color::hsv(0xD555, 255, 255), 500)   //       -> magenta
    .fade_to(Color::hsv(0x0000, 255, 255), 500)   //       -> red
    .build_const();

static void recipe_rainbow() {
    seq.load(kRainbow);
    seq.play(now());

    Segment s(0, kPixels, seq);
    s.set_pixel_delay_q8(spread(3000, kPixels));   // the whole wheel across the segment
    strip.add(s);
}

static void recipe_static_gradient() {
    seq.load(kRainbow);
    seq.seek(0);                  // seeked, never played: the playhead does not move

    Segment s(0, kPixels, seq);
    s.set_pixel_delay_q8(spread(3000, kPixels));
    strip.add(s);
}

static constexpr Sequence kWipe = SequenceBuilder{}
    .start_color(colors::black)
    .fade_to(colors::green, 200)
    .landing_color(colors::green)
    .repeat(1)
    .build_const();

static void recipe_wipe() {
    seq.load(kWipe);
    seq.play(now());

    Segment s(0, kPixels, seq);
    s.set_pixel_delay(30);
    strip.add(s);
}

static constexpr Sequence kProgress = SequenceBuilder{}
    .start_color(colors::black)
    .fade_to(colors::red, 10)
    .landing_color(colors::red)
    .repeat(1)
    .build_const();

static void recipe_progress_bar() {
    seq.load(kProgress);

    Segment s(0, kPixels, seq);
    s.set_pixel_delay(10);
    strip.add(s);
}

// Called whenever the value changes.
void set_progress(uint8_t percent) {
    seq.seek(static_cast<int32_t>(percent) * kPixels * 10 / 100);
}

static Sequencer seq_a, seq_b;   // loaded and started as above

static void recipe_two_halves_different() {
    strip.add(Segment(0,  15, seq_a));
    strip.add(Segment(15, 15, seq_b));
}

static void recipe_two_halves_converging() {
    Segment left(0, 15, seq);
    Segment right(15, 15, seq);
    left.set_pixel_delay(20);
    right.set_pixel_delay(-20);
    strip.add(left);
    strip.add(right);
}

// pixel_delay is the span the offsets scatter across, here 1980 ms.
static int32_t scatter(uint16_t index, uint16_t /*count*/, int32_t span_q8) {
    const int32_t span = span_q8 >> 8;
    if (span <= 0)
        return 0;
    const uint32_t h = index * 2654435761u;      // Knuth's multiplicative hash
    return static_cast<int32_t>(h % static_cast<uint32_t>(span));
}

static constexpr Sequence kTwinkle = SequenceBuilder{}
    .fade_to(colors::rgbwhite, 80)
    .fade_to(colors::black, 1900)
    .build_const();

static void recipe_twinkle() {
    seq.load(kTwinkle);
    seq.play(now());

    Segment s(0, kPixels, seq);
    s.set_offset_fn(scatter);
    s.set_pixel_delay(1980);
    strip.add(s);
}

static Sequencer breathe_seq;
static Sequencer alert_seq;

static constexpr Sequence kBackground = SequenceBuilder{}
    .fade_to(colors::blue,  1500, Transition::EaseInOut)
    .fade_to(colors::black, 1500, Transition::EaseInOut)
    .build_const();

void start_background() {
    breathe_seq.load(kBackground);
    breathe_seq.play(now());
    strip.add(Segment(0, kPixels, breathe_seq));    // first in the list, painted first
}

static constexpr Sequence kAlert = SequenceBuilder{}
    .hold(colors::red,   120)
    .hold(colors::black, 120)
    .repeat(3)
    .build_const();

void raise_alert() {
    alert_seq.load(kAlert);
    alert_seq.play(now());
    strip.add(Segment(0, kPixels, alert_seq));      // added last, so painted on top
}

/* The recipe's one line in the frame loop. */
static void alert_frame() {
    strip.drop_finished(now());
}

void cancel_alert() {
    for (uint8_t i = strip.segment_count(); i > 0; --i)
        if (strip.segment(i - 1).drives(alert_seq))
            strip.remove(i - 1);
}

/* ------------------------------------------------------------------------ */
/* Properties                                                               */
/* ------------------------------------------------------------------------ */

TEST_CASE("Blink: the whole strip alternates red and black every 500 ms") {
    begin();
    recipe_blink();

    for (int64_t k = 0; k < 8; ++k) {
        const Color want = k % 2 == 0 ? colors::red : colors::black;
        CHECK(render_at(k * 500) == 500);
        CHECK(all_pixels_alike());
        CHECK(shows(buffer, 0, want));

        render_at(k * 500 + 250);
        CHECK(all_pixels_alike());
        CHECK(shows(buffer, 0, want));
    }
}

TEST_CASE("Breathe: black, rising to white at 1000 ms, falling to black at 2000") {
    begin();
    recipe_breathe();

    uint8_t previous[kBytes];
    render_at(0);
    CHECK(all_pixels_alike());
    CHECK(shows(buffer, 0, colors::black));
    copy_frame(previous);

    /* Monotonic per channel, in each direction. Deliberately not symmetric
     * about 1000 ms: progress is floored, so the rise at 1000 - t and the
     * fall at 1000 + t sample the curve at points that do not mirror. */
    for (int64_t t = 1; t <= 2000; ++t) {
        render_at(t);
        REQUIRE(all_pixels_alike());
        if (t <= 1000)
            REQUIRE(no_brighter(previous, 0, buffer, 0));
        else
            REQUIRE(no_brighter(buffer, 0, previous, 0));
        copy_frame(previous);
    }

    render_at(1000);
    CHECK(shows(buffer, 0, colors::rgbwhite));
    render_at(2000);
    CHECK(shows(buffer, 0, colors::black));

    CHECK(render_at(500) == 0);
    CHECK(render_at(1500) == 0);
}

TEST_CASE("Comet: brightness falls monotonically back from the head") {
    begin();
    recipe_comet();

    for (int64_t t = 0; t < 3000; t += 7) {
        render_at(t);

        /* Pixel i is at t - 16 i in the 500 ms loop. The head is the pixel
         * earliest in it; walking back from there, down the strip and round
         * to the far end, every pixel is later in the loop than the one
         * before. 29 x 16 + 36 = 500, so the walk never comes back round. */
        auto phase = [t](uint16_t i) { return ((t - 16 * int64_t{i}) % 500 + 500) % 500; };
        uint16_t head = 0;
        for (uint16_t i = 1; i < kPixels; ++i)
            if (phase(i) < phase(head))
                head = i;

        CAPTURE(t);
        CAPTURE(head);
        REQUIRE(shows(buffer, head, colors::rgbwhite));
        for (uint16_t step = 1; step < kPixels; ++step) {
            const uint16_t here = (head + kPixels - step) % kPixels;
            const uint16_t ahead = (here + 1) % kPixels;
            REQUIRE(no_brighter(buffer, here, buffer, ahead));
        }
    }
}

TEST_CASE("Rainbow cycle: each pixel shows what its predecessor showed 100 ms earlier") {
    begin();
    recipe_rainbow();

    /* spread(3000, 30) is exactly 100 ms, so the comparison is exact, and the
     * seam on the ring obeys the same rule as every other pair. */
    REQUIRE(spread(3000, kPixels) == 100 * 256);

    uint8_t earlier[kBytes];
    for (int64_t t = 100; t < 6100; t += 13) {
        render_at(t - 100);
        copy_frame(earlier);
        render_at(t);
        CAPTURE(t);
        for (uint16_t i = 1; i < kPixels; ++i)
            REQUIRE(same_pixel(buffer, i, earlier, i - 1));
        REQUIRE(same_pixel(buffer, 0, earlier, kPixels - 1));
    }
}

TEST_CASE("Static gradient: the same frame at any time, held forever") {
    begin();
    recipe_static_gradient();

    uint8_t first[kBytes];
    CHECK(render_at(0) == hold_forever);
    copy_frame(first);

    CHECK(render_at(int64_t{30} * 24 * 3600 * 1000) == hold_forever);
    CHECK(same_bytes(buffer, first, kBytes));
}

TEST_CASE("Wipe: green lands pixel by pixel, stays, and drop_finished blanks it") {
    begin();
    recipe_wipe();

    /* The last pixel lags 29 x 30 ms and its fade is 200. */
    constexpr int64_t landed = 200 + 29 * 30;

    for (int64_t t : {int64_t{0}, int64_t{199}, int64_t{1000}, landed - 1}) {
        clock_ms = kOrigin + t;
        CAPTURE(t);
        CHECK_FALSE(strip.segment(0).finished(now()));
    }

    clock_ms = kOrigin + landed;
    CHECK(strip.segment(0).finished(now()));
    CHECK(render_at(landed) == hold_forever);
    for (uint16_t i = 0; i < kPixels; ++i)
        CHECK(shows(buffer, i, colors::green));

    /* Still green long after, because nothing removed it. */
    CHECK(render_at(landed + 60000) == hold_forever);
    for (uint16_t i = 0; i < kPixels; ++i)
        CHECK(shows(buffer, i, colors::green));

    /* The recipe's own warning: dropping it blanks the strip. */
    CHECK(strip.drop_finished(now()) == 1);
    CHECK(render_at(landed + 60000) == hold_forever);
    for (uint16_t i = 0; i < kPixels; ++i)
        CHECK(shows(buffer, i, colors::black));
}

/* Whether pixel i is part-lit: every byte that differs between black and red
 * lies strictly between the two. Its exact value moves with gamma. */
static bool part_lit(uint16_t i) {
    uint8_t dark[Strip::bytes_per_pixel];
    uint8_t lit[Strip::bytes_per_pixel];
    to_bytes(colors::black, 255, dark);
    to_bytes(colors::red, 255, lit);
    for (size_t k = 0; k < Strip::bytes_per_pixel; ++k) {
        const uint8_t b = buffer[size_t{i} * Strip::bytes_per_pixel + k];
        if (dark[k] == lit[k] ? b != dark[k] : !(dark[k] < b && b < lit[k]))
            return false;
    }
    return true;
}

TEST_CASE("Progress bar: the playhead is the value") {
    begin();
    recipe_progress_bar();

    set_progress(0);
    render_at(0);
    for (uint16_t i = 0; i < kPixels; ++i)
        CHECK(shows(buffer, i, colors::black));

    /* A playhead of 75: pixels 0..6 are past their fade, and pixel 7 is 5 ms
     * into its 10. */
    set_progress(25);
    render_at(0);
    for (uint16_t i = 0; i < 7; ++i)
        CHECK(shows(buffer, i, colors::red));
    CHECK(part_lit(7));
    for (uint16_t i = 8; i < kPixels; ++i)
        CHECK(shows(buffer, i, colors::black));

    /* Here the boundary falls exactly between two pixels, so none is
     * part-lit. */
    set_progress(50);
    render_at(0);
    for (uint16_t i = 0; i < 15; ++i)
        CHECK(shows(buffer, i, colors::red));
    for (uint16_t i = 15; i < kPixels; ++i)
        CHECK(shows(buffer, i, colors::black));

    set_progress(100);
    render_at(0);
    for (uint16_t i = 0; i < kPixels; ++i)
        CHECK(shows(buffer, i, colors::red));
}

/* Blink on seq_a and Comet on seq_b, which the recipe leaves to the caller. */
static void start_halves() {
    seq_a.load(kBlink);
    seq_a.play(now());
    seq_b.load(kComet);
    seq_b.play(now());
}

TEST_CASE("Two halves, different animations: each half paints what it paints alone") {
    begin();
    start_halves();
    recipe_two_halves_different();

    uint8_t alone_a[kBytes];
    uint8_t alone_b[kBytes];
    Strip scratch_a(alone_a);
    Strip scratch_b(alone_b);
    REQUIRE(scratch_a.add(Segment(0, 15, seq_a)));
    REQUIRE(scratch_b.add(Segment(15, 15, seq_b)));

    for (int64_t t = 0; t < 3000; t += 11) {
        const uint32_t hold = render_at(t);
        const uint32_t hold_a = scratch_a.render(now());
        const uint32_t hold_b = scratch_b.render(now());
        CAPTURE(t);
        CHECK(hold == (hold_a < hold_b ? hold_a : hold_b));
        for (uint16_t i = 0; i < 15; ++i) {
            REQUIRE(same_pixel(buffer, i, alone_a, i));
            REQUIRE(shows(alone_b, i, colors::black));
        }
        for (uint16_t i = 15; i < kPixels; ++i) {
            REQUIRE(same_pixel(buffer, i, alone_b, i));
            REQUIRE(shows(alone_a, i, colors::black));
        }
    }
}

/* Comet on seq: a stagger is what shows the direction. */
static void start_converging() {
    seq.load(kComet);
    seq.play(now());
}

TEST_CASE("Two halves, converging: both patterns move one pixel toward the centre every 20 ms") {
    begin();
    start_converging();
    recipe_two_halves_converging();

    uint8_t before[kBytes];
    for (int64_t t = 0; t < 3000; t += 13) {
        render_at(t);
        copy_frame(before);
        render_at(t + 20);
        CAPTURE(t);
        for (uint16_t k = 0; k < 14; ++k)
            REQUIRE(same_pixel(buffer, k + 1, before, k));
        for (uint16_t j = 0; j < 14; ++j)
            REQUIRE(same_pixel(buffer, 15 + j, before, 16 + j));
    }
}

TEST_CASE("Twinkle: thirty distinct phases, all inside the span") {
    begin();
    recipe_twinkle();

    constexpr int32_t span_q8 = 1980 * 256;
    int32_t offsets[kPixels];
    for (uint16_t i = 0; i < kPixels; ++i) {
        offsets[i] = scatter(i, kPixels, span_q8);
        CHECK(offsets[i] >= 0);
        CHECK(offsets[i] < 1980);
        for (uint16_t j = 0; j < i; ++j)
            CHECK(offsets[i] != offsets[j]);
    }

    /* And the strip paints them: pixel 0's offset is 0, so pixel i shows now
     * what pixel 0 showed offsets[i] ms ago. */
    REQUIRE(offsets[0] == 0);
    uint8_t earlier[kBytes];
    for (int64_t t = 2000; t < 6000; t += 97) {
        render_at(t);
        uint8_t now_frame[kBytes];
        copy_frame(now_frame);
        CAPTURE(t);
        for (uint16_t i = 1; i < kPixels; ++i) {
            render_at(t - offsets[i]);
            copy_frame(earlier);
            REQUIRE(same_pixel(now_frame, i, earlier, 0));
        }
    }
}

/* The breathe painted alone, on a strip of its own, at the current now(). */
static void render_breathe_alone(uint8_t (&into)[kBytes]) {
    Strip scratch(into);
    REQUIRE(scratch.add(Segment(0, kPixels, breathe_seq)));
    scratch.render(now());
}

TEST_CASE("Alert over a background: the breathe resumes where it would have been") {
    begin();
    start_background();
    render_at(400);
    raise_alert();

    /* While the alert runs it covers the breathe entirely. */
    render_at(400 + 60);
    for (uint16_t i = 0; i < kPixels; ++i)
        CHECK(shows(buffer, i, colors::red));

    /* Nothing goes before the alert's 720 ms, and exactly the alert at them. */
    clock_ms = kOrigin + 400 + 719;
    alert_frame();
    CHECK(strip.segment_count() == 2);

    clock_ms = kOrigin + 400 + 720;
    alert_frame();
    REQUIRE(strip.segment_count() == 1);
    CHECK(strip.segment(0).drives(breathe_seq));

    uint8_t alone[kBytes];
    for (int64_t t = 400 + 720; t < 7000; t += 41) {
        render_at(t);
        render_breathe_alone(alone);
        CAPTURE(t);
        REQUIRE(same_bytes(buffer, alone, kBytes));
    }
}

TEST_CASE("Alert over a background: cancel_alert removes it early and leaves the breathe") {
    begin();
    start_background();
    render_at(400);
    raise_alert();

    clock_ms = kOrigin + 400 + 300;
    cancel_alert();
    REQUIRE(strip.segment_count() == 1);
    CHECK(strip.segment(0).drives(breathe_seq));

    uint8_t alone[kBytes];
    render_at(400 + 300);
    render_breathe_alone(alone);
    CHECK(same_bytes(buffer, alone, kBytes));
}

/* ------------------------------------------------------------------------ */
/* Golden hashes                                                            */
/* ------------------------------------------------------------------------ */

namespace {

/* Offsets from the origin, in rendering order. They cross 1000 ms -- the
 * 2^32 point -- and they include the edges the recipes care about: Blink's
 * 500s, Comet's 50, Wipe's 1070, the alert's 720 after it is raised at 500,
 * and a day later for whatever holds forever. */
constexpr int64_t kTimestamps[] = {
    0,    1,    16,   49,   50,   99,   100,  250,  499,  500,  501,  719,
    720,  999,  1000, 1001, 1069, 1070, 1219, 1220, 1500, 1980, 2000, 2999,
    3000, 4500, 6000, int64_t{24} * 3600 * 1000,
};

constexpr uint32_t kFnvBasis = 2166136261u;
constexpr uint32_t kFnvPrime = 16777619u;

uint32_t fnv1a(uint32_t h, uint8_t byte) { return (h ^ byte) * kFnvPrime; }

void raise_alert_at_500(size_t /*frame*/, int64_t offset) {
    if (offset == 500)
        raise_alert();
    alert_frame();
}

/* The Progress bar's sequencer is never played, so the timestamps alone would
 * hash one black frame over and over. Its value is what moves it: the frames
 * walk these percentages instead, which put the boundary inside a pixel and
 * exactly between two. */
constexpr uint8_t kPercents[] = {0, 1, 3, 25, 33, 50, 67, 99, 100};

void walk_progress(size_t frame, int64_t /*offset*/) {
    set_progress(kPercents[frame % (sizeof(kPercents) / sizeof(kPercents[0]))]);
}

struct Recipe {
    const char* name;
    void (*start)();                 /* begin() has run and now() is the origin */
    void (*before_frame)(size_t frame, int64_t offset);   /* may be null */
};

void start_blink() { recipe_blink(); }
void start_two_halves_different() {
    start_halves();
    recipe_two_halves_different();
}
void start_two_halves_converging() {
    start_converging();
    recipe_two_halves_converging();
}

constexpr Recipe kRecipes[] = {
    {"Blink",                         start_blink,                  nullptr},
    {"Breathe",                       recipe_breathe,               nullptr},
    {"Comet",                         recipe_comet,                 nullptr},
    {"Rainbow cycle",                 recipe_rainbow,               nullptr},
    {"Static gradient",               recipe_static_gradient,       nullptr},
    {"Wipe",                          recipe_wipe,                  nullptr},
    {"Progress bar",                  recipe_progress_bar,          walk_progress},
    {"Two halves, different",         start_two_halves_different,   nullptr},
    {"Two halves, converging",        start_two_halves_converging,  nullptr},
    {"Twinkle",                       recipe_twinkle,               nullptr},
    {"Alert over a background",       start_background,             raise_alert_at_500},
};

constexpr size_t kRecipeCount = sizeof(kRecipes) / sizeof(kRecipes[0]);

/* FNV-1a over every frame's bytes and the hold render() returned, in
 * timestamp order, so a drifting hold is caught as surely as a drifting
 * color. */
uint32_t golden_hash(const Recipe& recipe) {
    begin();
    recipe.start();
    uint32_t h = kFnvBasis;
    size_t frame = 0;
    for (int64_t offset : kTimestamps) {
        clock_ms = kOrigin + offset;
        if (recipe.before_frame)
            recipe.before_frame(frame, offset);
        ++frame;
        const uint32_t hold = strip.render(now());
        for (size_t k = 0; k < kBytes; ++k)
            h = fnv1a(h, buffer[k]);
        for (int shift = 0; shift < 32; shift += 8)
            h = fnv1a(h, static_cast<uint8_t>(hold >> shift));
    }
    return h;
}

/* One table per behavioural configuration. tight has no table of its own: a
 * capacity has no business reaching the output, so it must match the
 * default's. Regenerate deliberately, with the skipped case below, and review
 * the diff. */
#if !STRIPSEQ_WHITE_CHANNEL && STRIPSEQ_GAMMA
constexpr uint32_t kGolden[] = {
    0x2E7AF85Du,  /* Blink */
    0x6D65D87Fu,  /* Breathe */
    0x47EB87BCu,  /* Comet */
    0x1B564ABFu,  /* Rainbow cycle */
    0x16368FF5u,  /* Static gradient */
    0x36B89F90u,  /* Wipe */
    0x485069ECu,  /* Progress bar */
    0x2BE780C7u,  /* Two halves, different */
    0x7953B45Au,  /* Two halves, converging */
    0x00CA2899u,  /* Twinkle */
    0xF6536567u,  /* Alert over a background */
};
#elif STRIPSEQ_WHITE_CHANNEL && STRIPSEQ_GAMMA
constexpr uint32_t kGolden[] = {
    0x6B34857Fu,  /* Blink */
    0x708BDB15u,  /* Breathe */
    0xAE9C2180u,  /* Comet */
    0x453F37AFu,  /* Rainbow cycle */
    0xDC6FC5A5u,  /* Static gradient */
    0xE4FFEE26u,  /* Wipe */
    0x9F3FF124u,  /* Progress bar */
    0xEFD09AE5u,  /* Two halves, different */
    0x7ACADF9Cu,  /* Two halves, converging */
    0x15CCDEC5u,  /* Twinkle */
    0xF2C98435u,  /* Alert over a background */
};
#elif !STRIPSEQ_WHITE_CHANNEL && !STRIPSEQ_GAMMA
constexpr uint32_t kGolden[] = {
    0x2E7AF85Du,  /* Blink */
    0xC9BC2DD7u,  /* Breathe */
    0x50A3E4FCu,  /* Comet */
    0x94122595u,  /* Rainbow cycle */
    0x421EF835u,  /* Static gradient */
    0x415F705Cu,  /* Wipe */
    0x0332DF3Eu,  /* Progress bar */
    0xC995E008u,  /* Two halves, different */
    0xB5B317B9u,  /* Two halves, converging */
    0x8F60A24Du,  /* Twinkle */
    0x5C3AADB3u,  /* Alert over a background */
};
#else
constexpr uint32_t kGolden[] = {
    0x6B34857Fu,  /* Blink */
    0x4C609275u,  /* Breathe */
    0x3D996052u,  /* Comet */
    0xE2C35D41u,  /* Rainbow cycle */
    0x2F011545u,  /* Static gradient */
    0xEB5DA992u,  /* Wipe */
    0xE7D20464u,  /* Progress bar */
    0x914DF1D2u,  /* Two halves, different */
    0x1FCC2F21u,  /* Two halves, converging */
    0x5F2B7C0Fu,  /* Twinkle */
    0x2CD651F5u,  /* Alert over a background */
};
#endif

static_assert(sizeof(kGolden) / sizeof(kGolden[0]) == kRecipeCount,
              "one golden hash per recipe");

}  // namespace

TEST_CASE("every recipe matches its golden hash") {
    for (const Recipe& recipe : kRecipes) {
        INFO(recipe.name);
        CHECK(golden_hash(recipe) == kGolden[&recipe - kRecipes]);
    }
}

/* Prints this variant's table as source, to paste over the matching one
 * above. Run once per behavioural variant:
 *
 *   build/tests_default -tc='print the golden hashes' --no-skip
 */
TEST_CASE("print the golden hashes" * doctest::skip()) {
    std::printf("#if %sSTRIPSEQ_WHITE_CHANNEL && %sSTRIPSEQ_GAMMA\n",
                STRIPSEQ_WHITE_CHANNEL ? "" : "!", STRIPSEQ_GAMMA ? "" : "!");
    std::printf("constexpr uint32_t kGolden[] = {\n");
    for (const Recipe& recipe : kRecipes)
        std::printf("    0x%08Xu,  /* %s */\n", static_cast<unsigned>(golden_hash(recipe)),
                    recipe.name);
    std::printf("};\n");
}
