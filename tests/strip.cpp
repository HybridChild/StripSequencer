/* Strip: the segment list, the frame, and the hold folded across both.
 *
 * The cases are grouped by what the strip promises. The list is paint order,
 * and add() refuses a segment that would run past the buffer while a segment
 * assigned in place is clipped to it. Rendering is a pure function of the
 * segments and the time handed in, so the same frame comes out however the
 * strip got there. Later segments win where they overlap. And drop_finished()
 * and the drives() sweep remove exactly what they should, however the list
 * shifts underneath them.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>   /* range-for over a braced list; gcc insists */

#include <stripseq/config.h>
#include <stripseq/segment.h>
#include <stripseq/sequence.h>
#include <stripseq/sequencer.h>
#include <stripseq/strip.h>

#include "support.h"

using namespace stripseq;

namespace {

constexpr uint16_t kPixels = 10;
constexpr size_t kBytes = kPixels * Strip::bytes_per_pixel;

/* Steady red, so a pixel painted by it is red at any time. */
constexpr Sequence red = SequenceBuilder{}.hold(colors::red, 1000).build_const();

/* Red for a second, then blue for a second: a hold that counts down. */
constexpr Sequence red_then_blue =
    SequenceBuilder{}.hold(colors::red, 1000).hold(colors::blue, 1000).build_const();

/* Always mid-fade, so a hold of 0 on a playing sequencer. */
constexpr Sequence breathe = SequenceBuilder{}
                                 .fade_to(colors::rgbwhite, 1000)
                                 .fade_to(colors::black, 1000)
                                 .build_const();

/* Finite overlays of two lengths, landing on green. */
constexpr Sequence short_flash =
    SequenceBuilder{}.hold(colors::green, 100).landing_color(colors::green).repeat(1).build_const();
constexpr Sequence long_flash =
    SequenceBuilder{}.hold(colors::green, 300).landing_color(colors::green).repeat(1).build_const();

}  // namespace

/* ------------------------------------------------------------------------ */
/* The list                                                                  */
/* ------------------------------------------------------------------------ */

TEST_CASE("an empty strip renders black and holds forever") {
    uint8_t buffer[kBytes];
    for (uint8_t& b : buffer)
        b = 0xEE;
    Strip strip(buffer);
    CHECK(strip.segment_count() == 0);
    CHECK(strip.brightness() == 255);
    CHECK(strip.render(0) == hold_forever);
    for (uint16_t i = 0; i < kPixels; ++i)
        CHECK(shows(buffer, i, colors::black));
}

TEST_CASE("add appends up to capacity, and remove shifts the rest down") {
    uint8_t buffer[kBytes];
    Strip strip(buffer);
    Sequencer seqs[STRIPSEQ_MAX_SEGMENTS + 1];

    for (uint8_t i = 0; i < STRIPSEQ_MAX_SEGMENTS; ++i)
        CHECK(strip.add(Segment(0, kPixels, seqs[i])));
    CHECK(strip.segment_count() == STRIPSEQ_MAX_SEGMENTS);
    CHECK_FALSE(strip.add(Segment(0, kPixels, seqs[STRIPSEQ_MAX_SEGMENTS])));
    CHECK(strip.segment_count() == STRIPSEQ_MAX_SEGMENTS);

    CHECK_FALSE(strip.remove(STRIPSEQ_MAX_SEGMENTS));
    CHECK(strip.remove(0));
    CHECK(strip.segment_count() == STRIPSEQ_MAX_SEGMENTS - 1);
    for (uint8_t i = 0; i < strip.segment_count(); ++i)
        CHECK(strip.segment(i).drives(seqs[i + 1]));
}

TEST_CASE("add refuses a segment that runs past the end of the strip") {
    uint8_t buffer[kBytes];
    Strip strip(buffer);
    Sequencer seq;
    CHECK(strip.add(Segment(6, 4, seq)));       /* exactly to the end */
    CHECK_FALSE(strip.add(Segment(6, 5, seq)));
    CHECK_FALSE(strip.add(Segment(10, 1, seq)));
    CHECK_FALSE(strip.add(Segment(65535, 2, seq)));   /* would wrap in uint16_t */
    CHECK(strip.segment_count() == 1);
}

TEST_CASE("a default-constructed segment in the list paints nothing") {
    uint8_t buffer[kBytes];
    Strip strip(buffer);
    REQUIRE(strip.add(Segment()));
    CHECK(strip.render(0) == hold_forever);
    for (uint16_t i = 0; i < kPixels; ++i)
        CHECK(shows(buffer, i, colors::black));
    CHECK(strip.drop_finished(0) == 0);
}

/* ------------------------------------------------------------------------ */
/* Clipping a segment assigned in place                                      */
/* ------------------------------------------------------------------------ */

