/* Sequence::at() and SequenceBuilder.
 *
 * at() is the function every other part of the library exists to call, and
 * every semantic docs/DESIGN.md argues for in prose is decided inside it: the
 * ring an endless sequence makes of negative playheads, the two ends of a
 * finite one, and the hold each case reports. The cases below are grouped by
 * those, then by what the builder accepts and refuses.
 *
 * Nothing here assumes STRIPSEQ_MAX_STEPS is 8: the tight variant builds with
 * 6, and no sequence below has more steps than that unless it is counting up
 * to the capacity on purpose.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>   /* range-for over a braced list; gcc insists */

#include <stripseq/sequence.h>

#include "support.h"

using namespace stripseq;

namespace {

Sequence built(const SequenceBuilder& b) {
    Sequence s;
    REQUIRE(b.build(s) == Error::None);
    return s;
}

Error error_of(const SequenceBuilder& b) {
    Sequence s;
    return b.build(s);
}

/* One channel at full scale, the others dark: a fade from black to this is
 * a fade of the red channel alone, from 0 to 65535. */
constexpr Color red_only = Color::rgb(65535, 0, 0);

}  // namespace

/* ------------------------------------------------------------------------ */
/* The empty sequence                                                        */
/* ------------------------------------------------------------------------ */

TEST_CASE("an empty sequence is black, forever, at every playhead") {
    const Sequence s;
    for (int32_t p : {INT32_MIN, -1, 0, 1, INT32_MAX}) {
        CAPTURE(p);
        CHECK(s.at(p).color == colors::black);
        CHECK(s.at(p).hold_ms == hold_forever);
    }
    CHECK(s.loop_duration_ms() == 0);
    CHECK(s.total_duration_ms() == hold_forever);   /* endless, by default */
}

/* ------------------------------------------------------------------------ */
/* Durations                                                                 */
/* ------------------------------------------------------------------------ */

TEST_CASE("loop and total durations") {
    const SequenceBuilder steps = SequenceBuilder{}.hold(colors::red, 500).hold(colors::black, 300);

    const Sequence endless = built(steps);
    CHECK(endless.loop_duration_ms() == 800);
    CHECK(endless.total_duration_ms() == hold_forever);

    const Sequence thrice = built(SequenceBuilder{steps}.repeat(3));
    CHECK(thrice.loop_duration_ms() == 800);
    CHECK(thrice.total_duration_ms() == 2400);

    /* endless() after repeat() is the last word, not an error. */
    CHECK(built(SequenceBuilder{steps}.repeat(3).endless()).total_duration_ms() == hold_forever);
}

/* ------------------------------------------------------------------------ */
/* Endless: the timeline is a ring                                           */
/* ------------------------------------------------------------------------ */

TEST_CASE("endless steps: color and hold inside each step, and at its boundaries") {
    const Sequence s = built(SequenceBuilder{}.hold(colors::red, 500).hold(colors::black, 500));

    CHECK(s.at(0).color == colors::red);
    CHECK(s.at(0).hold_ms == 500);       /* a step holds to its own end */
    CHECK(s.at(499).color == colors::red);
    CHECK(s.at(499).hold_ms == 1);
    CHECK(s.at(500).color == colors::black);   /* a boundary belongs to the later step */
    CHECK(s.at(500).hold_ms == 500);
    CHECK(s.at(999).hold_ms == 1);
    CHECK(s.at(1000).color == colors::red);    /* and the loop's end is the next start */
    CHECK(s.at(1000).hold_ms == 500);
}

TEST_CASE("endless: a negative playhead wraps rather than falling off") {
    const Sequence s = built(SequenceBuilder{}.hold(colors::red, 500).hold(colors::black, 500));

    CHECK(s.at(-1).color == colors::black);     /* 999 */
    CHECK(s.at(-1).hold_ms == 1);
    CHECK(s.at(-500).color == colors::black);   /* 500, exactly on a boundary */
    CHECK(s.at(-500).hold_ms == 500);
    CHECK(s.at(-501).color == colors::red);     /* 499 */
    CHECK(s.at(-501).hold_ms == 1);
    CHECK(s.at(-1000).color == colors::red);    /* 0 */
    CHECK(s.at(-1000).hold_ms == 500);
}

