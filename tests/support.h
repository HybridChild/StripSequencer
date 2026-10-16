/* What the test files share: comparing and printing a Color.
 *
 * The library gives Color no operator==, since nothing in it compares colors.
 * The tests compare them constantly, and a failed CHECK should print the
 * channels rather than `{?}`, so both live here instead.
 */

#ifndef STRIPSEQ_TESTS_SUPPORT_H_
#define STRIPSEQ_TESTS_SUPPORT_H_

#include <doctest/doctest.h>

#include <stripseq/color.h>

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
