/* What the test files share: comparing and printing a Color, and reading one
 * back out of a rendered buffer.
 *
 * The library gives Color no operator==, since nothing in it compares colors.
 * The tests compare them constantly, and a failed CHECK should print the
 * channels rather than `{?}`, so both live here instead.
 */

#ifndef STRIPSEQ_TESTS_SUPPORT_H_
#define STRIPSEQ_TESTS_SUPPORT_H_

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>

#include <stripseq/color.h>
#include <stripseq/strip.h>

#include "color_internal.h"

namespace stripseq {

/* In the library's namespace, so that argument-dependent lookup finds it from
 * inside doctest's comparison templates. */
constexpr bool operator==(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b
#if STRIPSEQ_WHITE_CHANNEL
        && a.w == b.w
#endif
        ;
}

/* Whether pixel i of a rendered buffer holds c, as the strip writes it at this
 * brightness. Through to_bytes, so the check holds in every variant and leaves
 * the conversion's own exact values to tests/convert.cpp. */
inline bool shows(const uint8_t* buffer, uint16_t i, Color c, uint8_t brightness = 255) {
    uint8_t want[Strip::bytes_per_pixel];
    to_bytes(c, brightness, want);
    for (size_t k = 0; k < Strip::bytes_per_pixel; ++k)
        if (buffer[size_t{i} * Strip::bytes_per_pixel + k] != want[k])
            return false;
    return true;
}

/* Whether two buffers of n bytes are identical. */
inline bool same_bytes(const uint8_t* a, const uint8_t* b, size_t n) {
    for (size_t k = 0; k < n; ++k)
        if (a[k] != b[k])
            return false;
    return true;
}

}  // namespace stripseq

template <>
struct doctest::StringMaker<stripseq::Color> {
    static doctest::String convert(stripseq::Color c) {
        return doctest::String("rgb") +
#if STRIPSEQ_WHITE_CHANNEL
               "w" +
#endif
               "(" + doctest::toString(c.r) + ", " + doctest::toString(c.g) + ", " +
               doctest::toString(c.b) +
#if STRIPSEQ_WHITE_CHANNEL
               ", " + doctest::toString(c.w) +
#endif
               ")";
    }
};

#endif  /* STRIPSEQ_TESTS_SUPPORT_H_ */
