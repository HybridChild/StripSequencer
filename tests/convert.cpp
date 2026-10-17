/* to_bytes, the conversion from a 16-bit Color to the 8-bit buffer.
 *
 * The expectation is computed here in double, independently of the library's
 * integer generator, so a slip in the table's rounding shows up as a mismatch
 * rather than being checked against itself. Floating point is fine in a test:
 * the rule against it is about the library.
 */

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <initializer_list>   /* range-for over a braced list; gcc insists */

#include <stripseq/color.h>

#include "color_internal.h"

using namespace stripseq;

namespace {

constexpr int bytes = STRIPSEQ_WHITE_CHANNEL ? 4 : 3;

/* What one channel's top byte should leave the library as. */
uint8_t expected(uint32_t top) {
#if STRIPSEQ_GAMMA
    return static_cast<uint8_t>(std::lround(255.0 * std::pow(top / 255.0, 2.5)));
#else
    return static_cast<uint8_t>(top);
#endif
}

Color gray(uint16_t v) {
#if STRIPSEQ_WHITE_CHANNEL
    return Color::rgbw(v, v, v, v);
#else
    return Color::rgb(v, v, v);
#endif
}

}  // namespace

TEST_CASE("to_bytes at full brightness encodes every top byte") {
    for (uint32_t top = 0; top <= 255; ++top) {
        CAPTURE(top);
        /* Both ends of the 256 channel values that share this top byte. */
        for (uint32_t v : {top << 8, (top << 8) | 0xFF}) {
            uint8_t out[bytes];
            to_bytes(gray(static_cast<uint16_t>(v)), 255, out);
            for (uint8_t byte : out) {
                CHECK(byte == expected(top));
            }
        }
    }
}

TEST_CASE("to_bytes keeps the ends fixed and never runs backwards") {
    uint8_t out[bytes];
    to_bytes(gray(0), 255, out);
    CHECK(out[0] == 0);
    to_bytes(gray(65535), 255, out);
    CHECK(out[0] == 255);

    uint8_t prev = 0;
    bool monotonic = true;
    for (uint32_t v = 0; v <= 65535; ++v) {
        to_bytes(gray(static_cast<uint16_t>(v)), 255, out);
        monotonic = monotonic && out[0] >= prev;
        prev = out[0];
    }
    CHECK(monotonic);
}

TEST_CASE("to_bytes at brightness 0 is black whatever the color") {
    for (uint32_t v : {0u, 1u, 255u, 256u, 32768u, 65535u}) {
        CAPTURE(v);
        uint8_t out[bytes];
        to_bytes(gray(static_cast<uint16_t>(v)), 0, out);
        for (uint8_t byte : out) {
            CHECK(byte == 0);
        }
    }
}

TEST_CASE("to_bytes applies brightness before gamma") {
    /* Full scale at brightness 128 is 65535 * 129 >> 16, top byte 128, and
     * then gamma. The shortcut that scales the encoded byte would give
     * 255 * 128 / 255 = 128 instead, which differs whenever gamma is on. */
    uint8_t out[bytes];
    to_bytes(gray(65535), 128, out);
    for (uint8_t byte : out) {
        CHECK(byte == expected(128));
    }
#if STRIPSEQ_GAMMA
    CHECK(expected(128) != 128);
#endif

    /* Every brightness scales a full-scale channel to its own level before it
     * is encoded. */
    for (uint32_t level = 0; level <= 255; ++level) {
        CAPTURE(level);
        to_bytes(gray(65535), static_cast<uint8_t>(level), out);
        CHECK(out[0] == expected(level));
    }
}

TEST_CASE("brightness is the division by 255 or one level above it, and never runs backwards") {
    /* Every channel at every brightness. The level is the top byte of
     * channel * brightness / 255, or one more: the shift divides by 65536
     * rather than 65280, a little too much, but scales by brightness + 1,
     * which more than makes up for it. Monotonic in the channel and in the brightness, so
     * neither a fade nor turning the knob can step backwards. */
    bool near = true;
    bool rising_with_channel = true;
    bool rising_with_brightness = true;
    uint8_t previous_level[65536] = {};
    for (uint32_t level = 0; level <= 255; ++level) {
        uint8_t prev = 0;
        for (uint32_t v = 0; v <= 65535; ++v) {
            uint8_t out[bytes];
            to_bytes(gray(static_cast<uint16_t>(v)), static_cast<uint8_t>(level), out);
            const uint32_t divided = (v * level / 255u) >> 8;
            near = near && (out[0] == expected(divided) ||
                            (divided < 255 && out[0] == expected(divided + 1)));
            rising_with_channel = rising_with_channel && out[0] >= prev;
            rising_with_brightness = rising_with_brightness && out[0] >= previous_level[v];
            prev = out[0];
            previous_level[v] = out[0];
        }
    }
    CHECK(near);
    CHECK(rising_with_channel);
    CHECK(rising_with_brightness);
}

TEST_CASE("to_bytes converts each channel on its own, in r, g, b, w order") {
    uint8_t out[bytes + 1];
    out[bytes] = 0xA5;   /* sentinel: nothing past the pixel is written */
#if STRIPSEQ_WHITE_CHANNEL
    to_bytes(Color::rgbw(65535, 0, 32768, 16384), 255, out);
    CHECK(out[3] == expected(64));
#else
    to_bytes(Color::rgb(65535, 0, 32768), 255, out);
#endif
    CHECK(out[0] == 255);
    CHECK(out[1] == 0);
    CHECK(out[2] == expected(128));
    CHECK(out[bytes] == 0xA5);
}
