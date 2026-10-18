/* Segment: the stagger, the two plug-in points, and the finished() question.
 *
 * A segment paints only through a strip, so most cases here render a one-
 * segment strip and read the pixels back. They are grouped by what the segment
 * promises. spread() fits a span to a ring without a seam. The setters refuse a
 * delay the default step 1 could not multiply, and a replacement's offset is
 * clamped to the same range, so no pixel's playhead leaves int32_t at either
 * end of the sequencer's. A sequencer that is not playing holds forever. And
 * finished() is true exactly when the last pixel lands, whichever pixel that is.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>   /* range-for over a braced list; gcc insists */

#include <stripseq/segment.h>
#include <stripseq/sequence.h>
#include <stripseq/sequencer.h>
#include <stripseq/strip.h>

#include "support.h"

using namespace stripseq;

namespace {

constexpr uint16_t kPixels = 10;
constexpr size_t kBytes = kPixels * Strip::bytes_per_pixel;

constexpr int64_t ms_per_day = 24 * 60 * 60 * 1000;

/* The largest q8 delay a kPixels segment accepts either way: its product with
 * the last index, 9, still fits in int32_t. */
constexpr int32_t largest_q8 = INT32_MAX / (kPixels - 1);

/* The range of the default step 1's offset, and so of a replacement's after
 * the clamp: an arithmetic shift of anything within +-INT32_MAX. */
constexpr int32_t max_offset_ms = (int32_t{1} << 23) - 1;
constexpr int32_t min_offset_ms = -(int32_t{1} << 23);

/* The Wipe recipe's sequence: black, 200 ms of fade, then green for good. */
constexpr Sequence wipe = SequenceBuilder{}
                              .start_color(colors::black)
                              .fade_to(colors::green, 200)
                              .landing_color(colors::green)
                              .repeat(1)
                              .build_const();

/* 777 ms a loop, which divides neither a power of two nor a day. */
constexpr Sequence endless_777 =
    SequenceBuilder{}.hold(colors::red, 400).fade_to(colors::green, 377).build_const();

/* Finite, with start, middle and landing all different, so a pixel at either
 * end of the timeline shows which end it is at. */
constexpr Sequence three_colors = SequenceBuilder{}
                                      .start_color(colors::blue)
                                      .fade_to(colors::red, 1000)
                                      .landing_color(colors::green)
                                      .repeat(1)
                                      .build_const();

/* Always mid-fade, so a hold of 0 everywhere on a playing sequencer. */
constexpr Sequence breathe = SequenceBuilder{}
                                 .fade_to(colors::rgbwhite, 1000)
                                 .fade_to(colors::black, 1000)
                                 .build_const();

/* The cookbook's mirror: the index flipped, so the far end leads. */
int32_t mirror(uint16_t index, uint16_t count, int32_t delay_q8) {
    return ((count - 1 - index) * delay_q8) >> 8;
}

int32_t at_int32_max(uint16_t, uint16_t, int32_t) { return INT32_MAX; }
int32_t at_int32_min(uint16_t, uint16_t, int32_t) { return INT32_MIN; }
int32_t at_max_offset(uint16_t, uint16_t, int32_t) { return max_offset_ms; }
int32_t at_min_offset(uint16_t, uint16_t, int32_t) { return min_offset_ms; }

Sample never_still(int32_t, uint16_t, uint16_t) { return {colors::red, 0}; }
Sample always_still(int32_t, uint16_t, uint16_t) { return {colors::red, hold_forever}; }

/* What a ColorFn was called with, per index. Recorded rather than encoded in
 * the color it returns, because gamma folds small channel values together and
 * would hide a wrong argument. */
struct Call {
    bool     seen;
    int32_t  playhead_ms;
    uint16_t count;
};
Call calls[kPixels];

Sample recorded(int32_t playhead_ms, uint16_t index, uint16_t count) {
    if (index < kPixels)
        calls[index] = {true, playhead_ms, count};
    return {colors::green, 0};
}

/* What a pixel at `position` shows, worked out in int64_t rather than through
 * the narrowed playhead: modulo the loop for an endless sequence, and pinned to
 * either end of int32_t for a finite one, which is past both of its ends. */
Color shown_at(const Sequence& s, int64_t position) {
    if (s.total_duration_ms() == hold_forever) {
        const int64_t loop = s.loop_duration_ms();
        return s.at(static_cast<int32_t>(((position % loop) + loop) % loop)).color;
    }
    const int64_t pinned = position > INT32_MAX ? INT32_MAX
                         : position < INT32_MIN ? INT32_MIN
                                                : position;
    return s.at(static_cast<int32_t>(pinned)).color;
}

/* Renders one segment alone on a kPixels strip into `buffer`. */
uint32_t render_alone(const Segment& s, uint8_t (&buffer)[kBytes], int64_t now_ms) {
    Strip strip(buffer);
    REQUIRE(strip.add(s));
    return strip.render(now_ms);
}

}  // namespace

