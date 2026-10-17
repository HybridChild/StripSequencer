/* Sequencer: the playhead, derived from a clock reading.
 *
 * The cases are grouped by what the sequencer promises. The calls move between
 * following the clock and standing still without the playhead jumping. The
 * position underneath is int64_t and is kept exactly, however far it runs.
 * What playhead() hands out is that position narrowed -- into one loop for an
 * endless sequence, to within 2^30 of zero for a finite one -- and nothing
 * downstream can tell the narrowed answer from the exact one. A 32-bit clock
 * reading is refused at compile time, because widening one keeps its wrap.
 *
 * The narrowing is checked against a table of positions rather than by
 * running a clock for days: the clock is a parameter, so day 60 is one call.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>   /* range-for over a braced list; gcc insists */

#include <stripseq/sequence.h>
#include <stripseq/sequencer.h>
#include <stripseq/strip.h>

#include "support.h"

using namespace stripseq;

namespace {

Sequence built(const SequenceBuilder& b) {
    Sequence s;
    REQUIRE(b.build(s) == Error::None);
    return s;
}

constexpr int64_t ms_per_day = 24 * 60 * 60 * 1000;

/* The limit a finite sequence's playhead is narrowed to, and the limits that
 * make it exact: the longest total the builder accepts, and the largest
 * stagger a segment accepts either way. A stagger is a q8 delay times a pixel
 * index within +-INT32_MAX, shifted down by 8, and the shift floors, so a
 * pixel can look up to 2^23 - 1 ms behind the sequencer and 2^23 ms ahead. */
constexpr int32_t limit_ms = int32_t{1} << 30;
constexpr uint32_t longest_total_ms = uint32_t{1} << 29;
constexpr int32_t largest_lag_ms = (int32_t{1} << 23) - 1;
constexpr int32_t largest_lead_ms = int32_t{1} << 23;

/* 777 ms a loop, which divides neither a power of two nor a day. */
constexpr Sequence endless_777 =
    SequenceBuilder{}.hold(colors::red, 400).fade_to(colors::green, 377).build_const();

/* The Wipe recipe's sequence: black, 200 ms of fade, then green for good. */
constexpr Sequence wipe = SequenceBuilder{}
                              .start_color(colors::black)
                              .fade_to(colors::green, 200)
                              .landing_color(colors::green)
                              .repeat(1)
                              .build_const();

/* Long enough that the worked examples below never reach its end. */
constexpr Sequence finite_long =
    SequenceBuilder{}.hold(colors::red, 100000).landing_color(colors::green).repeat(1).build_const();

int32_t mod_loop(int64_t position, uint32_t loop) {
    return static_cast<int32_t>(((position % loop) + loop) % loop);
}

/* Positions an int32_t or a uint32_t cannot hold, beside ones they can. */
constexpr int64_t two_31 = int64_t{1} << 31;
constexpr int64_t two_32 = int64_t{1} << 32;
constexpr int64_t two_40 = int64_t{1} << 40;

/* Whether a call compiles for a clock reading of type T. */
template <typename T>
concept plays_with = requires(Sequencer s, T t) { s.play(t); };
template <typename T>
concept pauses_with = requires(Sequencer s, T t) { s.pause(t); };
template <typename T>
concept reads_with = requires(const Sequencer s, T t) { s.playhead(t); };
template <typename T>
concept finishes_with = requires(const Segment s, T t) { s.finished(t); };
template <typename T>
concept renders_with = requires(Strip& s, T t) { s.render(t); };
template <typename T>
concept drops_with = requires(Strip& s, T t) { s.drop_finished(t); };

/* Whether load() compiles for an argument of type T. */
template <typename T>
concept loads = requires(Sequencer s, T t) { s.load(static_cast<T>(t)); };

template <typename T>
constexpr bool every_call_takes = plays_with<T> && pauses_with<T> && reads_with<T> &&
                                  finishes_with<T> && renders_with<T> && drops_with<T>;
template <typename T>
constexpr bool no_call_takes = !plays_with<T> && !pauses_with<T> && !reads_with<T> &&
                               !finishes_with<T> && !renders_with<T> && !drops_with<T>;

}  // namespace

/* ------------------------------------------------------------------------ */
/* The clock is 64 bits, and a narrower one is refused                       */
/* ------------------------------------------------------------------------ */

