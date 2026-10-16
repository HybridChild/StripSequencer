/* Sequencer: the playhead, derived from a clock reading.
 *
 * The cases are grouped by what the sequencer promises. The calls move between
 * following the clock and standing still without the playhead jumping. An
 * endless sequence's playhead is re-based by whole loops, which nothing
 * downstream can see. A finite one is capped at 2^30, where nothing downstream
 * can tell the difference either. The uint32_t clock wraps every 49.7 days
 * without any of it showing.
 *
 * The long runs step the clock in a prime stride rather than every
 * millisecond, so they stay fast in every variant, and check each answer
 * against the same position computed in int64_t with no re-basing at all.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>   /* range-for over a braced list; gcc insists */

#include <stripseq/sequence.h>
#include <stripseq/sequencer.h>

#include "support.h"

using namespace stripseq;

namespace {

Sequence built(const SequenceBuilder& b) {
    Sequence s;
    REQUIRE(b.build(s) == Error::None);
    return s;
}

constexpr uint32_t ms_per_day = 24u * 60u * 60u * 1000u;

/* The cap a finite sequence's playhead is held at, and the two limits that
 * make it safe: the longest total the builder accepts, and the largest stagger
 * a segment accepts, which is a q8 delay times a pixel index that still fits
 * in an int32_t, shifted down by 8. */
constexpr int32_t cap_ms = int32_t{1} << 30;
constexpr uint32_t longest_total_ms = uint32_t{1} << 29;
constexpr int32_t largest_stagger_ms = (int32_t{1} << 23) - 1;

/* 777 ms a loop, which divides neither the strides below nor a day. */
constexpr Sequence endless_777 =
    SequenceBuilder{}.hold(colors::red, 400).fade_to(colors::green, 377).build_const();

/* The Wipe recipe's sequence: 200 ms of fade, then green for good. */
constexpr Sequence wipe = SequenceBuilder{}
                              .start_color(colors::black)
                              .fade_to(colors::green, 200)
                              .landing_color(colors::green)
                              .repeat(1)
                              .build_const();

/* Long enough that the worked examples below never reach its end, and landing
 * on green like the wipe, so the two can share the 60-day run. */
constexpr Sequence finite_long =
    SequenceBuilder{}.hold(colors::red, 100000).landing_color(colors::green).repeat(1).build_const();

/* What a uint32_t millisecond clock reads `elapsed` after `start`: the true
 * time, wrapped every 49.7 days, as a real one would be. */
uint32_t clock_after(uint32_t start, int64_t elapsed) {
    return start + static_cast<uint32_t>(elapsed);
}

int32_t mod_loop(int64_t elapsed, uint32_t loop) {
    return static_cast<int32_t>(((elapsed % loop) + loop) % loop);
}

}  // namespace

/* ------------------------------------------------------------------------ */
/* load, and a sequencer that has never been told anything                   */
/* ------------------------------------------------------------------------ */

TEST_CASE("a default sequencer stands at 0, not playing, with no steps") {
    Sequencer seq;
    CHECK_FALSE(seq.playing());
    CHECK(seq.sequence().loop_duration_ms() == 0);
    for (uint32_t now : {0u, 1234u, UINT32_MAX}) {
        CAPTURE(now);
        CHECK(seq.playhead(now) == 0);
    }
}

TEST_CASE("load copies the sequence, resets the playhead to 0 and stops it") {
    Sequencer seq;
    seq.load(finite_long);
    seq.play(1000);
    REQUIRE(seq.playhead(3500) == 2500);

    seq.load(endless_777);
    CHECK_FALSE(seq.playing());
    CHECK(seq.sequence().loop_duration_ms() == 777);
    CHECK(seq.sequence().total_duration_ms() == hold_forever);
    for (uint32_t now : {3500u, 9999u, 0u}) {
        CAPTURE(now);
        CHECK(seq.playhead(now) == 0);
    }

    /* After a seek too: load is a fresh start whatever came before. */
    seq.seek(-4000);
    seq.load(wipe);
    CHECK(seq.playhead(0) == 0);
}

/* ------------------------------------------------------------------------ */
/* play, pause and seek                                                      */
/* ------------------------------------------------------------------------ */

