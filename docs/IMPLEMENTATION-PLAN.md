# StripSequencer — implementation plan

How the mechanism in [DESIGN.md](DESIGN.md), with the calling surface in [API-SKETCH.md](API-SKETCH.md), gets built. Eight phases, each one buildable and testable on its own, ordered so that every phase can be verified before the next depends on it.

The shape of the project is a **standalone CMake library that a Zephyr module wraps**. The core knows nothing about Zephyr, builds and tests on a host machine with no cross-compiler, and is consumed by Zephyr through a thin module directory that translates Kconfig into the four build-time macros. That ordering is deliberate: the OS-agnostic claim in DESIGN.md is only credible if the library is genuinely built and tested without an OS, and a Zephyr-first repo with host tests bolted on would let platform assumptions leak in unnoticed.

## Contents

- [Ground rules](#ground-rules)
- [Repository layout](#repository-layout)
- [Phase 0 — Skeleton and build](#phase-0--skeleton-and-build)
- [Phase 1 — Configuration, color and output conversion](#phase-1--configuration-color-and-output-conversion)
- [Phase 2 — Sequence](#phase-2--sequence)
- [Phase 3 — Sequencer](#phase-3--sequencer)
- [Phase 4 — Segment and Strip](#phase-4--segment-and-strip)
- [Phase 5 — The cookbook as tests](#phase-5--the-cookbook-as-tests)
- [Phase 6 — Zephyr integration](#phase-6--zephyr-integration)
- [Phase 7 — Hardening and release](#phase-7--hardening-and-release)
- [Decisions deferred to their phase](#decisions-deferred-to-their-phase)
- [Risk register](#risk-register)

## Ground rules

These hold in every phase and are worth stating once rather than repeating.

**The library never sets a toolchain flag.** Zephyr owns the compiler, the standard library, the optimisation level and the warning set for anything built inside it. The core target declares `target_compile_features(stripseq PUBLIC cxx_std_20)` — a requirement, which CMake reconciles with the consumer — and nothing else. No touching `CMAKE_CXX_FLAGS`, no `-fno-exceptions`, no `-Os`. Warnings and sanitizers are applied only when `PROJECT_IS_TOP_LEVEL`, so they shape our own builds and never a consumer's.

**No floating point anywhere in the library.** Not in easing, not in HSV, not in gamma generation at compile time. Many Cortex-M parts have no FPU, soft-float pulls in library code, and integer math is bit-exact across compilers, which is what makes golden-frame tests meaningful. Fixed point where fractions are needed; the `q8` delay in `Segment` is the existing precedent.

**No dynamic initialisation at file scope, and no function-local statics.** A function-local static with a non-constant initialiser emits a guard variable and calls `__cxa_guard_acquire`, which drags in threading support and defeats `constinit`. Every table is `constexpr` or a `const` array in flash. Phase 7 checks this mechanically rather than by inspection.

**Only `<cstdint>`, `<cstddef>` and `<type_traits>`.** No `<algorithm>`, no `<array>`, no `<limits>`. Where a `min` is wanted, write it. This is what makes the freestanding cross-compile check in Phase 0 a real test rather than a formality.

**The bench target is a Nucleo-H753ZI driving 11 RGBW pixels over I2S3.** The one thing that matters before Phase 6 is that the board configuration is `STRIPSEQ_WHITE_CHANNEL=1`, which is *not* the library default — so RGBW must be a first-class entry in the host test matrix rather than an afterthought.

**Every phase ends green.** A phase is not done when the code is written; it is done when its tests pass in every build variant and CI is clean. There are five: the cross product of `STRIPSEQ_WHITE_CHANNEL` and `STRIPSEQ_GAMMA`, both of which change compiled behaviour, plus one built at awkward capacities to catch code that assumes the defaults.

## Repository layout

```text
CMakeLists.txt              # standalone build; library + optional tests
cmake/
  sources.cmake             # STRIPSEQ_SOURCES / STRIPSEQ_INCLUDE_DIR, shared with the Zephyr module
  stripseq_variant.cmake    # helper that builds one (library, test) pair per configuration
  stripseq_compile_fail.cmake  # configure-time check that build_const() rejects invalid sequences
  arm-none-eabi.cmake       # toolchain file for the freestanding cross-compile check
scripts/                    # build.sh, test.sh, cross-check.sh -- what CI runs, runnable by hand; clean.sh deletes the build trees
.github/workflows/ci.yml
include/stripseq/
  config.h                  # the four macros, each #ifndef-guarded
  color.h                   # Color, colors::, lerp, hsv        (all constexpr, header-only)
  sequence.h                # Transition, Step, Sample, Error, Sequence, SequenceBuilder
  sequencer.h
  segment.h                 # PlayheadFn, ColorFn, spread, Segment
  strip.h
  stripseq.h                # umbrella header, includes the rest
src/
  color.cpp                 # the gamma table and the 16->8 conversion
  sequence.cpp              # Sequence::at and the easing curves
  sequencer.cpp
  segment.cpp
  strip.cpp
tests/
  ...                       # one file per header, plus cookbook.cpp
  compile_fail/             # probes that must not compile, driven by stripseq_compile_fail.cmake
zephyr/
  module.yml                # makes the repo a Zephyr module
  CMakeLists.txt            # zephyr_library() over the same source list
  Kconfig                   # CONFIG_STRIPSEQ_* symbols
samples/strip_demo/         # the Nucleo-H753ZI bring-up app, added in Phase 6
  boards/nucleo_h753zi.overlay
docs/
```

Two notes on why it is arranged this way. The source list lives in `cmake/sources.cmake` and is included by both the standalone `CMakeLists.txt` and `zephyr/CMakeLists.txt`, so a new `.cpp` cannot be added to one build and forgotten in the other — a failure mode that surfaces as a link error on hardware long after the host tests went green. And `include/stripseq/` is a prefixed include directory so consumers write `#include <stripseq/strip.h>`, which is what keeps the headers from colliding with anything in a Zephyr tree.

## Phase 0 — Skeleton and build

**Goal:** every build path exists and works before there is any code worth building, so that later phases never debug the build and the feature at the same time.

**Deliverables:**

- The layout above, with headers containing only declarations copied from the sketch and sources containing stub definitions.
- A `CMakeLists.txt` producing a static `stripseq` target plus an alias `StripSequencer::stripseq`.
- The test framework wired up with one trivially passing test.
- The variant helper and its five calls.
- A freestanding cross-compile check.
- CI.

**Implementation notes.**

The variant helper is the load-bearing piece and is worth getting right on day one. `STRIPSEQ_WHITE_CHANNEL` and `STRIPSEQ_GAMMA` change compiled behaviour rather than just a constant — the first changes `sizeof(Color)`, `sizeof(Step)` and `Strip::bytes_per_pixel`, the second changes the output conversion and whether a table is linked — so no single compiled artifact represents both settings and the whole library is built once per combination:

```cmake
# One (library, test binary, CTest entries) triple per build configuration.
#
#   stripseq_add_variant(default)                       # config.h's own defaults
#   stripseq_add_variant(rgbw   STRIPSEQ_WHITE_CHANNEL=1)
#   stripseq_add_variant(tight  STRIPSEQ_MAX_STEPS=3 STRIPSEQ_MAX_SEGMENTS=1)
function(stripseq_add_variant name)
    set(lib stripseq_${name})
    set(exe tests_${name})

    add_library(${lib} STATIC ${STRIPSEQ_SOURCES})
    target_include_directories(${lib} PUBLIC ${STRIPSEQ_INCLUDE_DIR})
    target_compile_features(${lib} PUBLIC cxx_std_20)

    # PUBLIC is load-bearing: it propagates to ${exe}, so the tests cannot
    # disagree with the library about the layout of Color. They would link
    # anyway if they did -- the mangled names do not change -- so nothing
    # downstream would catch it.
    target_compile_definitions(${lib} PUBLIC ${ARGN})

    # PRIVATE: our warnings are not a consumer's problem.
    target_compile_options(${lib} PRIVATE ${STRIPSEQ_WARNINGS})

    add_executable(${exe} ${STRIPSEQ_TEST_SOURCES})
    target_link_libraries(${exe} PRIVATE ${lib} doctest::doctest)
    target_compile_options(${exe} PRIVATE ${STRIPSEQ_WARNINGS})

    doctest_discover_tests(${exe} TEST_PREFIX "${name}:")
endfunction()
```

**`PUBLIC` on the definitions is the one word that must not be wrong**, because getting it wrong produces no diagnostic at all. With `PRIVATE`, the library compiles against an eight-byte `Color` while the tests compile against a six-byte one, and the two link cleanly — `Color::rgb(uint16_t, uint16_t, uint16_t)` mangles identically whether or not the class carries a fourth member, so neither the compiler nor the linker can see the mismatch. The result is a binary that writes past its own pixels, surfacing as corruption somewhere unrelated. `PUBLIC` on the include directory and on `cxx_std_20` is the same argument: the headers are what tests and consumers actually compile.

**Five calls, not four.** The cross product of the two behavioural macros is four, and `default` is one of them — called with no definitions at all, so that `config.h`'s `#ifndef` values are exercised *as* the defaults. Setting both macros explicitly in every variant would leave the declared defaults untested, and a typo turning the default white channel on would pass the whole suite. The fifth is `tight`, at `STRIPSEQ_MAX_STEPS=3 STRIPSEQ_MAX_SEGMENTS=1`: the capacities are plain constants and do not need their own cross product, but one cheap variant catches anything that quietly assumes 8 — an off-by-one in the builder's capacity check, or a test with a hardcoded step count.

**The variants are test scaffolding, not the product.** Consumers link a separate canonical `stripseq` target with an `StripSequencer::stripseq` alias, built with no definitions. Aliasing one of the variants instead would save a compile and quietly export that variant's `PUBLIC` definitions to every consumer, pinning their configuration from inside the library.

The whole helper lives behind `STRIPSEQ_BUILD_TESTS`. CMake target names are globally unique across a build tree, so a consumer pulling this in via `add_subdirectory` should see exactly one target rather than five libraries and five executables competing for names.

Tests that only make sense in one configuration guard on the macro rather than living in a separate file, so a test never silently stops running because someone forgot to add it to a list.

**doctest** is the test framework, fetched with `FetchContent` and guarded by `STRIPSEQ_BUILD_TESTS`, which defaults to `PROJECT_IS_TOP_LEVEL` so a consumer never builds our tests. It is a single header, compiles far faster than Catch2 or GoogleTest, and needs no separate build — which matters because the test suite is compiled five times, once per variant, and that cost is inherent: the tests see the headers, so they cannot be shared across configurations. doctest also ships the `doctest_discover_tests` module used above, which enumerates the binary's cases and registers each with CTest. That buys real parallelism under `ctest -j` and failure output that names the case, where a plain `add_test` per variant would report five results and tell you only which configuration broke.

The **freestanding cross-compile check** is the cheapest possible proxy for "this actually works on the MCU": compile the library only — no tests, no link — with `arm-none-eabi-g++ -mcpu=cortex-m4 -ffreestanding -fno-exceptions -fno-rtti -Os`. It catches host-libc dependencies, accidental heap use through `operator new`, and code that only compiles because the host happens to be 64-bit. Run it in CI from Phase 0, where fixing a violation is one line, rather than in Phase 6, where it is an archaeology exercise.

CI is two GitHub Actions host jobs, gcc and clang, plus the cross-compile job. The variants are not a matrix axis: one configure produces all five libraries and all five test binaries in the same tree, and `doctest_discover_tests` reports each case under its variant's prefix — so a matrix over variants would rebuild everything five times for the same coverage and the same per-variant reporting. Adding the Zephyr build job waits for Phase 6.

**Exit criteria:**

- `cmake --build build && ctest` passes in all five variants on gcc and clang, RGBW among them and not as an afterthought.
- The cross-compile job produces an object file.
- The umbrella header compiles standalone (a `#include <stripseq/stripseq.h>`-only translation unit, which catches missing includes inside headers).

## Phase 1 — Configuration, color and output conversion

**Goal:** all color math, including the conversion to the 8-bit buffer. This is the whole bottom of the dependency graph, and it is pure enough to be tested exhaustively.

**Deliverables:**

- `config.h` complete.
- `color.h` with every factory and `lerp`, all `constexpr`.
- `colors::` constants.
- `src/color.cpp` with the gamma table and the internal `to_bytes(Color, uint8_t brightness, uint8_t* out)` used by `Strip`.

**Implementation notes.**

`rgb8` scales up by **×257**, not `<<8`. 257 maps 255 to exactly 65535 and 0 to 0; a left shift maps 255 to 65280 and can never reach full scale, which shows up as a strip that cannot quite hit white and as an off-by-one in every round-trip test.

`lerp` computes `(a * (65535 - t) + b * t) / 65535` in `uint32_t`. The alternative `a + (b - a) * t / 65535` needs a signed intermediate and is easy to get wrong when `b < a`. The invariants to test are exact endpoints (`t=0` gives `a`, `t=65535` gives `b`) and monotonicity in `t` — both cheap to check exhaustively over a sampled grid, and both things a naive shift-based version fails.

`hsv` is integer sextant math: split the 16-bit hue into a sextant index and a 16-bit position within it, then three cases per sextant. It leaves `w` at 0 in an RGBW build, as the sketch says. Test that saturation 0 gives an equal-channel gray, that value 0 gives black at every hue, and that the six sextant boundaries are continuous — a one-off at a boundary is invisible in a static gradient and obvious as a flicker in a rotating rainbow.

**The output stage is where two real decisions live**, and both are made here because `Strip` will simply call this function.

*Brightness applies before gamma, in linear space.* The channel is scaled `v * brightness / 255` while still 16-bit and linear, and gamma encodes the result. Applying brightness after gamma encoding — scaling the 8-bit value — is the common shortcut and it is wrong: it makes brightness non-linear in perceived output and shifts hue as channels crush toward zero at different rates.

*Gamma is a 256-entry `uint8_t` table indexed by the top 8 bits of the 16-bit channel.* This deserves its rationale written down, because it looks like a precision loss and is not one. The output is 8 bits, so at most 256 distinct values can leave the library no matter how the mapping is done; gamma compresses the low end so several input values map to the same output regardless. The 16-bit internal representation exists to keep **blending** accurate through long fades and multi-step lookups, not to produce more output levels than the wire format has. The extra eight bits are consumed by the conversion by design. The table costs ~256 bytes of flash and is compiled out entirely when `STRIPSEQ_GAMMA=0`, in which case conversion is a plain `>> 8`.

The W channel goes through the identical path. The library has no opinion about the emitter's chromaticity — that is the same refusal that keeps it from deriving W in the first place.

**Exit criteria:**

- Exhaustive or grid tests for `lerp` monotonicity and endpoints, `hsv` continuity, `rgb8` full-scale mapping, and conversion at brightness 0 / 128 / 255 with gamma on and off.
- `sizeof(Color)` asserted in both builds.

## Phase 2 — Sequence

**Goal:** the heart of the design. `Sequence::at()` is the function every other part exists to call, and every semantic that DESIGN.md argues for in prose is decided inside it. This is the phase most likely to change the design if something is wrong, which is why it comes before anything that depends on it.

**Deliverables:**

- `Transition`.
- `Step` with its layout `static_assert`.
- `Sample`.
- `hold_forever`.
- `Error`.
- `Sequence` with `at`/`loop_duration_ms`/`total_duration_ms`.
- `SequenceBuilder` with `build` and `build_const`.

**Implementation notes.**

*The modulo is the classic trap.* For an endless sequence, `playhead % loop` in C++ yields a **negative** result for a negative playhead — which is exactly the case DESIGN.md calls the mechanism rather than an edge case. The correct form is `((p % loop) + loop) % loop`, and it needs a test with a large negative playhead, not just `-1`.

*Hold values come out of the lookup for free, and each case is distinct.* Inside a `Step` transition, the hold is the time to that step's end. Inside a fade, it is 0 — asking for frame rate. Before the start of a finite timeline, it is `-playhead` — the time until this pixel starts moving, which is precisely what lets a wipe report a real deadline instead of a frame rate, and the case a naive implementation returns `hold_forever` for. Past the end of a finite timeline, and for a sequence with no steps, it is `hold_forever`.

*Easing is integer on a 0..65535 parameter.* `EaseIn` is `t²`, `EaseOut` is `1-(1-t)²`, `EaseInOut` is smoothstep, all with 32-bit or 64-bit intermediates. The testable invariants are `f(0) == 0`, `f(65535) == 65535` and monotonicity. Endpoint exactness matters more than it sounds: a curve that returns 65534 at the top leaves a one-LSB step at every transition boundary, which is a visible tick in a slow breathe.

*The builder rejects a sequence longer than 2^29 ms.* `Error::TooLong` covers the total for a finite sequence and one loop for an endless one. The limit is what lets the sequencer cap a finite playhead at 2^30 and still have every segment finish, and lets an endless one re-base before its playhead runs out of range; see Phase 3. Check it without overflowing on the way: summing step durations and multiplying by `repeats` both exceed `uint32_t` long before they look suspicious, so accumulate in `uint64_t`, or compare against the limit before each addition and multiplication.

*The builder needs a sticky error.* `fade_to` called nine times against a capacity of eight must not clobber the ninth or silently drop it; the builder records the first `Error` it hits and every later call is a no-op until `build()` reports it. Without this, over-long sequences fail in whichever way the copy loop happens to fail.

*`build_const()` cannot use `throw` to fail.* The usual `consteval` trick — throw on invalid input, which is a compile error in a constant expression — is ill-formed under `-fno-exceptions`, which is how Zephyr builds C++ by default. The portable form is to call a **declared-but-not-`constexpr`** function on the error path, one per `Error`:

```cpp
// Declared, never defined, never constexpr. Calling one in a constant
// expression is a compile error naming that function; calling it at run
// time is a link error. Both are what we want.
namespace invalid_sequence {
void sequence_has_no_steps();
void sequence_has_a_zero_duration_fade();
// ... one for each Error
}

consteval Sequence SequenceBuilder::build_const() const {
    Sequence out{};
    switch (build(out)) {
    case Error::None:             break;
    case Error::NoSteps:          invalid_sequence::sequence_has_no_steps(); break;
    case Error::ZeroDurationFade: invalid_sequence::sequence_has_a_zero_duration_fade(); break;
    // ...
    }
    return out;
}
```

One function per error rather than one for all is what the diagnostic is for: it is the message every user of a malformed compile-time sequence reads, so the function's name is written to be that message. Clang prints only the unqualified name, so each name reads as a sentence on its own. The names also let the `try_compile` check assert that each probe failed for its own reason — a probe that fails some other way, a missing include or a different rule firing first, fails the check rather than passing it.

*An unset `start_color` is black, and an unset `landing_color` is the last step's color.* The builder tracks set versus unset and `build()` writes the resolved color into the `Sequence`, so `at()` never branches on it. The alternative is the adjacent step's color at both ends, and the start end rules it out: the first pass fades *from* `start_color`, which is what puts the wipe's wavefront mid-transition, so a default of the first step's color would turn a one-step finite fade into a fade from a color to itself. At the other end, a finite sequence that runs to completion and then blanks is almost never wanted. The rule is recorded in the sketch beside the builder.

**Guides.** Two, and a section of a third.

- [`constant-initialisation-guide.md`](../notes/constant-initialisation-guide.md) — "why does nothing here run before `main`?" `constexpr`, `inline constexpr`, `constinit`, `consteval`, and what a function-local static costs on a target with no threads. It belongs to this phase because `build_const()` is what makes `consteval` real; written any earlier, the material would have to be rewritten once it is. [`cross-compiling-guide.md`](../notes/cross-compiling-guide.md) §5–§6 already covers the object-file evidence, and its Exercises 3 and 4 show the two costs directly, so the guide builds on those rather than repeating them.
- [`integer-arithmetic-guide.md`](../notes/integer-arithmetic-guide.md) — exact arithmetic with no floating point: the signed remainder behind the ring, negating `INT32_MIN`, overflow in intermediates, why the curves stay monotonic, and the 64-bit division a Cortex-M calls a library for. `at()`, `lerp` and the easing curves are the first arithmetic in the library, so this is where the material is.
- [`testing-guide.md`](../notes/testing-guide.md) §7 — tests that must not compile: why the `build_const()` check is a `try_compile` at configure time, why each probe names its failure, and why the probe's headers are configure dependencies.

**Exit criteria:**

- `at()` tested across negative, zero, mid-step, exact-boundary, past-end and far-negative playheads, for endless and finite sequences, with and without start/landing colors.
- Every `Error` produced by a builder that triggers it.
- `TooLong` accepted at exactly 2^29 ms and rejected one past it, including through a loop and repeat count whose product overflows `uint32_t`.
- A `build_const()` compile-failure test (CMake `try_compile` asserting failure), re-run by the next build after any change to the headers it compiles.
- The `Step` layout assertion passing in both builds.

## Phase 3 — Sequencer

**Goal:** the playhead, derived rather than stored, and safe against the 49.7-day `uint32_t` millisecond wrap.

**Deliverables:**

- `load`.
- `play`.
- `pause`.
- `seek`.
- `playing`.
- `sequence`.
- `playhead`.

**Implementation notes.**

The core is two lines: playing gives `static_cast<int32_t>(now_ms - origin_ms_)`, not playing gives `frozen_`. The unsigned subtraction before the cast is what makes the millisecond counter's wrap a non-event — the difference stays correct across it as long as the true elapsed time fits in the range.

*Re-basing keeps an endless sequence inside that range forever.* When the derived playhead reaches or exceeds the loop duration, add whole loops to `origin_ms_` and subtract the same from the playhead. Because the shift is a whole number of loops, phase is preserved exactly and no pixel moves. Doing it on every call — keeping the playhead permanently in `[0, loop)` — is simpler and more robust than a threshold, and costs one comparison. Skip it when the loop is zero, which is the empty sequence `load({})` leaves; otherwise the re-base divides by zero.

*Finite sequences cannot re-base*, since their position is absolute, but left playing they need the same protection: the Wipe recipe never pauses, and its playhead would overflow `int32_t` after about 24.8 days and show `start_color`. Cap it at a large constant, 2^30 (about 12.4 days), by moving the origin: when the derived playhead exceeds the cap, set `origin_ms_ = now_ms - cap` and return the cap. Clamping only the returned value is not enough, because the unsigned difference underneath keeps growing and the cast turns it negative at 24.8 days regardless. The cap is a constant rather than `total_duration_ms()` because `finished()` compares the playhead *minus a stagger* against the total; the builder's 2^29 limit on the total and Phase 4's overflow guard on the stagger keep a capped playhead minus any stagger past the end of every finite sequence. Capping cannot change behaviour, because everything past the end of a finite timeline reports the landing color and `hold_forever` regardless.

*`play()` re-bases or caps once, at the instant it starts following the clock.* A playhead sought near `INT32_MAX` and then played would otherwise overflow before the next `playhead()` call could move it, and a finite sequence would show `start_color`. `seek` stays exact, so a paused progress bar reads back what it set. `pause()` records `playhead(now)` for the same reason: one function turns a clock reading into a playhead.

`play()` on an already-playing sequencer must be a no-op, not a restart: recomputing the origin from the current playhead is the identity. Worth an explicit test, because getting it wrong turns any redundant `play()` call into a visible jump.

**Guides.** One, and a section of another.

- [`wrapping-clocks-guide.md`](../notes/wrapping-clocks-guide.md) — keeping a position from a clock that wraps: deriving it rather than counting it, subtracting before converting, the 24.8-day limit on the difference rather than the 49.7-day wrap of the clock, moving the origin to stay inside it, and testing sixty days in milliseconds. The `Sequencer` is the whole of the material, so the guide belongs to the phase that writes it.
- [`testing-guide.md`](../notes/testing-guide.md) §8 — tests that pass for the wrong reason: planting bugs to find what the suite misses, the equivalent mutant `pause()` turned out to guard against, and the division by zero that arm64 forgives, x86-64 traps and a Cortex-M under Zephyr faults on, which is the case for running the host suite under UBSan.

**Exit criteria:**

- `pause`/`play` round-trips preserve the playhead exactly.
- `seek` stops the playhead following the clock.
- `play` while playing and `pause` while paused leave the playhead exactly where it was.
- Re-basing over a simulated ten-day run keeps positions identical to the un-rebased arithmetic.
- A simulated `uint32_t` wrap of `now_ms` produces no discontinuity.
- A finite sequence left playing for a simulated 60 days still reports its landing color, and its playhead stays at 2^30, which minus the largest stagger a segment accepts is still past any total the builder accepts.
- `play()` after a `seek` within a few ms of `INT32_MAX` keeps a finite sequence on its landing color and an endless one in phase, with the first `playhead()` call well after the overflow would have happened.
- An empty sequence left playing does not divide by zero.
- `load()` resets the playhead to 0 and clears `playing_`.

## Phase 4 — Segment and Strip

**Goal:** pixels. The per-pixel loop, the hold fold, the segment list, and the frame answer.

**Deliverables:**

- `PlayheadFn`.
- `ColorFn`.
- `spread`.
- `Segment` with its setters and `finished`.
- `Strip` complete.

**Implementation notes.**

*Segments write converted 8-bit bytes straight into the output buffer.* There is no 16-bit shadow buffer, and there must not be one — it would be per-pixel state, the thing DESIGN.md forbids outright, and on a 300-pixel RGBW strip it would cost 2.4 KB. So the render loop is: `memset` the strip's pixels to zero, then for each segment, for each pixel, compute the playhead, look up the `Sample`, convert through brightness and gamma, and store. Overlap resolves as last-writer-wins at 8 bits, which is exactly the documented behaviour. This also means brightness and gamma are applied once per painted pixel rather than once per strip pixel — slightly more work under overlap, and not worth a second pass to avoid.

*The default `PlayheadFn` uses an arithmetic shift, not a division.* `playhead_ms - ((index * delay_q8_) >> 8)` floors consistently in both directions; `/ 256` truncates toward zero, which puts a one-count discontinuity at the origin — visible on a mirrored pair as a seam at the centre, and exactly the kind of artefact that gets misdiagnosed as a hardware problem.

*Guard the stagger against overflow at set time, not per pixel.* `index * delay_q8_` overflows `int32_t` for a long segment and a large delay. Checking it inside the per-pixel loop, or promoting to `int64_t` there, costs on every pixel of every frame. Both setters already return `bool`, so the check belongs there: reject a delay whose product with `count_ - 1` would not fit. That reuses surface that already exists for a different rejection and keeps the hot loop as narrow as it looks.

*`finished()` is a full stagger later than the sequencer's own end.* It is `playhead(now) - max_stagger > total_duration_ms()`, with `max_stagger` the largest offset across the segment's pixels — which is `(count_ - 1) * delay_q8_ >> 8` for a positive delay and zero for a negative one. It is false when the sequencer is not playing, false for an endless sequence, and false whenever a `PlayheadFn` replacement is installed, since the library cannot know the offsets a replacement produces.

*`add()` rejects a segment that does not fit the buffer.* The strip knows `pixel_count_`; a segment constructed independently does not. Returning false for `first + count > pixel_count_` catches the mistake at the call site instead of turning it into a buffer overrun at frame rate. The `Segment&` assignment path in `segment(at)` cannot be checked, since a reference has no way to refuse, so `paint` clips to `pixel_count_` as well: an overhanging segment that arrives that way paints its visible part rather than overrunning the buffer. The asymmetry — rejected through `add()`, clipped through assignment — is commented in the header.

*Delete `-Wno-unused-private-field` from `CMakeLists.txt`.* Clang reports private fields nothing reads, and until `Segment` and `Strip` are implemented the stubs read nothing. This is the phase that leaves no field unread, so the suppression goes, and whatever clang still reports is a real finding rather than a placeholder.

*The hold fold is one `min` per pixel over work already being done*, folded again across segments. A pixel reporting 0 cannot be beaten, so folding may stop early — but painting cannot, so the saving is a comparison, not a loop exit.

**Exit criteria:**

- Rendering is verified pure by rendering the same frame twice and comparing buffers.
- Rendering is verified stateless by building the same strip through different call orders and comparing.
- Overlap resolves to the later segment.
- `drop_finished` removes correctly with two overlays finishing out of order, which is the case the backwards loop exists for.
- The cancel sweep over `drives()` removes both of two segments sharing one sequencer and leaves the background segment painting.
- `add()` returns false for a segment running past the end of the strip, and the same segment assigned through `segment(i)` paints only its visible pixels, leaves guard bytes placed after the buffer untouched, and yields a hold from those pixels alone, while a lone segment assigned wholly off the strip paints nothing and `render` returns `hold_forever`.
- `spread` produces a delay whose accumulated error across a ring is under 1 ms, which is the reason `q8` exists at all.
- A finite sequence left playing for a simulated 60 days lets a segment with the largest accepted stagger report `finished()`.

## Phase 5 — The cookbook as tests

**Goal:** prove the design, not just the code. The eleven recipes in the sketch were written to show the API is usable; compiled and run, they become the acceptance suite.

**Deliverables:**

- `tests/cookbook.cpp` containing all eleven recipes verbatim from the sketch, each with property assertions and a golden-frame hash.

**Implementation notes.**

The recipes go in **unmodified**. If a recipe needs an edit to compile, that is a finding about the sketch, and the sketch gets fixed rather than the recipe — this test's whole value is that it is the documented API and not a convenient variant of it.

Each recipe gets **property assertions** rather than only golden bytes, because a property says what broke while a hash only says that something did. Blink: every pixel identical, alternating red and black on a 500 ms period, hold exactly 500 at the start of a step. Comet: brightness decreasing monotonically back from the head. Rainbow: hue advancing with index, and the seam between the last pixel and the first continuous on a ring. Static gradient: identical buffer at two very different `now` values, and `render` returning `hold_forever`. Wipe: after the last pixel lands, everything green and holding forever, and still green after `drop_finished` is *not* called. Progress bar: exactly the expected number of lit pixels at 0/25/50/100 %, with the boundary pixel part-lit. Twinkle: pixels visibly out of phase, and `set_pixel_delay` returning false while the replacement is installed. Overlay: the breathe underneath resumes at the phase it would have reached, which is the single strongest test of the stateless-lookup claim.

A **golden hash** per recipe over a table of timestamps then guards against silent numeric drift in easing or conversion. Golden values are regenerated deliberately and the regeneration diff is reviewed, never blessed automatically.

**Exit criteria:**

- All eleven recipes compile unmodified and pass in every variant.
- Blink is still three lines.

## Phase 6 — Zephyr integration

**Goal:** the library builds inside a Zephyr application and drives the bench hardware — a Nucleo-H753ZI with an 11-pixel RGBW strip on I2S3 / PB2.

**Deliverables:**

- `zephyr/module.yml`, `zephyr/CMakeLists.txt`, `zephyr/Kconfig`.
- A sample app with a board overlay, a `prj.conf` and the frame loop.
- A short bring-up note in `docs/`.

The module directory is thin by construction, and its only real job is translating Kconfig into the macros the core already reads:

```cmake
# zephyr/CMakeLists.txt
if(CONFIG_STRIPSEQ)
  include(${CMAKE_CURRENT_LIST_DIR}/../cmake/sources.cmake)
  zephyr_library()
  zephyr_library_sources(${STRIPSEQ_SOURCES})
  zephyr_include_directories(${STRIPSEQ_INCLUDE_DIR})
  zephyr_compile_definitions(
    STRIPSEQ_MAX_STEPS=${CONFIG_STRIPSEQ_MAX_STEPS}
    STRIPSEQ_MAX_SEGMENTS=${CONFIG_STRIPSEQ_MAX_SEGMENTS}
    STRIPSEQ_WHITE_CHANNEL=$<BOOL:${CONFIG_STRIPSEQ_WHITE_CHANNEL}>
    STRIPSEQ_GAMMA=$<BOOL:${CONFIG_STRIPSEQ_GAMMA}>)
endif()
```

Because `config.h` guards each macro with `#ifndef`, this needs no Zephyr-aware code anywhere in `include/` or `src/`, and the standalone build is unaffected.

The sample is the adapter from the sketch made real: a `k_work_delayable` frame loop, `k_uptime_get_32()` sampled once per frame, `k_work_reschedule` on the returned hold, the RGBW permute, and the strip update.

### Four things already checked against Zephyr v4.4.1

Read out of `~/zephyr-workspace` rather than assumed. Recorded here so Phase 6 does not rediscover them; the rest of the platform detail can wait until the phase starts.

- **`led_strip_update_rgb()` cannot drive the white die.** `ws2812_i2s.c` resolves `LED_COLOR_ID_WHITE` to a hard zero, and `struct led_rgb` has no fourth channel. RGBW must use `led_strip_update_channels()`.
- **`update_channels()` applies no color mapping**, so the adapter owns the permute. Against our neutral `R,G,B,W`, a `GRBW` strip is one swap of bytes 0 and 1 per pixel.
- **I2S3 needs DMA that nothing enables by default.** `dma1` and `dmamux1` are `status = "disabled"` in `stm32h7.dtsi` and the board dts leaves them that way, while the I2S binding requires `dmas`. The overlay needs both set okay, plus `CONFIG_DMA=y`.
- **The draft overlay's timing values are SK6812, not WS2812B** — `nibble-one = <0xC>` is 625 ns high against the binding default's 937.5 ns, and `reset-delay = <80>` agrees. Consistent with the RGBW mapping, and worth confirming from the part number since it also fixes `STRIPSEQ_WHITE_CHANNEL=1`.

**Bring-up order, cheapest signal first.** Solid red confirms wiring, permute and power. Then a white-only color, because nothing else distinguishes a working `update_channels` path from `update_rgb`'s silent zero. Then blink for the frame loop and the hold path, comet for the stagger and its sign, rainbow for HSV and the ring seam. Last, load a static gradient and confirm the workqueue is genuinely idle rather than spinning — if it is not, the hold fold is returning 0 where it should return `hold_forever`, and that is a Phase 2 bug surfacing late.

**Exit criteria:**

- The sample builds and runs on the board.
- Both `STRIPSEQ_WHITE_CHANNEL` settings build.
- The bring-up sequence passes.

## Phase 7 — Hardening and release

**Goal:** know what it costs, prove the constraints hold mechanically, and make it usable by someone who was not here.

**Deliverables:**

- `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` enabled on the library and its tests.
- Host test runs under UBSan and ASan.
- A fuzz harness over `SequenceBuilder` and `Sequence::at`.
- A CI check of the cross-compiled objects for dynamic initialisation.
- Footprint measurements in the README.
- The README.
- A `v0.1.0` tag.

**Implementation notes.**

Turn on `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` and reach zero. `-Wconversion` is the one that matters here and the one worth enabling last, because this codebase mixes `uint8_t`, `uint16_t`, `int32_t` and `uint32_t` in nearly every expression, and it will find genuine truncations alongside a lot of noise that needs explicit casts.

Run the host tests under **UBSan and ASan**. UBSan is the high-value one: signed overflow in the stagger multiply, the shift-vs-divide question, and the modulo of a negative playhead are all exactly what it detects. A short **fuzz harness** over `SequenceBuilder` and `Sequence::at` — random steps, random playheads including extremes — is worth the hour it takes, because `at()` is the one function that takes fully unconstrained input.

Check the no-dynamic-initialisation rule mechanically rather than by reading: `nm` the cross-compiled object files and assert `.init_array` is empty and no `__cxa_guard` symbol appears. Put it in CI; it is the kind of rule that holds until the day someone adds a convenient function-local static.

Measure and publish the footprint — flash and RAM for each of the four behavioural configurations, plus per-frame cycle cost for a 60-pixel strip — in the README. DESIGN.md and the sketch both make size arguments (a `Sequence` at ~116 bytes, a `Segment` at 20, the macro-versus-template trade); those arguments should end up with measured numbers behind them.

Then the README: what it is, the three-line blink, the four macros, how to consume it standalone and as a Zephyr module, and links to the two design documents. Tag `v0.1.0`.

**Exit criteria:**

- Zero warnings in every variant on gcc and clang.
- The host tests pass under UBSan and ASan with no reports.
- The fuzz harness runs over random steps and random playheads, extremes included, with no sanitizer report.
- CI fails when `.init_array` in a cross-compiled object is non-empty or a `__cxa_guard` symbol appears.
- The README gives flash and RAM for each of the four behavioural configurations and the per-frame cycle cost for a 60-pixel strip.
- Any golden hash a warning fix invalidates is regenerated deliberately and its diff reviewed.
- `v0.1.0` is tagged.

## Decisions deferred to their phase

Everything the sketch settled stays settled. These are genuinely open and are called out where they arise rather than being decided in the abstract now.

| Decision | Phase | Recommendation |
| --- | --- | --- |
| Whether `add()` rejects a segment that exceeds the buffer | 4 | Yes — it is the only place the strip and the segment's extent are both known |
| Whether `set_pixel_delay` rejects a delay that would overflow the stagger | 4 | Yes — it keeps the check out of the per-pixel loop and reuses the existing `bool` |

## Risk register

**`Sequence::at()` turns out to need more cases than expected.** This is the likeliest place the design gets amended, and it is why Phase 2 comes early: an amendment there costs a day, and the same amendment discovered in Phase 5 costs a rewrite of everything above it.

**`-Wconversion` finds a real truncation in the stagger or hold arithmetic.** Likely, and mostly harmless — but any fix that changes a computed value invalidates the golden hashes, so Phase 7 running after Phase 5 means regenerating them. Accepted deliberately: enabling `-Wconversion` early would slow every phase with casts on code still in flux.

**Hardware disagrees with the host.** Expected in some form, and the bring-up order in Phase 6 exists to localise it. The one failure worth pre-empting is the white die staying dark, because the API that looks correct silently zeroes it and three-quarters of the strip looks right while it does.

**The default capacities turn out to be wrong.** `STRIPSEQ_MAX_STEPS=8` and `STRIPSEQ_MAX_SEGMENTS=4` are guesses that the sketch reasons about but nothing has measured. Phase 7's footprint numbers are what turns them into informed defaults, and they are macros precisely so being wrong is cheap.
