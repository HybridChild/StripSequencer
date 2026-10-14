/* What the build variants are for: the sizes and capacities that change with
 * the configuration, checked against what the build asked for.
 *
 * The static_asserts at the top are the load-bearing part, and they exist for
 * one specific failure. If `target_compile_definitions` in
 * cmake/stripseq_variant.cmake were PRIVATE rather than PUBLIC, the library
 * would compile against an eight-byte Color while the tests compiled against a
 * six-byte one -- and the two would link cleanly, because
 * Color::rgb(uint16_t, uint16_t, uint16_t) mangles identically either way.
 * Neither the compiler nor the linker can see that; the result is a binary that
 * writes past its own pixels.
 *
 * So the variant helper hands the *test* target a second, independent copy of
 * each definition, named STRIPSEQ_EXPECT_*. The library gets its copy through
 * the PUBLIC one. If the PUBLIC propagation ever breaks, the two disagree here
 * and the build stops with the message below.
 */

#include <doctest/doctest.h>

#include <cstdint>

#include <stripseq/stripseq.h>

using namespace stripseq;

#define STRIPSEQ_PROPAGATION_MSG \
    "The variant's definitions did not reach the tests. Check that " \
    "target_compile_definitions in cmake/stripseq_variant.cmake is PUBLIC."

#ifdef STRIPSEQ_EXPECT_WHITE_CHANNEL
static_assert(STRIPSEQ_WHITE_CHANNEL == STRIPSEQ_EXPECT_WHITE_CHANNEL, STRIPSEQ_PROPAGATION_MSG);
#endif
#ifdef STRIPSEQ_EXPECT_GAMMA
static_assert(STRIPSEQ_GAMMA == STRIPSEQ_EXPECT_GAMMA, STRIPSEQ_PROPAGATION_MSG);
#endif
#ifdef STRIPSEQ_EXPECT_MAX_STEPS
static_assert(STRIPSEQ_MAX_STEPS == STRIPSEQ_EXPECT_MAX_STEPS, STRIPSEQ_PROPAGATION_MSG);
#endif
#ifdef STRIPSEQ_EXPECT_MAX_SEGMENTS
static_assert(STRIPSEQ_MAX_SEGMENTS == STRIPSEQ_EXPECT_MAX_SEGMENTS, STRIPSEQ_PROPAGATION_MSG);
#endif

/* File scope, constinit: the ground rule is that nothing in this library runs
 * before main(), and a Strip declared the way the cookbook declares one is the
 * case that would break it first. If Strip's constructor ever stops being a
 * constant expression, this line stops compiling. */
static uint8_t buffer[11 * Strip::bytes_per_pixel];
constinit static Strip strip(buffer);

TEST_CASE("color and step layout follow the white-channel setting") {
    CHECK(sizeof(Color) == (STRIPSEQ_WHITE_CHANNEL ? 8u : 6u));
    CHECK(sizeof(Step) == (STRIPSEQ_WHITE_CHANNEL ? 16u : 12u));
    CHECK(Strip::bytes_per_pixel == (STRIPSEQ_WHITE_CHANNEL ? 4u : 3u));
}

TEST_CASE("capacities are the ones this variant was built with") {
    CHECK(STRIPSEQ_MAX_STEPS >= 1);
    CHECK(STRIPSEQ_MAX_SEGMENTS >= 1);
    CHECK(sizeof(Sequence) >= sizeof(Step) * STRIPSEQ_MAX_STEPS);
}

TEST_CASE("a constinit strip is usable and links against the library") {
    CHECK(strip.segment_count() == 0);
    CHECK(strip.brightness() == 255);
}

TEST_CASE("the constant palette is constant-initialised") {
    /* Reading these in a constant expression is what proves it: a dynamic
     * initialiser would not survive here. */
    static_assert(colors::black.r == 0 && colors::black.g == 0 && colors::black.b == 0);
    static_assert(colors::red.r == 65535 && colors::red.g == 0);
    CHECK(colors::rgbwhite.b == 65535);   /* x257, not <<8: full scale is reachable */
}