TEST_CASE("endless: far-negative and far-positive playheads land in the right place") {
    /* -1 alone would not catch the trap: `p % loop` is negative for every
     * negative p, and a fix that only handles small ones -- adding one loop,
     * say -- passes at -1 and fails here. */
    const Sequence s = built(SequenceBuilder{}.hold(colors::red, 500).hold(colors::black, 500));

    CHECK(s.at(-1'000'000'007).color == colors::black);   /* 993 */
    CHECK(s.at(-1'000'000'007).hold_ms == 7);
    CHECK(s.at(INT32_MIN).color == colors::red);          /* -2147483648 = 352 mod 1000 */
    CHECK(s.at(INT32_MIN).hold_ms == 148);
    CHECK(s.at(INT32_MAX).color == colors::black);        /* 2147483647 = 647 mod 1000 */
    CHECK(s.at(INT32_MAX).hold_ms == 353);
}

TEST_CASE("endless: shifting a playhead by whole loops changes nothing") {
    /* The property that lets the sequencer narrow an endless playhead modulo
     * the loop. */
    const Sequence s = built(SequenceBuilder{}
                                 .hold(colors::red, 70)
                                 .fade_to(colors::blue, 130)
                                 .fade_to(colors::green, 300, Transition::EaseInOut));
    const int32_t loop = static_cast<int32_t>(s.loop_duration_ms());
    REQUIRE(loop == 500);

    for (int32_t p = 0; p < loop; ++p) {
        for (int32_t k : {-4'000'000, -3, -1, 1, 2, 4'000'000}) {
            CAPTURE(p);
            CAPTURE(k);
            CHECK(s.at(p + k * loop).color == s.at(p).color);
            CHECK(s.at(p + k * loop).hold_ms == s.at(p).hold_ms);
        }
    }
}

TEST_CASE("endless: the first step fades from the last step's color") {
    const Sequence s =
        built(SequenceBuilder{}.fade_to(red_only, 1000).fade_to(colors::black, 1000));

    CHECK(s.at(0).color == colors::black);      /* from black, the last step */
    CHECK(s.at(0).hold_ms == 0);                /* mid-fade asks for frame rate */
    CHECK(s.at(500).color.r == 32767);          /* t = 500 * 65535 / 1000 */
    CHECK(s.at(500).hold_ms == 0);
    CHECK(s.at(1000).color == red_only);        /* the second fade starts from red */
    CHECK(s.at(1500).color.r == 32768);
    CHECK(s.at(-500).color.r == 32768);         /* and wraps into it backwards */
}

TEST_CASE("a zero-length step is never shown, but it is what the next step fades from") {
    /* Endless, with the instant step last: every pass jumps to red at the
     * loop's end and fades from there. */
    const Sequence s = built(SequenceBuilder{}.fade_to(colors::blue, 100).hold(red_only, 0));

    CHECK(s.at(0).color == red_only);
    CHECK(s.at(50).color.r == 32768);
    CHECK(s.at(50).color.b == 32767);
    CHECK(s.at(99).color.b > 64000);

    /* The same in the middle of a loop. */
    const Sequence mid = built(SequenceBuilder{}
                                   .hold(colors::green, 100)
                                   .hold(red_only, 0)
                                   .fade_to(colors::black, 100));
    CHECK(mid.at(99).color == colors::green);
    CHECK(mid.at(100).color == red_only);
    CHECK(mid.at(150).color.r == 32768);
    CHECK(mid.at(150).color.g == 0);
}

/* ------------------------------------------------------------------------ */
/* Finite: the timeline is a line                                            */
/* ------------------------------------------------------------------------ */

TEST_CASE("finite: the wipe, before, during and after") {
    const Sequence s = built(SequenceBuilder{}
                                 .start_color(colors::black)
                                 .fade_to(colors::green, 200)
                                 .landing_color(colors::green)
                                 .repeat(1));

    /* Before the start the hold is the time until this pixel starts moving --
     * a real deadline rather than a frame rate. */
    CHECK(s.at(-1).color == colors::black);
    CHECK(s.at(-1).hold_ms == 1);
    CHECK(s.at(-30'000).hold_ms == 30'000);
    CHECK(s.at(INT32_MIN).color == colors::black);
    CHECK(s.at(INT32_MIN).hold_ms == 2'147'483'648u);

    /* At zero the first fade has started, from start_color. */
    CHECK(s.at(0).color == colors::black);
    CHECK(s.at(0).hold_ms == 0);
    CHECK(s.at(100).color.g == 32767);
    CHECK(s.at(100).hold_ms == 0);

    /* From the end onwards, landed. */
    CHECK(s.at(200).color == colors::green);
    CHECK(s.at(200).hold_ms == hold_forever);
    CHECK(s.at(INT32_MAX).color == colors::green);
    CHECK(s.at(INT32_MAX).hold_ms == hold_forever);
}

TEST_CASE("finite: start and landing colors are their own, not the steps'") {
    const Sequence s = built(SequenceBuilder{}
                                 .start_color(colors::blue)
                                 .hold(colors::red, 100)
                                 .landing_color(colors::green)
                                 .repeat(1));

    CHECK(s.at(-1).color == colors::blue);
    CHECK(s.at(0).color == colors::red);
    CHECK(s.at(0).hold_ms == 100);
    CHECK(s.at(100).color == colors::green);
    CHECK(s.at(100).hold_ms == hold_forever);
}

TEST_CASE("finite: unset start and landing colors are black before, the last step after") {
    const SequenceBuilder fade =
        SequenceBuilder{}.fade_to(colors::red, 100).fade_to(colors::green, 100);

    const Sequence neither = built(SequenceBuilder{fade}.repeat(1));
    CHECK(neither.at(-5).color == colors::black);
    CHECK(neither.at(-5).hold_ms == 5);
    CHECK(neither.at(0).color == colors::black);     /* and the first fade starts there */
    CHECK(neither.at(200).color == colors::green);
    CHECK(neither.at(200).hold_ms == hold_forever);

    const Sequence start_only = built(SequenceBuilder{fade}.start_color(colors::blue).repeat(1));
    CHECK(start_only.at(-5).color == colors::blue);
    CHECK(start_only.at(200).color == colors::green);

    const Sequence landing_only =
        built(SequenceBuilder{fade}.landing_color(colors::blue).repeat(1));
    CHECK(landing_only.at(-5).color == colors::black);
    CHECK(landing_only.at(200).color == colors::blue);

    /* Black is a real choice, not "unset": setting it explicitly must not be
     * mistaken for leaving the default. */
    const Sequence black_landing =
        built(SequenceBuilder{fade}.landing_color(colors::black).repeat(1));
    CHECK(black_landing.at(200).color == colors::black);
}

TEST_CASE("finite with repeats: only the first pass starts from start_color") {
    const Sequence s = built(SequenceBuilder{}
                                 .start_color(colors::green)
                                 .fade_to(red_only, 100)
                                 .fade_to(colors::black, 100)
                                 .repeat(2));
    REQUIRE(s.total_duration_ms() == 400);

    CHECK(s.at(0).color == colors::green);   /* pass 1, from start_color */
    CHECK(s.at(50).color.g == 32768);
    CHECK(s.at(200).color == colors::black);   /* pass 2, from where pass 1 ended */
    CHECK(s.at(250).color.r == 32767);
    CHECK(s.at(250).color.g == 0);
    CHECK(s.at(399).hold_ms == 0);
    CHECK(s.at(400).color == colors::black);   /* landed: the last step's color */
    CHECK(s.at(400).hold_ms == hold_forever);
}

TEST_CASE("finite: the alert flashes three times and lands") {
    const Sequence s = built(SequenceBuilder{}
                                 .hold(colors::red, 120)
                                 .hold(colors::black, 120)
                                 .repeat(3));

    for (int32_t pass = 0; pass < 3; ++pass) {
        CAPTURE(pass);
        CHECK(s.at(pass * 240).color == colors::red);
        CHECK(s.at(pass * 240).hold_ms == 120);
        CHECK(s.at(pass * 240 + 239).color == colors::black);
        CHECK(s.at(pass * 240 + 239).hold_ms == 1);
    }
    CHECK(s.at(720).color == colors::black);
    CHECK(s.at(720).hold_ms == hold_forever);
}

/* ------------------------------------------------------------------------ */
/* Easing                                                                    */
/* ------------------------------------------------------------------------ */

/* A fade of 65535 ms from black to full red makes the red channel *be* the
 * curve: t equals the elapsed milliseconds exactly, and a lerp from 0 to 65535
 * returns t unchanged. So every value the curve can take is one at() away, and
 * the invariants are checked exhaustively rather than on a sample.
 *
 * Each is finite, so that the fade starts from black. Endless, a one-step
 * sequence fades from its own last color -- itself -- and is solid red. */
TEST_CASE("every curve is exact at both ends and monotonic in between") {
    for (Transition kind : {Transition::Linear, Transition::EaseIn,
                            Transition::EaseOut, Transition::EaseInOut}) {
        CAPTURE(static_cast<int>(kind));
        const Sequence s = built(SequenceBuilder{}.fade_to(red_only, 65535, kind).repeat(1));

        CHECK(s.at(0).color.r == 0);

        uint16_t prev = 0;
        bool monotonic = true;
        for (int32_t p = 0; p < 65535; ++p) {
            const uint16_t r = s.at(p).color.r;
            monotonic = monotonic && r >= prev;
            prev = r;
        }
        CHECK(monotonic);

        /* No tick at the boundary: the last value inside the fade is within
         * a couple of LSB of the target the next instant shows exactly. */
        CHECK(prev >= 65533);
        CHECK(s.at(65535).color.r == 65535);
    }
}

TEST_CASE("the curves have the shapes their names promise") {
    const auto mid = [](Transition kind) {
        return built(SequenceBuilder{}.fade_to(red_only, 65535, kind).repeat(1)).at(32768).color.r;
    };
    CHECK(mid(Transition::Linear) == 32768);
    CHECK(mid(Transition::EaseIn) == 16384);       /* t^2: a quarter at half way */
    CHECK(mid(Transition::EaseOut) == 49152);      /* 1-(1-t)^2: three quarters */
    CHECK(mid(Transition::EaseInOut) == 32769);    /* 1 - 2(1-t)^2, just past half */
    CHECK(built(SequenceBuilder{}.fade_to(red_only, 65535, Transition::EaseInOut).repeat(1))
              .at(16384).color.r == 8192);         /* 2t^2: an eighth at a quarter */

    /* EaseInOut is EaseIn and EaseOut joined at the middle, so it is exactly
     * symmetric about it: each half is the other turned through 180 degrees. */
    const Sequence s =
        built(SequenceBuilder{}.fade_to(red_only, 65535, Transition::EaseInOut).repeat(1));
    bool symmetric = true;
    for (int32_t p = 0; p <= 65535; ++p) {
        symmetric = symmetric && s.at(p).color.r + s.at(65535 - p).color.r == 65535;
    }
    CHECK(symmetric);
}

/* The same 65535 ms fade shows the public curve directly, so a ColorFn that
 * calls ease() gets exactly the curve a step's fade follows. */
TEST_CASE("ease() is the curve at() applies") {
    for (Transition kind : {Transition::Linear, Transition::EaseIn,
                            Transition::EaseOut, Transition::EaseInOut}) {
        CAPTURE(static_cast<int>(kind));
        const Sequence s = built(SequenceBuilder{}.fade_to(red_only, 65535, kind).repeat(1));
        bool same = true;
        for (int32_t p = 0; p < 65535; ++p)
            same = same && s.at(p).color.r == ease(kind, static_cast<uint16_t>(p));
        CHECK(same);
        CHECK(ease(kind, 65535) == 65535);
    }
}

static_assert(ease(Transition::Step, 1234) == 1234);
static_assert(ease(Transition::EaseIn, 32768) == 16384);

TEST_CASE("a fade longer than a minute stays monotonic and reaches its target") {
    /* Past 65536 ms the fade's progress is computed on shifted values, so it
     * is checked separately from the exhaustive case above. */
    const uint32_t ms = 1'000'000;
    const Sequence s = built(SequenceBuilder{}.fade_to(red_only, ms).repeat(1));

    uint16_t prev = 0;
    bool monotonic = true;
    for (int32_t p = 0; p < static_cast<int32_t>(ms); ++p) {
        const uint16_t r = s.at(p).color.r;
        monotonic = monotonic && r >= prev;
        prev = r;
    }
    CHECK(monotonic);
    CHECK(s.at(0).color.r == 0);
    CHECK(s.at(static_cast<int32_t>(ms / 2)).color.r == 32767);
    CHECK(prev >= 65533);
}

/* ------------------------------------------------------------------------ */
/* The builder: what it refuses                                              */
/* ------------------------------------------------------------------------ */

TEST_CASE("NoSteps") {
    CHECK(error_of(SequenceBuilder{}) == Error::NoSteps);
    CHECK(error_of(SequenceBuilder{}.repeat(2)) == Error::NoSteps);
}

TEST_CASE("TooManySteps, counted against this build's capacity") {
    SequenceBuilder b;
    for (int i = 0; i < STRIPSEQ_MAX_STEPS; ++i)
        b.hold(colors::red, 10);
    CHECK(error_of(b) == Error::None);

    b.hold(colors::red, 10);
    CHECK(error_of(b) == Error::TooManySteps);
}

TEST_CASE("ZeroDurationFade: a Step may be instant, a fade may not") {
    /* Finite, so that the loop's own length is not the question here. */
    CHECK(error_of(SequenceBuilder{}.hold(colors::red, 0).repeat(1)) == Error::None);
    CHECK(error_of(SequenceBuilder{}.fade_to(colors::red, 0, Transition::Step).repeat(1)) ==
          Error::None);

    for (Transition kind : {Transition::Linear, Transition::EaseIn,
                            Transition::EaseOut, Transition::EaseInOut}) {
        CAPTURE(static_cast<int>(kind));
        CHECK(error_of(SequenceBuilder{}.fade_to(colors::red, 0, kind)) == Error::ZeroDurationFade);
    }
}

TEST_CASE("ZeroRepeatCount") {
    CHECK(error_of(SequenceBuilder{}.hold(colors::red, 10).repeat(0)) == Error::ZeroRepeatCount);
}

TEST_CASE("LandingColorOnEndless and StartColorOnEndless, even in black") {
    const SequenceBuilder steps = SequenceBuilder{}.hold(colors::red, 10);

    const auto endless = [&steps] { return SequenceBuilder{steps}; };

    CHECK(error_of(endless().landing_color(colors::green)) == Error::LandingColorOnEndless);
    CHECK(error_of(endless().start_color(colors::green)) == Error::StartColorOnEndless);

    /* Black is the case a "color is non-zero" test for set-ness would miss. */
    CHECK(error_of(endless().landing_color(colors::black)) == Error::LandingColorOnEndless);
    CHECK(error_of(endless().start_color(colors::black)) == Error::StartColorOnEndless);

    /* Endless is judged at build(), so the order of the calls does not matter
     * -- including endless() undoing an earlier repeat(). */
    CHECK(error_of(SequenceBuilder{steps}.landing_color(colors::green).repeat(1)) == Error::None);
    CHECK(error_of(SequenceBuilder{steps}.repeat(1).landing_color(colors::green).endless()) ==
          Error::LandingColorOnEndless);
}

TEST_CASE("ZeroLengthLoop: an endless sequence has to take some time to go round") {
    CHECK(error_of(SequenceBuilder{}.hold(colors::red, 0)) == Error::ZeroLengthLoop);
    CHECK(error_of(SequenceBuilder{}.hold(colors::red, 0).hold(colors::blue, 0)) ==
          Error::ZeroLengthLoop);
    CHECK(error_of(SequenceBuilder{}.hold(colors::red, 0).hold(colors::blue, 1)) == Error::None);
}

TEST_CASE("finite, a sequence of 0 ms is a clean edge at zero") {
    const Sequence s = built(SequenceBuilder{}
                                 .start_color(colors::blue)
                                 .hold(colors::red, 0)
                                 .repeat(3));
    CHECK(s.total_duration_ms() == 0);
    CHECK(s.at(-1).color == colors::blue);
    CHECK(s.at(-1).hold_ms == 1);
    CHECK(s.at(0).color == colors::red);       /* landed: the last step's color */
    CHECK(s.at(0).hold_ms == hold_forever);
}

TEST_CASE("the first error sticks, and later calls cannot clear it") {
    CHECK(error_of(SequenceBuilder{}.fade_to(colors::red, 0).repeat(0)) == Error::ZeroDurationFade);
    CHECK(error_of(SequenceBuilder{}.repeat(0).fade_to(colors::red, 0)) == Error::ZeroRepeatCount);
    CHECK(error_of(SequenceBuilder{}.repeat(0).repeat(2).hold(colors::red, 10)) ==
          Error::ZeroRepeatCount);

    SequenceBuilder full;
    for (int i = 0; i <= STRIPSEQ_MAX_STEPS; ++i)
        full.hold(colors::red, 10);
    CHECK(error_of(full.repeat(0)) == Error::TooManySteps);
}

TEST_CASE("a failed build leaves its output untouched") {
    Sequence s = built(SequenceBuilder{}.hold(colors::red, 500));
    CHECK(SequenceBuilder{}.fade_to(colors::blue, 0).build(s) == Error::ZeroDurationFade);
    CHECK(s.at(0).color == colors::red);
    CHECK(s.loop_duration_ms() == 500);
}

/* ------------------------------------------------------------------------ */
/* TooLong: 2^29 ms                                                          */
/* ------------------------------------------------------------------------ */

TEST_CASE("TooLong: one endless loop, accepted at exactly 2^29 ms") {
    constexpr uint32_t limit = 1u << 29;

    CHECK(error_of(SequenceBuilder{}.hold(colors::red, limit)) == Error::None);
    CHECK(error_of(SequenceBuilder{}.hold(colors::red, limit + 1)) == Error::TooLong);
    const auto half = [] { return SequenceBuilder{}.hold(colors::red, limit / 2); };
    CHECK(error_of(half().hold(colors::red, limit / 2)) == Error::None);
    CHECK(error_of(half().hold(colors::red, limit / 2 + 1)) == Error::TooLong);
}

TEST_CASE("TooLong: a finite total, through loop and repeat count") {
    constexpr uint32_t limit = 1u << 29;

    /* 2^20 x 2^9 = 2^29 exactly. */
    CHECK(error_of(SequenceBuilder{}.hold(colors::red, 1u << 20).repeat(512)) == Error::None);
    CHECK(error_of(SequenceBuilder{}.hold(colors::red, 1u << 20).repeat(513)) == Error::TooLong);
    CHECK(error_of(SequenceBuilder{}.hold(colors::red, limit).repeat(1)) == Error::None);
    CHECK(error_of(SequenceBuilder{}.hold(colors::red, limit).repeat(2)) == Error::TooLong);
}

TEST_CASE("TooLong: totals that overflow uint32_t are still too long") {
    /* Each of these wraps to a small number in uint32_t arithmetic, which is
     * what a check done there would wrongly accept. */

    /* 2^17 x 2^15 = 2^32, which wraps to exactly 0. */
    CHECK(error_of(SequenceBuilder{}.hold(colors::red, 1u << 17).repeat(32768)) == Error::TooLong);
    /* 2^17 x (2^15 + 1) wraps to 2^17. */
    CHECK(error_of(SequenceBuilder{}.hold(colors::red, 1u << 17).repeat(32769)) == Error::TooLong);
    /* Two steps of 2^31 sum to 2^32, which wraps to 0. */
    CHECK(error_of(SequenceBuilder{}.hold(colors::red, 1u << 31).hold(colors::red, 1u << 31)) ==
          Error::TooLong);
    /* The largest durations there are, repeated as often as possible. */
    CHECK(error_of(SequenceBuilder{}.hold(colors::red, UINT32_MAX).repeat(UINT16_MAX)) ==
          Error::TooLong);
}

TEST_CASE("a sequence at the limit answers correctly at its far end") {
    constexpr uint32_t limit = 1u << 29;
    const Sequence s = built(SequenceBuilder{}
                                 .hold(colors::red, limit / 2)
                                 .hold(colors::blue, limit / 2)
                                 .repeat(1));

    CHECK(s.total_duration_ms() == limit);
    CHECK(s.at(static_cast<int32_t>(limit) - 1).color == colors::blue);
    CHECK(s.at(static_cast<int32_t>(limit) - 1).hold_ms == 1);
    CHECK(s.at(static_cast<int32_t>(limit)).color == colors::blue);   /* landing: the last step */
}

/* ------------------------------------------------------------------------ */
/* Compile time                                                              */
/* ------------------------------------------------------------------------ */

/* A sequence built with build_const() is a constant: constant-initialised into
 * flash like the colors:: palette, with nothing run before main(). constexpr
 * rather than constinit, because constinit promises only the initialisation --
 * the static_asserts below could not read the object back. The failure half --
 * an invalid sequence refusing to compile -- cannot be a test case in a file
 * that has to compile; it is the try_compile check in
 * cmake/stripseq_compile_fail.cmake. */
constexpr Sequence blink =
    SequenceBuilder{}.hold(colors::red, 500).hold(colors::black, 500).build_const();

static_assert(blink.loop_duration_ms() == 1000);
static_assert(blink.total_duration_ms() == hold_forever);

/* build() is constexpr as well, so its verdict can be read at compile time. */
static_assert([] {
    Sequence s;
    return SequenceBuilder{}.hold(colors::red, 0).fade_to(colors::red, 0).build(s);
}() == Error::ZeroDurationFade);

TEST_CASE("a build_const() sequence answers the same as a built one") {
    const Sequence runtime =
        built(SequenceBuilder{}.hold(colors::red, 500).hold(colors::black, 500));
    for (int32_t p : {-1501, -1, 0, 499, 500, 999, 1000, 123'456}) {
        CAPTURE(p);
        CHECK(blink.at(p).color == runtime.at(p).color);
        CHECK(blink.at(p).hold_ms == runtime.at(p).hold_ms);
    }
}
