/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Color: sixteen bits per channel, with white present only when the build asks
 * for it.
 *
 * Sixteen bits are for *blending*, not for output. The wire format is eight
 * bits per channel, so the extra precision exists to keep long fades and
 * multi-step lookups accurate; it is spent at the conversion in color.cpp.
 *
 * Everything here is constexpr and header-only, so a palette costs flash and no
 * startup code.
 */

#ifndef STRIPSEQ_COLOR_H_
#define STRIPSEQ_COLOR_H_

#include <cstdint>

#include "stripseq/config.h"

namespace stripseq {

/* Not part of the API: helpers a public constexpr function needs, and so a
 * header must show. Nothing outside the library should name them. */
namespace detail {

/* x / 65535, floored, without a division: on a Cortex-M0 every `/` is a
 * library call, and the compiler does not turn this one into a multiply at
 * -Os. 65535 is 2^16 - 1, so x / 65535 is x / 2^16 times 1 + 2^-16 + 2^-32 +
 * ..., and the first correction term plus a carry of one is enough: the result
 * is exact for every x up to 65535^2, the largest a product of two 16-bit
 * values can be, and the sum stays under 2^32 there. It is non-decreasing in
 * x, so being exact on both sides of every multiple of 65535 is being exact
 * everywhere -- 131,070 cases rather than four billion, which is how
 * tests/color.cpp checks it. */
constexpr uint32_t div65535(uint32_t x) {
    return (x + 1 + (x >> 16)) >> 16;
}

}  // namespace detail

/* One channel of Color::lerp, for a ColorFn blending something that is not a
 * color: a level, a heat, a height. t: 0 = a, 65535 = b. Exact at both ends
 * and monotonic in t, for the reasons Color::lerp gives. */
constexpr uint16_t lerp(uint16_t a, uint16_t b, uint16_t t) {
    return static_cast<uint16_t>(
        detail::div65535(uint32_t{a} * (65535u - t) + uint32_t{b} * t));
}

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

    /* hue 0..65535; leaves w at 0. Integer sextant math.
     *
     * hue * 6 splits into a sextant index (the top bits) and a 16-bit position
     * within it (the low sixteen). In every sextant one channel sits at the
     * value, one at the floor saturation leaves, and the third rises or falls
     * between them with the position. sat and val are scaled by x257 like
     * rgb8, so 255 is full scale.
     *
     * All of it fits uint32_t: the largest product, span * pos, is under
     * 65535^2. hue * 6 is always even, so pos stops at 65534 and a channel
     * ends each sextant at most a step short of where the next begins. */
    static constexpr Color hsv(uint16_t hue, uint8_t sat, uint8_t val) {
        const uint32_t scaled  = uint32_t{hue} * 6u;
        const uint32_t sextant = scaled >> 16;
        const uint32_t pos     = scaled & 0xFFFFu;

        const uint32_t top  = uint32_t{val} * 257u;
        const uint32_t base = top * (255u - sat) / 255u;
        const uint32_t step = (top - base) * pos / 65535u;

        const auto v    = static_cast<uint16_t>(top);
        const auto m    = static_cast<uint16_t>(base);
        const auto rise = static_cast<uint16_t>(base + step);
        const auto fall = static_cast<uint16_t>(top - step);

        switch (sextant) {
            case 0:  return rgb(v, rise, m);   /* red     -> yellow  */
            case 1:  return rgb(fall, v, m);   /* yellow  -> green   */
            case 2:  return rgb(m, v, rise);   /* green   -> cyan    */
            case 3:  return rgb(m, fall, v);   /* cyan    -> blue    */
            case 4:  return rgb(rise, m, v);   /* blue    -> magenta */
            default: return rgb(v, m, fall);   /* magenta -> red     */
        }
    }

#if STRIPSEQ_WHITE_CHANNEL
    static constexpr Color rgbw(uint16_t r, uint16_t g, uint16_t b, uint16_t w) {
        return Color{r, g, b, w};
    }
#endif

    /* The blend every transition already performs, exposed because a custom
     * ColorFn is the one place outside the library that needs it. Covers all
     * channels, w included. t: 0 = a, 65535 = b. Works in the same 16-bit
     * space the sequence does, the one colors are written in; gamma maps it to
     * emitted light later, at the 8-bit conversion. So with gamma on, a blend's
     * midpoint is halfway to the eye rather than halfway in light.
     *
     * Each channel is (x * (65535 - t) + y * t) / 65535, in uint32_t -- the
     * largest the numerator can be is 65535^2, which fits, and is exactly the
     * range detail::div65535 is exact over. Exact at both ends and monotonic in
     * t. Never x + (y - x) * t / 65535: it needs a signed intermediate, and is
     * easy to get wrong in the direction where y < x.
     *
     * The channel blend is the free stripseq::lerp above, qualified because
     * this member hides it. */
    static constexpr Color lerp(Color a, Color b, uint16_t t) {
#if STRIPSEQ_WHITE_CHANNEL
        return Color{stripseq::lerp(a.r, b.r, t), stripseq::lerp(a.g, b.g, t),
                     stripseq::lerp(a.b, b.b, t), stripseq::lerp(a.w, b.w, t)};
#else
        return Color{stripseq::lerp(a.r, b.r, t), stripseq::lerp(a.g, b.g, t),
                     stripseq::lerp(a.b, b.b, t)};
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
 * of use, where the channels are visible.
 *
 * Nothing is named after it, neither a constant nor a Color::white() factory.
 * "White" is unambiguous in a channel context -- Zephyr's LED_COLOR_ID_WHITE
 * sits beside LED_COLOR_ID_RED -- but beside red and blue in a namespace of
 * colors it reads as "the strip looks white", and the phosphor emitter is
 * usually warmer and dimmer than the RGB mix by an amount the library cannot
 * know. rgbwhite keeps its awkward name because it is the portable one, and
 * the awkwardness stops it being read as authoritative. Adafruit NeoPixel and
 * NeoPixelBus likewise expose the channel and name no constant. */

}  // namespace colors

}  // namespace stripseq

#endif  /* STRIPSEQ_COLOR_H_ */