TEST_CASE("the sketch's worked example, call by call") {
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
        uint32_t now = 50;
        for (uint32_t i = 0; i < 40; ++i) {
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

TEST_CASE("seek stops the playhead following the clock, and stores it exactly") {
    for (int32_t p : {INT32_MIN, -1, 0, 1, 2000, INT32_MAX - 5, INT32_MAX}) {
        Sequencer seq;
        seq.load(endless_777);
        seq.play(100);
        seq.seek(p);
        CAPTURE(p);
        CHECK_FALSE(seq.playing());
        CHECK(seq.playhead(100) == p);
        CHECK(seq.playhead(500000) == p);   /* not re-based: nothing is playing */
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

TEST_CASE("a negative playhead plays toward 0 and is left alone until then") {
    Sequencer seq;
    seq.load(endless_777);
    seq.seek(-300);
    seq.play(1000);
    CHECK(seq.playhead(1100) == -200);
    CHECK(seq.playhead(1299) == -1);
    CHECK(seq.playhead(1300) == 0);
    CHECK(seq.playhead(1400) == 100);
}

/* ------------------------------------------------------------------------ */
/* Endless: re-basing by whole loops                                         */
/* ------------------------------------------------------------------------ */

TEST_CASE("the sketch's re-base example") {
    Sequencer seq;
    seq.load(built(SequenceBuilder{}.hold(colors::red, 250).hold(colors::black, 250)));
    seq.play(1000);
    CHECK(seq.playhead(2730) == 230);
    CHECK(seq.playhead(2746) == 246);
    CHECK(seq.playhead(3010) == 10);
}

TEST_CASE("an endless playhead is always within one loop, from 0") {
    Sequencer seq;
    seq.load(endless_777);
    seq.play(0);
    for (uint32_t now = 0; now < 20000; now += 13) {
        const int32_t p = seq.playhead(now);
        CAPTURE(now);
        CHECK(p >= 0);
        CHECK(p < 777);
    }
}

TEST_CASE("re-basing over ten days matches the un-rebased arithmetic") {
    const Sequence& s = endless_777;
    const uint32_t loop = s.loop_duration_ms();
    const uint32_t start = 123456;

    Sequencer seq;
    seq.load(s);
    seq.play(start);

    /* Ten days is under 24.8, so the plain int32_t elapsed time is still a
     * valid playhead to compare against. */
    for (uint32_t elapsed = 0; elapsed <= 10 * ms_per_day; elapsed += 7919) {
        const int32_t p = seq.playhead(start + elapsed);
        if (p != mod_loop(elapsed, loop) ||
            !(s.at(p).color == s.at(static_cast<int32_t>(elapsed)).color)) {
            CAPTURE(elapsed);
            CHECK(p == mod_loop(elapsed, loop));
            CHECK(s.at(p).color == s.at(static_cast<int32_t>(elapsed)).color);
            break;
        }
    }
    CHECK(seq.playhead(start + 10 * ms_per_day) == mod_loop(10 * int64_t{ms_per_day}, loop));
}

/* ------------------------------------------------------------------------ */
/* The 49.7-day wrap                                                         */
/* ------------------------------------------------------------------------ */

TEST_CASE("the clock wrapping produces no discontinuity") {
    SUBCASE("finite, millisecond by millisecond across the wrap") {
        Sequencer seq;
        seq.load(finite_long);
        seq.play(UINT32_MAX - 100);
        int32_t last = seq.playhead(UINT32_MAX - 100);
        CHECK(last == 0);
        for (uint32_t now = UINT32_MAX - 99; now != 300; ++now) {
            const int32_t p = seq.playhead(now);
            if (p != last + 1) {
                CAPTURE(now);
                CHECK(p == last + 1);
                break;
            }
            last = p;
        }
        CHECK(seq.playhead(299) == 400);
    }

    SUBCASE("endless, for 60 days and so across the wrap at least once") {
        const uint32_t loop = endless_777.loop_duration_ms();
        const uint32_t start = UINT32_MAX - 5 * ms_per_day;

        Sequencer seq;
        seq.load(endless_777);
        seq.play(start);
        for (int64_t elapsed = 0; elapsed <= 60 * int64_t{ms_per_day}; elapsed += 7919) {
            const uint32_t now = clock_after(start, elapsed);
            const int32_t p = seq.playhead(now);
            if (p != mod_loop(elapsed, loop)) {
                CAPTURE(elapsed);
                CHECK(p == mod_loop(elapsed, loop));
                break;
            }
        }
    }

    SUBCASE("paused across the wrap and resumed after it") {
        Sequencer seq;
        seq.load(finite_long);
        seq.play(UINT32_MAX - 1000);
        seq.pause(UINT32_MAX - 400);
        REQUIRE(seq.playhead(UINT32_MAX - 400) == 600);
        seq.play(5000);
        CHECK(seq.playhead(5000) == 600);
        CHECK(seq.playhead(5050) == 650);
    }
}

/* ------------------------------------------------------------------------ */
/* Finite: the cap at 2^30                                                   */
/* ------------------------------------------------------------------------ */

TEST_CASE("a capped playhead minus any stagger is past any accepted total") {
    /* The arithmetic the cap rests on, and the room it leaves. */
    CHECK(int64_t{cap_ms} - largest_stagger_ms > int64_t{longest_total_ms});

    /* The builder does accept a total that long, and one more is refused. */
    Sequence longest;
    REQUIRE(SequenceBuilder{}.hold(colors::red, longest_total_ms).landing_color(colors::green)
                .repeat(1).build(longest) == Error::None);
    CHECK(longest.total_duration_ms() == longest_total_ms);
    Sequence too_long;
    CHECK(SequenceBuilder{}.hold(colors::red, longest_total_ms + 1).repeat(1).build(too_long) ==
          Error::TooLong);

    /* So the most-staggered pixel of a capped segment shows the landing color. */
    CHECK(longest.at(cap_ms - largest_stagger_ms).color == colors::green);
    CHECK(longest.at(cap_ms - largest_stagger_ms).hold_ms == hold_forever);
}

TEST_CASE("a finite sequence left playing for 60 days stays on its landing color") {
    for (const Sequence* s : {&wipe, &finite_long}) {
        const uint32_t start = 777777;

        Sequencer seq;
        seq.load(*s);
        seq.play(start);
        bool capped = false;
        for (int64_t elapsed = 0; elapsed <= 60 * int64_t{ms_per_day}; elapsed += 7919) {
            const uint32_t now = clock_after(start, elapsed);
            const int32_t p = seq.playhead(now);

            /* Exact until the cap, then the cap for good. */
            const int32_t expected =
                elapsed > cap_ms ? cap_ms : static_cast<int32_t>(elapsed);
            const bool landed = elapsed <= s->total_duration_ms() ||
                                (s->at(p).color == colors::green &&
                                 s->at(p).hold_ms == hold_forever);
            if (p != expected || !landed) {
                CAPTURE(elapsed);
                CHECK(p == expected);
                CHECK(landed);
                break;
            }
            capped = capped || p == cap_ms;
        }
        CHECK(capped);
        const int32_t at_61_days = seq.playhead(clock_after(start, 61 * int64_t{ms_per_day}));
        CHECK(at_61_days == cap_ms);
        CHECK(s->at(at_61_days).color == colors::green);

        /* Pausing at the cap and playing again stays there too. */
        seq.pause(clock_after(start, 62 * int64_t{ms_per_day}));
        CHECK(seq.playhead(0) == cap_ms);
        seq.play(12345);
        CHECK(seq.playhead(12345 + 10 * ms_per_day) == cap_ms);
    }
}

/* ------------------------------------------------------------------------ */
/* play() moves the origin once, the instant it starts following the clock   */
/* ------------------------------------------------------------------------ */

TEST_CASE("play after a seek near INT32_MAX does not overflow") {
    /* The first playhead() call comes a second after play: the raw elapsed
     * time would have passed INT32_MAX 994 ms earlier. */
    const int32_t sought = INT32_MAX - 5;

    SUBCASE("finite: held at the cap, on its landing color") {
        Sequencer seq;
        seq.load(wipe);
        seq.seek(sought);
        CHECK(seq.playhead(1000) == sought);   /* seek itself stays exact */
        seq.play(1000);
        const int32_t p = seq.playhead(2000);
        CHECK(p == cap_ms);
        CHECK(wipe.at(p).color == colors::green);
    }

    SUBCASE("endless: in phase with where it was sought") {
        const uint32_t loop = endless_777.loop_duration_ms();
        Sequencer seq;
        seq.load(endless_777);
        seq.seek(sought);
        seq.play(1000);
        CHECK(seq.playhead(2000) == mod_loop(int64_t{sought} + 1000, loop));
        CHECK(endless_777.at(seq.playhead(2000)).color ==
              endless_777.at(mod_loop(int64_t{sought} + 1000, loop)).color);
    }

    SUBCASE("play across the clock's wrap as well") {
        const uint32_t loop = endless_777.loop_duration_ms();
        Sequencer seq;
        seq.load(endless_777);
        seq.seek(sought);
        seq.play(UINT32_MAX - 200);
        CHECK(seq.playhead(800) == mod_loop(int64_t{sought} + 1001, loop));
    }
}

/* ------------------------------------------------------------------------ */
/* The empty sequence                                                        */
/* ------------------------------------------------------------------------ */

TEST_CASE("an empty sequence left playing does not divide by zero") {
    Sequencer seq;
    seq.load(Sequence{});
    seq.play(5000);
    for (uint32_t elapsed : {0u, 1u, 999u, 1000000u, 20 * ms_per_day}) {
        CAPTURE(elapsed);
        CHECK(seq.playhead(5000 + elapsed) == static_cast<int32_t>(elapsed));
        CHECK(seq.sequence().at(seq.playhead(5000 + elapsed)).color == colors::black);
    }
    seq.pause(5000 + 20 * ms_per_day);
    seq.play(0);
    CHECK(seq.playhead(1) == static_cast<int32_t>(20 * ms_per_day) + 1);
}