/* At namespace scope, so a regression is a compile error naming the line. */
static_assert(every_call_takes<int64_t>);
static_assert(every_call_takes<uint64_t>);
static_assert(every_call_takes<int>, "a literal such as 1000 must still be accepted");
static_assert(no_call_takes<uint32_t>, "k_uptime_get_32() must not compile");
static_assert(no_call_takes<uint16_t>);
static_assert(no_call_takes<uint8_t>);

/* ------------------------------------------------------------------------ */
/* load, and a sequencer that has never been told anything                   */
/* ------------------------------------------------------------------------ */

/* A sequencer refers to what it loads, so a temporary would dangle at the end
 * of the statement that loaded it. */
static_assert(loads<const Sequence&>);
static_assert(loads<Sequence&>);
static_assert(!loads<Sequence>, "a temporary must not compile");
static_assert(!loads<const Sequence&&>);

TEST_CASE("a default sequencer stands at 0, not playing, with no steps") {
    Sequencer seq;
    CHECK_FALSE(seq.playing());
    CHECK(seq.sequence().loop_duration_ms() == 0);
    for (int64_t now : {int64_t{0}, int64_t{1234}, two_40, INT64_MAX}) {
        CAPTURE(now);
        CHECK(seq.playhead(now) == 0);
    }
}

TEST_CASE("load resets the playhead to 0 and stops it") {
    Sequencer seq;
    seq.load(finite_long);
    seq.play(1000);
    REQUIRE(seq.playhead(3500) == 2500);

    seq.load(endless_777);
    CHECK_FALSE(seq.playing());
    CHECK(seq.sequence().loop_duration_ms() == 777);
    CHECK(seq.sequence().total_duration_ms() == hold_forever);
    for (int64_t now : {3500, 9999, 0}) {
        CAPTURE(now);
        CHECK(seq.playhead(now) == 0);
    }

    /* After a seek too: load is a fresh start whatever came before. */
    seq.seek(-4000);
    seq.load(wipe);
    CHECK(seq.playhead(0) == 0);
}

TEST_CASE("load refers to the sequence, so a rebuild into it shows at once") {
    Sequence s = built(SequenceBuilder{}.hold(colors::red, 1000));
    Sequencer seq;
    seq.load(s);
    seq.play(0);
    REQUIRE(seq.playhead(250) == 250);
    CHECK(&seq.sequence() == &s);

    /* Still playing, from where it was: rebuilding is not reloading. */
    REQUIRE(SequenceBuilder{}.hold(colors::blue, 500).hold(colors::green, 500).build(s) == Error::None);
    CHECK(seq.playing());
    CHECK(seq.playhead(750) == 750);
    CHECK(seq.sequence().at(seq.playhead(250)).color == colors::blue);
    CHECK(seq.sequence().at(seq.playhead(750)).color == colors::green);
}

