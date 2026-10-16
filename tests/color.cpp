/* Color::lerp, the blend every fade performs.
 *
 * The two invariants are the ones a naive version fails: exact endpoints --
 * t = 0 gives a, t = 65535 gives b, with no LSB short -- and monotonicity in t,
 * in both directions. Both are checked over every t, against a grid of channel
 * values chosen to sit on the edges: zero, full scale, and either side of each
 * byte boundary.
 */

#include <doctest/doctest.h>

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
