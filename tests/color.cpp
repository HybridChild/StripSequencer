/* Color's arithmetic: lerp, rgb8 and hsv, and the division lerp is built on.
 *
 * lerp is the blend every fade performs. Its two invariants are the ones a
 * naive version fails: exact endpoints -- t = 0 gives a, t = 65535 gives b,
 * with no LSB short -- and monotonicity in t, in both directions. Both are
 * checked over every t, against a grid of channel values chosen to sit on the
 * edges: zero, full scale, and either side of each byte boundary.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>

#include <stripseq/color.h>

#include "support.h"

using namespace stripseq;

namespace {

constexpr uint16_t grid[] = {0, 1, 254, 255, 256, 257, 12345, 32767, 32768, 65280, 65534, 65535};

/* One channel's lerp, read through red: the other channels are identical. */
uint16_t mix(uint16_t x, uint16_t y, uint16_t t) {
    return Color::lerp(Color::rgb(x, 0, 0), Color::rgb(y, 0, 0), t).r;
}

}  // namespace

/* Every multiple of 65535 up to 65535^2 is a point where the quotient steps,
 * and the helper is non-decreasing, so landing on each step exactly -- k at
 * k * 65535, and still k - 1 one below it -- leaves nowhere in between for it
 * to be wrong. */
TEST_CASE("div65535 is exact on both sides of every multiple of 65535") {
    CHECK(detail::div65535(0) == 0);
    for (uint32_t k = 1; k <= 65535; ++k) {
        CAPTURE(k);
        CHECK(detail::div65535(k * 65535 - 1) == k - 1);
        CHECK(detail::div65535(k * 65535) == k);
    }
}

TEST_CASE("lerp is exact at both ends") {
    for (uint16_t x : grid) {
        for (uint16_t y : grid) {
            CAPTURE(x);
            CAPTURE(y);
            CHECK(mix(x, y, 0) == x);
            CHECK(mix(x, y, 65535) == y);
        }
    }
}

TEST_CASE("lerp is monotonic in t, rising and falling") {
    for (uint16_t x : grid) {
        for (uint16_t y : grid) {
            CAPTURE(x);
            CAPTURE(y);
            bool monotonic = true;
            uint16_t prev = x;
            for (uint32_t t = 0; t <= 65535; ++t) {
                const uint16_t v = mix(x, y, static_cast<uint16_t>(t));
                monotonic = monotonic && (y >= x ? v >= prev : v <= prev);
                prev = v;
            }
            CHECK(monotonic);
        }
    }
}

TEST_CASE("lerp blends every channel independently, w included") {
#if STRIPSEQ_WHITE_CHANNEL
    const Color a = Color::rgbw(0, 65535, 1000, 65535);
    const Color b = Color::rgbw(65535, 0, 1000, 0);
    CHECK(Color::lerp(a, b, 32768) == Color::rgbw(32768, 32767, 1000, 32767));
#else
    const Color a = Color::rgb(0, 65535, 1000);
    const Color b = Color::rgb(65535, 0, 1000);
    CHECK(Color::lerp(a, b, 32768) == Color::rgb(32768, 32767, 1000));
#endif
}

/* constexpr, so a ColorFn's palette can be blended at compile time too. */
static_assert(Color::lerp(colors::black, colors::red, 65535).r == 65535);
static_assert(Color::lerp(colors::black, colors::red, 0).r == 0);

/* Color::rgb8, the 8-bit convenience. */

TEST_CASE("rgb8 reaches full scale and round-trips every byte") {
    for (uint32_t v = 0; v <= 255; ++v) {
        CAPTURE(v);
        const auto byte = static_cast<uint8_t>(v);
        const Color c = Color::rgb8(byte, byte, byte);
        CHECK(c.r == v * 257);
        CHECK((c.r >> 8) == v);
    }
    CHECK(Color::rgb8(255, 255, 255) == Color::rgb(65535, 65535, 65535));
}

/* Color::hsv. The failure worth guarding against is a one-off at a sextant
 * boundary: invisible in a static gradient, a flicker in a rotating rainbow. So
 * the sweep walks every hue, across all six boundaries and the wrap from 65535
 * back to 0, and bounds how far any channel moves in one step. */

namespace {

uint32_t distance(uint16_t a, uint16_t b) {
    return a > b ? a - b : b - a;
}

}  // namespace

TEST_CASE("hsv is continuous at the sextant boundaries") {
    /* One hue step moves the position by 6 of 65535, so a channel moves by at
     * most 6, plus one for the truncation on either side. */
    constexpr uint32_t bound = 7;

    for (uint8_t sat : {uint8_t{255}, uint8_t{128}, uint8_t{1}}) {
        for (uint8_t val : {uint8_t{255}, uint8_t{128}, uint8_t{1}}) {
            CAPTURE(sat);
            CAPTURE(val);
            uint32_t worst = 0;
            Color prev = Color::hsv(65535, sat, val);
            for (uint32_t h = 0; h <= 65535; ++h) {
                const Color c = Color::hsv(static_cast<uint16_t>(h), sat, val);
                worst = std::max({worst, distance(c.r, prev.r), distance(c.g, prev.g),
                                  distance(c.b, prev.b)});
                prev = c;
            }
            CHECK(worst <= bound);
        }
    }
}

TEST_CASE("hsv at saturation 0 is a gray at every hue") {
    for (uint32_t h = 0; h <= 65535; h += 7) {
        CAPTURE(h);
        const auto hue = static_cast<uint16_t>(h);
        CHECK(Color::hsv(hue, 0, 255) == Color::rgb(65535, 65535, 65535));
        CHECK(Color::hsv(hue, 0, 100) == Color::rgb8(100, 100, 100));
    }
}

TEST_CASE("hsv at value 0 is black at every hue") {
    for (uint32_t h = 0; h <= 65535; h += 7) {
        CAPTURE(h);
        CHECK(Color::hsv(static_cast<uint16_t>(h), 255, 0) == colors::black);
    }
}

TEST_CASE("hsv lands on the primaries at the sextant starts it can reach exactly") {
    /* 0 and 0x8000 are the two sextant starts where hue * 6 is a multiple of
     * 65536; the other four fall between two hues. */
    CHECK(Color::hsv(0x0000, 255, 255) == colors::red);
    CHECK(Color::hsv(0x8000, 255, 255) == Color::rgb(0, 65535, 65535));
}

#if STRIPSEQ_WHITE_CHANNEL
TEST_CASE("hsv leaves the white channel dark") {
    for (uint32_t h = 0; h <= 65535; h += 7) {
        CAPTURE(h);
        CHECK(Color::hsv(static_cast<uint16_t>(h), 255, 255).w == 0);
        CHECK(Color::hsv(static_cast<uint16_t>(h), 0, 255).w == 0);
    }
}
#endif

/* constexpr, so a palette of hues costs flash and no startup code. */
static_assert(Color::hsv(0, 255, 255).r == 65535);
static_assert(Color::hsv(0x8000, 255, 255).b == 65535);