TEST_CASE("a segment assigned past the end paints its visible part only") {
    /* Bytes after the buffer, to catch a write past it. A struct of byte
     * arrays has no padding, so the guard sits directly behind the pixels. */
    struct {
        uint8_t pixels[kBytes];
        uint8_t guard[16];
    } memory;
    for (uint8_t& b : memory.guard)
        b = 0xA5;

    Strip strip(memory.pixels);
    Sequencer seq;
    seq.load(red_then_blue);
    seq.play(0);
    REQUIRE(strip.add(Segment(0, 1, seq)));

    SUBCASE("overhanging: the visible pixels paint, and only they set the hold") {
        /* Pixel i looks 100 i ms ahead. At 50 ms the four visible pixels, at
         * 50..350, next change in 650 ms; the four past the end would change
         * sooner, the last of them at 750 in 250 ms. */
        Segment overhanging(6, 8, seq);
        REQUIRE(overhanging.set_pixel_delay(-100));
        strip.segment(0) = overhanging;

        CHECK(strip.render(50) == 650);
        for (uint16_t i = 0; i < 6; ++i)
            CHECK(shows(memory.pixels, i, colors::black));
        for (uint16_t i = 6; i < kPixels; ++i)
            CHECK(shows(memory.pixels, i, colors::red));
    }
    SUBCASE("wholly off the strip: nothing painted, and no reason to come back") {
        seq.load(breathe);
        seq.play(0);
        strip.segment(0) = Segment(kPixels, 3, seq);
        CHECK(strip.render(500) == hold_forever);
        for (uint16_t i = 0; i < kPixels; ++i)
            CHECK(shows(memory.pixels, i, colors::black));
    }

    for (uint8_t b : memory.guard)
        CHECK(b == 0xA5);
}

/* ------------------------------------------------------------------------ */
/* The frame                                                                 */
/* ------------------------------------------------------------------------ */

TEST_CASE("later segments overwrite earlier ones where they overlap") {
    Sequencer under;
    Sequencer over;
    under.load(red);
    over.load(SequenceBuilder{}.hold(colors::green, 1000).build_const());
    under.play(0);
    over.play(0);

    uint8_t buffer[kBytes];
    Strip strip(buffer);

    SUBCASE("background first, overlay on top") {
        REQUIRE(strip.add(Segment(0, kPixels, under)));
        REQUIRE(strip.add(Segment(3, 4, over)));
        strip.render(10);
        for (uint16_t i = 0; i < kPixels; ++i) {
            CAPTURE(i);
            CHECK(shows(buffer, i, i >= 3 && i < 7 ? colors::green : colors::red));
        }
    }
    SUBCASE("the same two the other way round: the background covers it") {
        REQUIRE(strip.add(Segment(3, 4, over)));
        REQUIRE(strip.add(Segment(0, kPixels, under)));
        strip.render(10);
        for (uint16_t i = 0; i < kPixels; ++i)
            CHECK(shows(buffer, i, colors::red));
    }
}

TEST_CASE("the hold is the smallest across every pixel") {
    /* At 50 ms, pixel i looks 100 i ms behind: pixel 0 at 50 has 950 ms of red
     * left, and pixels 1 to 4 have wrapped into the blue at 1950, 1850, 1750
     * and 1650. The smallest is pixel 1's, in the middle of the segment. */
    Sequencer seq;
    seq.load(red_then_blue);
    seq.play(0);
    Segment s(0, 5, seq);
    REQUIRE(s.set_pixel_delay(100));

    uint8_t buffer[kBytes];
    Strip strip(buffer);
    REQUIRE(strip.add(s));
    CHECK(strip.render(50) == 50);
}

TEST_CASE("the hold is the smallest across every segment") {
    Sequencer a;
    Sequencer b;
    a.load(red_then_blue);
    b.load(red_then_blue);
    a.play(0);      /* at 300: 700 ms left of red */
    b.play(-400);   /* at 300: 300 ms left */

    uint8_t buffer[kBytes];
    Strip strip(buffer);
    REQUIRE(strip.add(Segment(0, 5, a)));
    CHECK(strip.render(300) == 700);
    REQUIRE(strip.add(Segment(5, 5, b)));
    CHECK(strip.render(300) == 300);
}

TEST_CASE("brightness is applied as each pixel is converted") {
    Sequencer seq;
    seq.load(red);
    seq.play(0);
    uint8_t buffer[kBytes];
    Strip strip(buffer);
    REQUIRE(strip.add(Segment(0, kPixels, seq)));
    strip.set_brightness(128);
    CHECK(strip.brightness() == 128);
    strip.render(0);
    for (uint16_t i = 0; i < kPixels; ++i)
        CHECK(shows(buffer, i, colors::red, 128));
}

TEST_CASE("rendering is pure: the same time gives the same frame") {
    Sequencer seq;
    seq.load(breathe);
    seq.play(0);

    uint8_t buffer[kBytes];
    Strip strip(buffer);
    Segment comet(0, kPixels, seq);
    REQUIRE(comet.set_pixel_delay(37));
    REQUIRE(strip.add(comet));

    const uint32_t hold = strip.render(1234);
    uint8_t first[kBytes];
    for (size_t k = 0; k < kBytes; ++k)
        first[k] = buffer[k];

    CHECK(strip.render(1234) == hold);
    CHECK(same_bytes(buffer, first, kBytes));

    strip.render(99999);
    CHECK_FALSE(same_bytes(buffer, first, kBytes));
    CHECK(strip.render(1234) == hold);
    CHECK(same_bytes(buffer, first, kBytes));
}