TEST_CASE("no_sequence unloads: black, held forever, wherever the clock is") {
    Sequencer seq;
    seq.load(endless_777);
    seq.play(0);
    seq.load(no_sequence);
    for (const Sequencer& s : {seq, Sequencer{}}) {
        CHECK(&s.sequence() == &no_sequence);
        CHECK_FALSE(s.playing());
        for (int64_t now : {int64_t{0}, int64_t{1234}, two_40}) {
            CAPTURE(now);
            const Sample sample = s.sequence().at(s.playhead(now));
            CHECK(sample.color == colors::black);
            CHECK(sample.hold_ms == hold_forever);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* play, pause and seek                                                      */
/* ------------------------------------------------------------------------ */

TEST_CASE("the cookbook's worked example, call by call") {
    Sequencer seq;
    seq.load(finite_long);                CHECK(seq.playhead(1000) == 0);
    seq.play(1000);                         CHECK(seq.playhead(1300) == 300);
    seq.pause(1500);                        CHECK(seq.playhead(9999) == 500);
    seq.play(4000);                         CHECK(seq.playhead(4200) == 700);
    seq.seek(2000);                         CHECK(seq.playhead(5000) == 2000);
    seq.play(6000);                         CHECK(seq.playhead(6100) == 2100);
}

TEST_CASE("pause then play preserves the playhead exactly") {
    for (const Sequence* s : {&finite_long, &endless_777}) {
        Sequencer seq;
        seq.load(*s);
        seq.play(50);

        /* Pause at an awkward instant, sit out an awkward gap, carry on. */
        int64_t now = 50;
        for (int64_t i = 0; i < 40; ++i) {
            now += 37 * i + 11;
            const int32_t before = seq.playhead(now);
            seq.pause(now);
            CAPTURE(i);
            CHECK_FALSE(seq.playing());
            CHECK(seq.playhead(now) == before);
            CHECK(seq.playhead(now + 123456) == before);

            now += 1000 + 61 * i;
            seq.play(now);
            CHECK(seq.playing());
            CHECK(seq.playhead(now) == before);
        }
    }
}

TEST_CASE("pause keeps the position itself, not the narrowed playhead") {
    /* Paused 2^40 + 123 ms in, on a loop that does not divide it: a pause that
     * stored the narrowed answer would resume in phase too, so the check is
     * that the position carries on from the exact value, a second later. */
    Sequencer seq;
    seq.load(endless_777);
    seq.play(0);
    seq.pause(two_40 + 123);
    seq.play(5);
    CHECK(seq.playhead(1005) == mod_loop(two_40 + 1123, 777));

    /* Where the difference shows: a finite position far below the start,
     * which a narrowed pause would have pulled up to -2^30. */
    seq.load(wipe);
    seq.seek(INT32_MIN);
    seq.play(0);
    seq.pause(1000);
    REQUIRE(seq.playhead(1000) == -limit_ms);
    seq.play(0);
    CHECK(seq.playhead(two_31 - 1000 + 50) == 50);
}

TEST_CASE("seek reads back exactly within the narrowed range") {
    for (int32_t p : {-limit_ms, -1, 0, 1, 2000, limit_ms}) {
        Sequencer seq;
        seq.load(wipe);
        seq.play(100);
        seq.seek(p);
        CAPTURE(p);
        CHECK_FALSE(seq.playing());
        CHECK(seq.playhead(100) == p);
        CHECK(seq.playhead(500000) == p);   /* nothing is playing */
    }

    /* An endless sequence reads back within one loop, in phase. */
    for (int32_t p : {-778, -1, 0, 776, 777, 5000}) {
        Sequencer seq;
        seq.load(endless_777);
        seq.seek(p);
        CAPTURE(p);
        CHECK(seq.playhead(0) == mod_loop(p, 777));
    }
}

TEST_CASE("seek to either end of int32_t, then play") {
    for (int32_t p : {INT32_MIN, INT32_MIN + 5, INT32_MAX - 5, INT32_MAX}) {
        CAPTURE(p);

        Sequencer finite;
        finite.load(wipe);
        finite.seek(p);
        const int32_t expected = p < 0 ? -limit_ms : limit_ms;
        CHECK(finite.playhead(0) == expected);
        finite.play(1000);
        CHECK(finite.playhead(2000) == expected);
        CHECK(wipe.at(finite.playhead(2000)).color ==
              (p < 0 ? colors::black : colors::green));

        Sequencer endless;
        endless.load(endless_777);
        endless.seek(p);
        CHECK(endless.playhead(0) == mod_loop(p, 777));
        endless.play(1000);
        CHECK(endless.playhead(2000) == mod_loop(int64_t{p} + 1000, 777));
    }
}

TEST_CASE("play while playing and pause while paused change nothing") {
    Sequencer seq;
    seq.load(finite_long);
    seq.play(1000);

    seq.play(1700);   /* a restart would put the playhead back at 0 here */
    CHECK(seq.playing());
    CHECK(seq.playhead(1700) == 700);
    CHECK(seq.playhead(2000) == 1000);

    seq.pause(2500);
    REQUIRE(seq.playhead(2500) == 1500);
    seq.pause(9000);  /* a re-derived value would read 8000 */
    CHECK_FALSE(seq.playing());
    CHECK(seq.playhead(9000) == 1500);

    /* And after a seek, which leaves the sequencer paused. */
    seq.seek(42);
    seq.pause(12345);
    CHECK(seq.playhead(12345) == 42);
}

TEST_CASE("a negative playhead plays toward 0") {
    SUBCASE("finite: exactly, before the start") {
        Sequencer seq;
        seq.load(finite_long);
        seq.seek(-300);
        seq.play(1000);
        CHECK(seq.playhead(1100) == -200);
        CHECK(seq.playhead(1299) == -1);
        CHECK(seq.playhead(1300) == 0);
        CHECK(seq.playhead(1400) == 100);
    }

    SUBCASE("endless: wrapped into the loop, where at() would have put it") {
        Sequencer seq;
        seq.load(endless_777);
        seq.seek(-300);
        seq.play(1000);
        CHECK(seq.playhead(1100) == 577);
        CHECK(seq.playhead(1299) == 776);
        CHECK(seq.playhead(1300) == 0);
        CHECK(seq.playhead(1400) == 100);
        CHECK(endless_777.at(577).color == endless_777.at(-200).color);
    }
}

/* ------------------------------------------------------------------------ */
/* The narrowing                                                             */
/* ------------------------------------------------------------------------ */

TEST_CASE("the cookbook's endless example") {
    const Sequence red_black = built(SequenceBuilder{}.hold(colors::red, 250).hold(colors::black, 250));
    Sequencer seq;
    seq.load(red_black);
    seq.play(1000);
    CHECK(seq.playhead(2730) == 230);
    CHECK(seq.playhead(2746) == 246);
    CHECK(seq.playhead(3010) == 10);
}

/* The positions every narrowing case below is asked about: either side of
 * zero, a loop and a few, either side of the finite limit, where an int32_t
 * and a uint32_t would run out, and far beyond. */
constexpr int64_t positions[] = {
    -two_40, -two_32 - 1, -two_31 - 1, -two_31, -int64_t{limit_ms} - 1, -int64_t{limit_ms},
    -778, -777, -1, 0, 1, 776, 777, 778, 777 * 1000 + 5,
    longest_total_ms, int64_t{limit_ms}, int64_t{limit_ms} + 1,
    two_31 - 1, two_31, two_31 + 1, two_32 - 1, two_32, two_32 + 1,
    60 * ms_per_day, two_40, int64_t{1} << 62,
};

TEST_CASE("an endless playhead is its position modulo the loop, wherever it is") {
    const int64_t start = two_40;
    Sequencer seq;
    seq.load(endless_777);
    seq.play(start);
    for (int64_t position : positions) {
        CAPTURE(position);
        const int32_t p = seq.playhead(start + position);
        CHECK(p >= 0);
        CHECK(p < 777);
        CHECK(p == mod_loop(position, 777));

        /* Exact through at(), wherever an int32_t could hold the position. */
        if (position >= INT32_MIN && position <= INT32_MAX) {
            const Sample exact = endless_777.at(static_cast<int32_t>(position));
            CHECK(endless_777.at(p).color == exact.color);
            CHECK(endless_777.at(p).hold_ms == exact.hold_ms);
        }
    }
}

TEST_CASE("an endless playhead is exact on the longest loop and at either end of int64_t") {
    /* The remainder's own edges, which a 777 ms loop leaves out: a loop with
     * the most bits the builder allows, so that doubling the remainder comes
     * closest to the top of a uint32_t, and odd, so that no power of two
     * divides it; and positions whose high word is full. */
    constexpr Sequence longest_loop =
        SequenceBuilder{}.hold(colors::red, 1).fade_to(colors::green, longest_total_ms - 2).build_const();
    static_assert(longest_loop.loop_duration_ms() == longest_total_ms - 1);
    const uint32_t loop = longest_loop.loop_duration_ms();

    for (const Sequence* s : {&endless_777, &longest_loop}) {
        Sequencer seq;
        seq.load(*s);
        seq.play(0);
        for (int64_t position : {INT64_MIN, INT64_MIN + 1, -two_32 * loop, -two_32 * loop - 1,
                                 two_32 * loop - 1, two_32 * loop, INT64_MAX - 1, INT64_MAX}) {
            CAPTURE(position);
            CHECK(seq.playhead(position) == mod_loop(position, s->loop_duration_ms()));
        }
        for (int64_t position : positions) {
            CAPTURE(position);
            CHECK(seq.playhead(position) == mod_loop(position, s->loop_duration_ms()));
        }
    }
}

TEST_CASE("a finite playhead is its position clamped to 2^30 either side of 0") {
    const int64_t start = two_40;
    Sequencer seq;
    seq.load(wipe);
    seq.play(start);
    for (int64_t position : positions) {
        CAPTURE(position);
        const int32_t p = seq.playhead(start + position);
        const int64_t expected = position < -limit_ms ? -limit_ms
                               : position > limit_ms  ? limit_ms
                                                      : position;
        CHECK(p == expected);

        /* The color is exact everywhere; the hold is exact everywhere but a
         * start-color hold past the limit, which the clamp understates. */
        const Sample got = wipe.at(p);
        if (position < 0)
            CHECK(got.color == colors::black);
        else if (position >= 200)
            CHECK(got.color == colors::green);
        if (position >= -limit_ms && position <= INT32_MAX)
            CHECK(got.hold_ms == wipe.at(static_cast<int32_t>(position)).hold_ms);
    }
}

TEST_CASE("a finite sequence left playing for 60 days stays on its landing color") {
    /* The Wipe recipe's whole life, in the calls a strip would make: play, and
     * then nothing but reading, for longer than either 32-bit limit. */
    Sequencer seq;
    seq.load(wipe);
    seq.play(777777);
    for (int64_t day : {0, 1, 12, 13, 24, 25, 49, 50, 60}) {
        CAPTURE(day);
        const int32_t p = seq.playhead(777777 + 200 + day * ms_per_day);
        CHECK(wipe.at(p).color == colors::green);
        CHECK(wipe.at(p).hold_ms == hold_forever);
    }
    CHECK(seq.playhead(777777 + 60 * ms_per_day) == limit_ms);
}

TEST_CASE("a narrowed playhead plus or minus any stagger is still exact") {
    /* The arithmetic the limit rests on, and the room it leaves. */
    CHECK(int64_t{limit_ms} - largest_lag_ms > int64_t{longest_total_ms});
    CHECK(int64_t{limit_ms} + largest_lead_ms <= INT32_MAX);
    CHECK(-int64_t{limit_ms} + largest_lead_ms < 0);
    CHECK(-int64_t{limit_ms} - largest_lag_ms >= INT32_MIN);

    /* The builder does accept a total that long, and one more is refused. */
    Sequence longest;
    REQUIRE(SequenceBuilder{}.start_color(colors::black).hold(colors::red, longest_total_ms)
                .landing_color(colors::green).repeat(1).build(longest) == Error::None);
    CHECK(longest.total_duration_ms() == longest_total_ms);
    Sequence too_long;
    CHECK(SequenceBuilder{}.hold(colors::red, longest_total_ms + 1).repeat(1).build(too_long) ==
          Error::TooLong);

    /* So the most-staggered pixel at either limit shows the color at that end. */
    CHECK(longest.at(limit_ms - largest_lag_ms).color == colors::green);
    CHECK(longest.at(limit_ms - largest_lag_ms).hold_ms == hold_forever);
    CHECK(longest.at(-limit_ms + largest_lead_ms).color == colors::black);

    /* And an endless loop of the longest length still leaves room either way. */
    CHECK(int64_t{longest_total_ms} - 1 + largest_lead_ms <= INT32_MAX);
}

TEST_CASE("the playhead is continuous where a 32-bit clock would break") {
    /* Millisecond by millisecond across 2^31 and 2^32 ms of play, where an
     * int32_t difference turns negative and a uint32_t clock wraps. */
    for (int64_t edge : {two_31, two_32}) {
        Sequencer seq;
        seq.load(endless_777);
        seq.play(edge);   /* the clock itself crosses the same edge */
        int32_t last = seq.playhead(edge + edge - 1000);
        for (int64_t now = edge + edge - 999; now <= edge + edge + 1000; ++now) {
            const int32_t p = seq.playhead(now);
            if (p != (last + 1) % 777) {
                CAPTURE(edge);
                CAPTURE(now);
                CHECK(p == (last + 1) % 777);
                break;
            }
            last = p;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* The empty sequence                                                        */
/* ------------------------------------------------------------------------ */

TEST_CASE("an empty sequence left playing does not divide by zero") {
    Sequencer seq;
    seq.load(no_sequence);
    seq.play(5000);
    for (int64_t elapsed : {int64_t{0}, int64_t{1}, int64_t{999}, 20 * ms_per_day, two_40}) {
        CAPTURE(elapsed);
        CHECK(seq.playhead(5000 + elapsed) == 0);
        CHECK(seq.sequence().at(seq.playhead(5000 + elapsed)).color == colors::black);
    }
    seq.pause(5000 + 20 * ms_per_day);
    seq.play(0);
    CHECK(seq.playhead(1) == 0);
}