/* ------------------------------------------------------------------------ */
/* hash                                                                      */
/* ------------------------------------------------------------------------ */

/* Reference values from an independent implementation of lowbias32, so a
 * mistyped constant cannot pass by agreeing with itself. */
static_assert(hash(0) == 0);
static_assert(hash(1) == 0x688990C0u);
static_assert(hash(2) == 0xD1132181u);
static_assert(hash(0xFFFFFFFFu) == 0x6768824Au);
static_assert(hash(0xDEADBEEFu) == 0xE628C683u);

/* The property the header promises, and the one a multiplicative hash lacks:
 * flipping any input bit can flip any output bit. A multiply alone never lets
 * a high input bit reach a low output bit. */
TEST_CASE("hash lets every input bit reach every output bit") {
    for (uint32_t bit = 0; bit < 32; ++bit) {
        CAPTURE(bit);
        uint32_t reached = 0;
        for (uint32_t x = 0; x < 1000; ++x)
            reached |= hash(x) ^ hash(x ^ (uint32_t{1} << bit));
        CHECK(reached == 0xFFFFFFFFu);
    }
}

/* ------------------------------------------------------------------------ */
/* spread                                                                    */
/* ------------------------------------------------------------------------ */

static_assert(spread(3000, 30) == 100 * 256, "the Rainbow recipe's delay is exact");
static_assert(spread(1000, 30) == 8533, "33.33 ms, to the nearest 1/256");
static_assert(spread(1000, 0) == 0);
static_assert(spread(0, 30) == 0);
static_assert(spread(8388607, 1) == 8388607 * 256, "the largest delay that fits");
static_assert(spread(8388608, 1) == INT32_MAX, "one more saturates");
static_assert(spread(UINT32_MAX, 1) == INT32_MAX);

