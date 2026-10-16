/* Probe for cmake/stripseq_compile_fail.cmake: one translation unit, compiled
 * once per case with STRIPSEQ_PROBE set to that case's number. Every case but
 * the first is a sequence build_const() must refuse to compile.
 *
 * Not part of any target. It is compiled only by try_compile, which reports
 * whether it compiled and what the compiler said.
 */

#include <stripseq/sequence.h>

using namespace stripseq;

#if STRIPSEQ_PROBE == 0   /* the control: the probe itself compiles */
constexpr Sequence probe = SequenceBuilder{}.hold(colors::red, 500).build_const();
#elif STRIPSEQ_PROBE == 1
constexpr Sequence probe = SequenceBuilder{}.build_const();
#elif STRIPSEQ_PROBE == 2
constexpr Sequence probe = [] {
    SequenceBuilder b;
    for (int i = 0; i <= STRIPSEQ_MAX_STEPS; ++i)
        b.hold(colors::red, 10);
    return b;
}().build_const();
#elif STRIPSEQ_PROBE == 3
constexpr Sequence probe = SequenceBuilder{}.fade_to(colors::red, 0).build_const();
#elif STRIPSEQ_PROBE == 4
constexpr Sequence probe = SequenceBuilder{}.hold(colors::red, 10).repeat(0).build_const();
#elif STRIPSEQ_PROBE == 5
constexpr Sequence probe =
    SequenceBuilder{}.hold(colors::red, 10).landing_color(colors::black).build_const();
#elif STRIPSEQ_PROBE == 6
constexpr Sequence probe =
    SequenceBuilder{}.hold(colors::red, 10).start_color(colors::black).build_const();
#elif STRIPSEQ_PROBE == 7
constexpr Sequence probe = SequenceBuilder{}.hold(colors::red, (1u << 29) + 1).build_const();
#elif STRIPSEQ_PROBE == 8
constexpr Sequence probe = SequenceBuilder{}.hold(colors::red, 0).build_const();
#endif
