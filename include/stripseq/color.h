/* Color: sixteen bits per channel, with white present only when the build asks
 * for it.
 *
 * Sixteen bits are for *blending*, not for output. The wire format is eight
 * bits per channel, so the extra precision exists to keep long fades and
 * multi-step lookups accurate; it is spent at the conversion in color.cpp.
 *
 * Everything here is constexpr and header-only, so a palette costs flash and no
 * startup code. The naming decision -- why nothing is called `white` -- is
 * recorded in docs/API-SKETCH.md, "Color".
 */

#ifndef STRIPSEQ_COLOR_H_
#define STRIPSEQ_COLOR_H_

#include <cstdint>

#include "stripseq/config.h"

namespace stripseq {

struct Color {
    uint16_t r, g, b;
#if STRIPSEQ_WHITE_CHANNEL
    uint16_t w;
#endif

    /* Named constructors only, never aggregate initialisation: `Color{255,0,0}`
     * compiles in an RGB build and is wrong-but-legal in an RGBW one. Inside
     * these factories the aggregate form is the implementation, and the fourth
     * channel is explicit in the one factory that carries it. */
    static constexpr Color rgb(uint16_t r, uint16_t g, uint16_t b) {
#if STRIPSEQ_WHITE_CHANNEL
        /* The fourth channel is written rather than left to the aggregate's
         * value-initialisation, so that "rgb() does not light the white die"
         * is visible here and not inferred. */
        return Color{r, g, b, 0};
#else
        return Color{r, g, b};
#endif
    }

    /* 8-bit convenience, scaled up by x257 rather than <<8: 257 maps 255 to
     * exactly 65535 and 0 to 0, where a shift tops out at 65280 and can never
     * reach full scale. */
    static constexpr Color rgb8(uint8_t r, uint8_t g, uint8_t b) {
        return rgb(static_cast<uint16_t>(r * 257),
                   static_cast<uint16_t>(g * 257),
                   static_cast<uint16_t>(b * 257));
    }

    /* hue 0..65535; leaves w at 0. Integer sextant math. */
    static constexpr Color hsv(uint16_t /*hue*/, uint8_t /*sat*/, uint8_t /*val*/) {
        return Color{};   /* stub */
    }

#if STRIPSEQ_WHITE_CHANNEL
    static constexpr Color rgbw(uint16_t r, uint16_t g, uint16_t b, uint16_t w) {
        return Color{r, g, b, w};
    }
#endif

    /* The blend every transition already performs, exposed because a custom
     * ColorFn is the one place outside the library that needs it. Covers all
     * channels, w included. t: 0 = a, 65535 = b. Works in the same linear
     * 16-bit space the sequence does -- gamma is applied later, at the 8-bit
     * conversion, so blending here is unaffected by it.
     *
     * Each channel is (x * (65535 - t) + y * t) / 65535, in uint32_t -- the
     * largest the numerator can be is 65535^2, which fits. Exact at both ends
     * and monotonic in t. Never x + (y - x) * t / 65535: it needs a signed
     * intermediate, and is easy to get wrong in the direction where y < x. */
    static constexpr Color lerp(Color a, Color b, uint16_t t) {
        const auto mix = [t](uint16_t x, uint16_t y) {
            return static_cast<uint16_t>(
                (uint32_t{x} * (65535u - t) + uint32_t{y} * t) / 65535u);
        };
#if STRIPSEQ_WHITE_CHANNEL
        return Color{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.w, b.w)};
#else
        return Color{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b)};
#endif
    }
};

/* Asserted here rather than in a test, because every size argument downstream
 * -- Step's field order, Strip::bytes_per_pixel -- is derived from it. */
static_assert(sizeof(Color) == (STRIPSEQ_WHITE_CHANNEL ? 8 : 6),
              "Color has picked up padding -- check the channel members.");

namespace colors {

/* `inline constexpr` keeps these constant-initialised, so they live in flash
 * and run no code before main(). */
inline constexpr Color black    = Color::rgb(0, 0, 0);
inline constexpr Color red      = Color::rgb8(255, 0, 0);
inline constexpr Color green    = Color::rgb8(0, 255, 0);
inline constexpr Color blue     = Color::rgb8(0, 0, 255);
inline constexpr Color rgbwhite = Color::rgb8(255, 255, 255);  /* all three channels; any build */

/* No constant for the W emitter: write Color::rgbw(0, 0, 0, 65535) at the point
 * of use, where the channels are visible. */

}  // namespace colors

}  // namespace stripseq

#endif  /* STRIPSEQ_COLOR_H_ */