TEST_CASE("spread keeps a ring's accumulated error under 1 ms up to 511 pixels") {
    /* The error across a ring is count * delay against the span, in 1/256 ms.
     * Rounding each pixel's share leaves it under half a unit, so the total
     * stays under 256 -- one millisecond -- while count < 512. */
    for (uint32_t total : {1u, 7u, 1000u, 1980u, 3000u, 65535u, 999999u, 12345678u,
                           (1u << 29) - 1, 1u << 29}) {
        for (uint16_t count = 1; count <= 511; ++count) {
            /* A span over 2^23 ms a pixel is past what a delay can store. */
            if (total / count >= (1u << 23))
                continue;
            CAPTURE(total);
            CAPTURE(count);
            const int64_t error = int64_t{spread(total, count)} * count - int64_t{total} * 256;
            CHECK((error < 0 ? -error : error) < 256);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* The setters, and the range of a pixel's offset                            */
/* ------------------------------------------------------------------------ */

TEST_CASE("the setters accept the largest stagger either way and refuse one more") {
    Sequencer seq;
    Segment s(0, kPixels, seq);
    CHECK(s.set_pixel_delay_q8(largest_q8));
    CHECK(s.set_pixel_delay_q8(-largest_q8));
    CHECK_FALSE(s.set_pixel_delay_q8(largest_q8 + 1));
    CHECK_FALSE(s.set_pixel_delay_q8(-largest_q8 - 1));
    CHECK_FALSE(s.set_pixel_delay_q8(INT32_MIN));

    /* In whole milliseconds, the same bound rounded down to a whole one. */
    CHECK(s.set_pixel_delay(largest_q8 / 256));
    CHECK_FALSE(s.set_pixel_delay(largest_q8 / 256 + 1));
}

TEST_CASE("the setters' bound is the product's, for every length of segment") {
    /* The setter takes the product in two halves, so its bound has corners a
     * single length does not reach: the high half alone over the limit, and
     * the low half carrying it over. Every span against the product in
     * int64_t, at the bound, one past it, and both ends of int32_t. */
    Sequencer seq;
    for (uint32_t count = 2; count <= UINT16_MAX; ++count) {
        CAPTURE(count);
        Segment s(0, static_cast<uint16_t>(count), seq);
        const int64_t span = count - 1;
        const int64_t bound = INT32_MAX / span;
        for (int64_t q8 : {bound, bound + 1, -bound, -bound - 1, int64_t{INT32_MAX},
                           int64_t{INT32_MIN}}) {
            /* One past the bound leaves int32_t when the span is 1. */
            if (q8 > INT32_MAX || q8 < INT32_MIN)
                continue;
            CAPTURE(q8);
            const int64_t product = q8 * span;
            CHECK(s.set_pixel_delay_q8(static_cast<int32_t>(q8)) ==
                  (product >= -INT32_MAX && product <= INT32_MAX));
        }
    }
}

TEST_CASE("a segment of one pixel takes any delay that can be stored") {
    /* No pixel is staggered, so nothing multiplies the delay, and it is free to
     * be a replacement's parameter. The millisecond setter still refuses a
     * delay that would not fit in 1/256 ms. */
    Sequencer seq;
    for (uint16_t count = 0; count <= 1; ++count) {
        CAPTURE(count);
        Segment s(0, count, seq);
        CHECK(s.set_pixel_delay_q8(INT32_MAX));
        CHECK(s.set_pixel_delay_q8(INT32_MIN));
        CHECK(s.set_pixel_delay(INT32_MAX / 256));
        CHECK(s.set_pixel_delay(-(INT32_MAX / 256)));
        CHECK_FALSE(s.set_pixel_delay(INT32_MAX / 256 + 1));
        CHECK_FALSE(s.set_pixel_delay(-(INT32_MAX / 256) - 1));
    }
}

TEST_CASE("a refused delay leaves the previous one in place") {
    Sequencer seq;
    seq.load(endless_777);
    seq.play(0);

    Segment kept(0, kPixels, seq);
    REQUIRE(kept.set_pixel_delay(30));
    CHECK_FALSE(kept.set_pixel_delay_q8(largest_q8 + 1));

    Segment fresh(0, kPixels, seq);
    REQUIRE(fresh.set_pixel_delay(30));

    uint8_t a[kBytes];
    uint8_t b[kBytes];
    render_alone(kept, a, 500);
    render_alone(fresh, b, 500);
    CHECK(same_bytes(a, b, kBytes));
}

TEST_CASE("set_pixel_delay is set_pixel_delay_q8 in units of 256") {
    Sequencer seq;
    seq.load(endless_777);
    seq.play(0);
    for (int32_t ms : {0, 1, 30, -30, 777, -1000}) {
        CAPTURE(ms);
        Segment a(0, kPixels, seq);
        Segment b(0, kPixels, seq);
        REQUIRE(a.set_pixel_delay(ms));
        REQUIRE(b.set_pixel_delay_q8(ms * 256));
        uint8_t fa[kBytes];
        uint8_t fb[kBytes];
        render_alone(a, fa, 1234);
        render_alone(b, fb, 1234);
        CHECK(same_bytes(fa, fb, kBytes));
    }
}

TEST_CASE("the default stagger floors, so a negative delay has no seam at 0") {
    /* A delay of -0.5 ms a pixel: pixel i looks 0.5 i ms ahead, floored, so
     * pixels 0 and 1 look at 0 and 1 ms ahead -- not both at 0, which is what
     * truncation toward zero would give. */
    constexpr Sequence red_then_blue =
        SequenceBuilder{}.hold(colors::red, 1).hold(colors::blue, 999).build_const();
    Sequencer seq;
    seq.load(red_then_blue);
    seq.seek(0);
    Segment s(0, kPixels, seq);
    REQUIRE(s.set_pixel_delay_q8(-128));
    uint8_t buffer[kBytes];
    render_alone(s, buffer, 0);
    CHECK(shows(buffer, 0, colors::red));
    CHECK(shows(buffer, 1, colors::blue));
}

TEST_CASE("removing an OffsetFn restores the default and keeps the delay") {
    Sequencer seq;
    seq.load(endless_777);
    seq.play(0);

    Segment restored(0, kPixels, seq);
    REQUIRE(restored.set_pixel_delay(40));
    restored.set_offset_fn(mirror);
    restored.set_offset_fn(nullptr);

    Segment plain(0, kPixels, seq);
    REQUIRE(plain.set_pixel_delay(40));

    Segment mirrored(0, kPixels, seq);
    REQUIRE(mirrored.set_pixel_delay(40));
    mirrored.set_offset_fn(mirror);

    uint8_t a[kBytes];
    uint8_t b[kBytes];
    uint8_t c[kBytes];
    render_alone(restored, a, 300);
    render_alone(plain, b, 300);
    render_alone(mirrored, c, 300);
    CHECK(same_bytes(a, b, kBytes));
    CHECK_FALSE(same_bytes(a, c, kBytes));
}

TEST_CASE("the largest stagger at either end of the sequencer's range is exact") {
    /* The comparison is with int64_t arithmetic, independent of the int32_t
     * arithmetic it checks. Signed overflow wraps silently wherever UBSan is
     * not watching, and a stagger that overflowed would still paint
     * something, just not this. */
    for (const Sequence* s : {&endless_777, &three_colors}) {
        for (int32_t sought : {INT32_MAX, INT32_MAX - 3, INT32_MIN, INT32_MIN + 3}) {
            for (int32_t q8 : {largest_q8, -largest_q8}) {
                CAPTURE(s->total_duration_ms());
                CAPTURE(sought);
                CAPTURE(q8);
                Sequencer seq;
                seq.load(*s);
                seq.seek(sought);
                Segment seg(0, kPixels, seq);
                REQUIRE(seg.set_pixel_delay_q8(q8));

                uint8_t buffer[kBytes];
                CHECK(render_alone(seg, buffer, 777) == hold_forever);
                for (uint16_t i = 0; i < kPixels; ++i) {
                    CAPTURE(i);
                    const int64_t offset = (int64_t{i} * q8) >> 8;
                    CHECK(shows(buffer, i, shown_at(*s, int64_t{sought} - offset)));
                }
            }
        }
    }
}

TEST_CASE("an OffsetFn's offset is clamped to the default's range") {
    /* Unclamped, INT32_MIN subtracted from a playhead near 2^30, or INT32_MAX
     * from one near -2^30, would leave int32_t. Clamped, each paints what the
     * clamp's own value paints. */
    struct Pair {
        OffsetFn extreme;
        OffsetFn clamped;
    };
    for (const Sequence* s : {&endless_777, &three_colors}) {
        for (int32_t sought : {INT32_MAX, INT32_MIN}) {
            for (Pair p : {Pair{at_int32_max, at_max_offset}, Pair{at_int32_min, at_min_offset}}) {
                CAPTURE(s->total_duration_ms());
                CAPTURE(sought);
                Sequencer seq;
                seq.load(*s);
                seq.seek(sought);

                Segment extreme(0, kPixels, seq);
                extreme.set_offset_fn(p.extreme);
                Segment clamped(0, kPixels, seq);
                clamped.set_offset_fn(p.clamped);

                uint8_t a[kBytes];
                uint8_t b[kBytes];
                render_alone(extreme, a, 0);
                render_alone(clamped, b, 0);
                CHECK(same_bytes(a, b, kBytes));
            }
        }
    }
}

/* ------------------------------------------------------------------------ */
/* The ColorFn                                                               */
/* ------------------------------------------------------------------------ */

TEST_CASE("a ColorFn is asked about each pixel's own playhead, index and count") {
    /* Placed away from pixel 0, so an index counted along the strip rather
     * than the segment shows, and staggered, so the unstaggered playhead
     * shows. */
    constexpr uint16_t first = 3;
    constexpr uint16_t count = 5;
    Sequencer seq;
    seq.load(endless_777);
    seq.play(0);
    Segment s(first, count, seq);
    REQUIRE(s.set_pixel_delay(30));
    s.set_color_fn(recorded);

    for (Call& c : calls)
        c = {};
    uint8_t buffer[kBytes];
    CHECK(render_alone(s, buffer, 1000) == 0);

    const int32_t playhead = seq.playhead(1000);
    for (uint16_t i = 0; i < kPixels; ++i) {
        CAPTURE(i);
        if (i < count) {
            REQUIRE(calls[i].seen);
            CHECK(calls[i].playhead_ms == playhead - 30 * i);
            CHECK(calls[i].count == count);
        } else {
            CHECK_FALSE(calls[i].seen);
        }
        const bool inside = i >= first && i < first + count;
        CHECK(shows(buffer, i, inside ? colors::green : colors::black));
    }
}

static_assert(timebase.total_duration_ms() == hold_forever, "timebase is endless");
static_assert(timebase.loop_duration_ms() == uint32_t{1} << 29, "the longest loop accepted");

TEST_CASE("timebase hands a ColorFn the time since play, until it wraps at 2^29 ms") {
    Sequencer seq;
    seq.load(timebase);
    seq.play(5000);
    Segment s(0, kPixels, seq);
    s.set_color_fn(recorded);
    uint8_t buffer[kBytes];

    constexpr int64_t wrap = int64_t{1} << 29;
    for (int64_t since : {int64_t{0}, int64_t{1}, int64_t{400}, int64_t{5} * 24 * 3600 * 1000,
                          wrap - 1, wrap, wrap + 400, 3 * wrap + 7}) {
        CAPTURE(since);
        for (Call& c : calls)
            c = {};
        render_alone(s, buffer, 5000 + since);
        REQUIRE(calls[0].seen);
        CHECK(calls[0].playhead_ms == since % wrap);
        CHECK_FALSE(s.finished(5000 + since));
    }

    /* Without the ColorFn, its one step shows. */
    s.set_color_fn(nullptr);
    CHECK(render_alone(s, buffer, 5000 + 1234) == static_cast<uint32_t>(wrap - 1234));
    for (uint16_t i = 0; i < kPixels; ++i)
        CHECK(shows(buffer, i, colors::black));
}

/* ------------------------------------------------------------------------ */
/* Holding still                                                             */
/* ------------------------------------------------------------------------ */

TEST_CASE("a segment whose sequencer is not playing holds forever") {
    Sequencer seq;
    seq.load(breathe);

    SUBCASE("through the default lookup") {
        Segment s(0, kPixels, seq);
        uint8_t buffer[kBytes];
        CHECK(render_alone(s, buffer, 500) == hold_forever);   /* never played */
        seq.play(0);
        CHECK(render_alone(s, buffer, 500) == 0);
        seq.pause(500);
        CHECK(render_alone(s, buffer, 900) == hold_forever);   /* paused */
        seq.seek(250);
        CHECK(render_alone(s, buffer, 900) == hold_forever);   /* sought */
    }

    SUBCASE("through a ColorFn that never holds") {
        Segment s(0, kPixels, seq);
        s.set_color_fn(never_still);
        uint8_t buffer[kBytes];
        CHECK(render_alone(s, buffer, 500) == hold_forever);
        seq.play(0);
        CHECK(render_alone(s, buffer, 500) == 0);
        seq.pause(500);
        CHECK(render_alone(s, buffer, 900) == hold_forever);
    }
}

/* ------------------------------------------------------------------------ */
/* finished()                                                                */
/* ------------------------------------------------------------------------ */

TEST_CASE("finished() comes when the last pixel lands, whichever pixel that is") {
    /* The wipe lands 200 ms after a pixel's playhead reaches 0. */
    Sequencer seq;
    seq.load(wipe);
    seq.play(0);
    Segment s(0, kPixels, seq);

    SUBCASE("a positive delay: the far end lands last") {
        REQUIRE(s.set_pixel_delay(30));
        CHECK_FALSE(s.finished(200 + 9 * 30 - 1));
        CHECK(s.finished(200 + 9 * 30));
    }
    SUBCASE("a negative delay: pixel 0 lands last") {
        REQUIRE(s.set_pixel_delay(-30));
        CHECK_FALSE(s.finished(199));
        CHECK(s.finished(200));
    }
    SUBCASE("the mirror's OffsetFn: its farthest pixel, index 0, lands last") {
        REQUIRE(s.set_pixel_delay(30));
        s.set_offset_fn(mirror);
        CHECK_FALSE(s.finished(200 + 9 * 30 - 1));
        CHECK(s.finished(200 + 9 * 30));
    }
}

TEST_CASE("the largest accepted stagger still finishes, after 60 days") {
    Sequencer seq;
    seq.load(wipe);
    seq.play(777777);
    for (int32_t q8 : {largest_q8, -largest_q8}) {
        CAPTURE(q8);
        Segment s(0, kPixels, seq);
        REQUIRE(s.set_pixel_delay_q8(q8));
        CHECK_FALSE(s.finished(777777 + 100));
        CHECK(s.finished(777777 + 60 * ms_per_day));
    }
}

TEST_CASE("finished() asks a ColorFn's holds, on a playing finite sequence only") {
    Sequencer seq;
    Segment s(0, kPixels, seq);

    SUBCASE("a ColorFn that never holds is never finished") {
        seq.load(wipe);
        seq.play(0);
        s.set_color_fn(never_still);
        CHECK_FALSE(s.finished(10 * ms_per_day));
    }
    SUBCASE("one that always holds is finished on a finite sequence at once") {
        seq.load(wipe);
        seq.play(0);
        s.set_color_fn(always_still);
        CHECK(s.finished(0));
    }
    SUBCASE("and never on an endless one") {
        seq.load(endless_777);
        seq.play(0);
        s.set_color_fn(always_still);
        CHECK_FALSE(s.finished(0));
        CHECK_FALSE(s.finished(10 * ms_per_day));
    }
}

TEST_CASE("a sequencer that is not playing is never finished") {
    /* Sought past the end, every pixel sits on the landing color and holds
     * forever -- and is holding still on purpose, so it must not be removed. */
    Sequencer seq;
    seq.load(wipe);
    seq.seek(100000);
    CHECK_FALSE(Segment(0, kPixels, seq).finished(0));

    seq.play(0);
    CHECK(Segment(0, kPixels, seq).finished(0));
    seq.pause(0);
    CHECK_FALSE(Segment(0, kPixels, seq).finished(0));
}

TEST_CASE("an endless sequence is never finished") {
    Sequencer seq;
    seq.load(endless_777);
    seq.play(0);
    CHECK_FALSE(Segment(0, kPixels, seq).finished(60 * ms_per_day));
}

TEST_CASE("a default-constructed segment is not playing anything") {
    Sequencer seq;
    const Segment s;
    CHECK_FALSE(s.finished(0));
    CHECK_FALSE(s.drives(seq));
}