TEST_CASE("rendering is stateless: call order does not reach the frame") {
    Sequencer under;
    Sequencer over;
    Sequencer spare;
    under.load(breathe);
    over.load(red_then_blue);
    spare.load(red);
    under.play(0);
    over.play(0);
    spare.play(0);

    Segment background(0, kPixels, under);
    REQUIRE(background.set_pixel_delay(25));
    Segment overlay(2, 5, over);
    REQUIRE(overlay.set_pixel_delay(-150));

    /* Built directly. */
    uint8_t a[kBytes];
    Strip direct(a);
    REQUIRE(direct.add(background));
    REQUIRE(direct.add(overlay));
    direct.set_brightness(100);

    /* Built the long way round: rendered at other times, a segment added and
     * removed, the brightness changed twice, a segment replaced in place. */
    uint8_t b[kBytes];
    Strip roundabout(b);
    roundabout.set_brightness(7);
    roundabout.render(5000);
    REQUIRE(roundabout.add(background));
    roundabout.render(17);
    REQUIRE(roundabout.add(Segment(0, kPixels, spare)));
    roundabout.render(800);
    REQUIRE(roundabout.remove(1));
    REQUIRE(roundabout.add(Segment(0, 1, spare)));
    roundabout.segment(1) = overlay;
    roundabout.set_brightness(100);
    roundabout.render(123456);

    for (int64_t now : {0, 640, 1999, 70000}) {
        CAPTURE(now);
        CHECK(direct.render(now) == roundabout.render(now));
        CHECK(same_bytes(a, b, kBytes));
    }
}

/* ------------------------------------------------------------------------ */
/* Removing segments                                                         */
/* ------------------------------------------------------------------------ */

TEST_CASE("drop_finished removes overlays that finish out of order") {
    /* The one added first finishes last, so the earlier index goes second. */
    Sequencer first;
    Sequencer second;
    first.load(long_flash);
    second.load(short_flash);
    first.play(0);
    second.play(0);

    uint8_t buffer[kBytes];
    Strip strip(buffer);
    REQUIRE(strip.add(Segment(0, kPixels, first)));
    REQUIRE(strip.add(Segment(0, kPixels, second)));

    SUBCASE("one at a time") {
        CHECK(strip.drop_finished(50) == 0);
        CHECK(strip.drop_finished(150) == 1);
        REQUIRE(strip.segment_count() == 1);
        CHECK(strip.segment(0).drives(first));
        CHECK(strip.drop_finished(350) == 1);
        CHECK(strip.segment_count() == 0);
    }
    SUBCASE("both in one call: the case a forward loop gets wrong") {
        CHECK(strip.drop_finished(350) == 2);
        CHECK(strip.segment_count() == 0);
    }
}

/* A background and two overlays is three segments, one more than tight holds. */
#if STRIPSEQ_MAX_SEGMENTS >= 3

TEST_CASE("drop_finished leaves an endless background in place") {
    Sequencer background;
    Sequencer first;
    Sequencer second;
    background.load(red);
    first.load(long_flash);
    second.load(short_flash);
    background.play(0);
    first.play(0);
    second.play(0);

    uint8_t buffer[kBytes];
    Strip strip(buffer);
    REQUIRE(strip.add(Segment(0, kPixels, background)));
    REQUIRE(strip.add(Segment(0, kPixels, first)));
    REQUIRE(strip.add(Segment(0, kPixels, second)));

    CHECK(strip.drop_finished(150) == 1);
    CHECK(strip.drop_finished(350) == 1);
    REQUIRE(strip.segment_count() == 1);
    CHECK(strip.segment(0).drives(background));
    CHECK(strip.drop_finished(60000) == 0);
}

TEST_CASE("the drives() sweep removes every segment on one sequencer") {
    /* The sketch's cancel_alert(), over two segments sharing the alert. */
    Sequencer breathe_seq;
    Sequencer alert_seq;
    breathe_seq.load(red);
    alert_seq.load(SequenceBuilder{}
                       .hold(colors::green, 120)
                       .hold(colors::black, 120)
                       .repeat(3)
                       .build_const());
    breathe_seq.play(0);
    alert_seq.play(0);

    uint8_t buffer[kBytes];
    Strip strip(buffer);
    REQUIRE(strip.add(Segment(0, kPixels, breathe_seq)));
    REQUIRE(strip.add(Segment(0, 5, alert_seq)));
    REQUIRE(strip.add(Segment(5, 5, alert_seq)));

    for (uint8_t i = strip.segment_count(); i > 0; --i)
        if (strip.segment(i - 1).drives(alert_seq))
            strip.remove(i - 1);

    REQUIRE(strip.segment_count() == 1);
    CHECK(strip.segment(0).drives(breathe_seq));
    strip.render(10);
    for (uint16_t i = 0; i < kPixels; ++i)
        CHECK(shows(buffer, i, colors::red));
}

#endif  /* STRIPSEQ_MAX_SEGMENTS >= 3 */
