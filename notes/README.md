# The guides

Teaching documents, each building one concept from first principles and using this repo — an OS-agnostic C++20 library for animating LED strips, built five times over and cross-compiled for a Cortex-M — as the running example. They are meant to be readable by someone who has never seen this project.

The split mirrors [`../docs/`](../docs/): **`docs/` and the comments in the code record what *this* project decided; `notes/` teaches the concept behind it.** Each guide's reference half is a file rather than a document, because every decision is recorded beside the code it governs, and the table below names it.

## Where to start

The guides fall into two groups. The first five are about the build — targets, variants, tests, dependencies, toolchains — and have one root: read [`cmake-usage-requirements-guide.md`](cmake-usage-requirements-guide.md) first, then take whichever of the rest you need. The other four are about what the library computes: two need only the object-file reading in §5–§6 of the cross-compiling guide, and the other two lean lightly on the integer-arithmetic one.

1. **[`cmake-usage-requirements-guide.md`](cmake-usage-requirements-guide.md)** — the foundation. What a CMake target is, and what `PUBLIC` really controls. Every build guide below assumes it, and the bug in its §6 is the reason the other four exist in the shape they do.
2. **[`build-variants-guide.md`](build-variants-guide.md)** — the direct consequence: when a build-time macro changes a *type*, one compiled library is not enough, and the build has to carry several at once.
3. **[`testing-guide.md`](testing-guide.md)** — how each test case becomes five CTest results, one per configuration, why the suite is compiled once per variant, how to test code that must *not* compile, how to find out what a passing suite does not see, and how to test with inputs nobody wrote by hand. Wants #2 for the per-variant argument, #6 §4 for what the compile-failure probes are probing, and #7 lightly for the generated tests.
4. **[`dependencies-guide.md`](dependencies-guide.md)** — *self-contained after #1.* Where `doctest::doctest` comes from: installed packages, fetched ones, and the imported targets both produce.
5. **[`cross-compiling-guide.md`](cross-compiling-guide.md)** — *off to the side.* Compiling for a machine you cannot run: toolchain files, freestanding C++, and the sharp distinction between what a compile-only check proves and what it does not. Also the bridge to the C++ guides: its §5–§6 teach reading an object file with `nm` and `objdump`, which both of them use as evidence.
6. **[`constant-initialisation-guide.md`](constant-initialisation-guide.md)** — why nothing in this library runs before `main`: `constexpr`, `consteval`, `constinit` and `inline constexpr`, what each one actually promises, and how `build_const()` makes an invalid sequence a compile error. Wants #5 §5–§6.
7. **[`integer-arithmetic-guide.md`](integer-arithmetic-guide.md)** — *nearly self-contained.* Exact arithmetic with no floating point: signed remainders, overflow in intermediates, rounding that stays monotonic, rounding an irrational value by comparison, and the 64-bit division a Cortex-M has to call a library for. Wants only `nm -u` from #5 §6.
8. **[`wrapping-clocks-guide.md`](wrapping-clocks-guide.md)** — *nearly self-contained.* Keeping a position from a millisecond clock that wraps: deriving instead of counting, subtracting before converting, the real 24.8-day limit, the two ways past it — moving the origin or widening the type — narrowing the answer for the code that consumes it, refusing the narrow type at compile time, and testing sixty days in milliseconds. Wants #7 for unsigned wrap, signed overflow and the negative remainder, lightly, and #3 §7 for one contrast.
9. **[`gamma-and-color-guide.md`](gamma-and-color-guide.md)** — *nearly self-contained.* Color on an addressable LED: emitted light against perceived brightness, the gamma table between them, why sixteen bits go in and eight come out, where a brightness knob belongs and what it costs at the dim end, and HSV in integers. Wants #7 §1 and §4 for the casts, and §7 only to see how the table is computed.

### The dependencies, at a glance

Arrows mean "assumes". There is one root; the build guides hang off it, and the C++ guides hang off the cross-compiling guide's object-file sections, or off each other, rather than off the root.

```
                                    ┌─▶ build-variants ──▶ testing
cmake-usage-requirements ───────────┤     one library,       doctest + CTest, the five-times
  targets, PUBLIC/PRIVATE,          │     five builds        cost, and tests that must not compile
  the invisible mismatch            │
                                    ├─▶ dependencies
                                    │     find_package, FetchContent
                                    │
                                    └─▶ cross-compiling ──────┬─▶ constant-initialisation
                                          toolchain files,    │     constexpr, consteval, constinit,
                                          freestanding, nm    │     what runs before main
                                          (only §3 leans      │
                                           on the root)       └─▶ integer-arithmetic ──┬─▶ wrapping-clocks
                                                                    remainders,        │   derive, don't count;
                                                                    overflow, floors,  │   subtract, then convert;
                                                                    rounding a root,   │   move the origin or widen
                                                                    the division the   │
                                                                    chip cannot do     └─▶ gamma-and-color
                                                                                           light vs the eye, the table,
                                                                                           16 in, 8 out, brightness, HSV
```

### Shortcuts

- **New to CMake?** #1 alone answers most day-to-day confusion; its §8 exercises take about ten minutes and two of them break the build on purpose.
- **Here for the embedded side?** #5 stands almost alone — read §3 of #1 first if the "the library sets no flags" rule looks arbitrary.
- **Here for the C++?** #6 to #9 need none of the CMake guides. Skim §5–§6 of #5 for `nm` and `objdump`, then read #6 or #7; #8 and #9 want only #7.
- **Wondering what `constinit` does that `constexpr` does not?** #6 §5, and its Exercise 3.
- **Fades snap off at the bottom, or dimming changes the color?** #9 §2–§5, and its Exercises 2 and 6.
- **Wondering why `ctest` prints five results for every test case?** #3 §3–§4, and nothing else.
- **Timing something with `millis()` or `k_uptime_get_32()`?** #8 §2–§3 is the whole of what you need, and its Exercise 2 takes two minutes.
- **A test passes here and crashes elsewhere?** #3 §8, and its Exercise 9.
- **A function with too many inputs to test by example?** #3 §9, and its Exercises 10–12.
- **Chasing a build failure?** The lab at the end of each guide is a set of numbered exercises with commands, expected output and a **Proves:** line. Several of them *are* the failure you are looking at, reproduced deliberately.

## All of them

Rows are in the reading order above, so the column to scan for "can I start here?" is **Assumes you know**.

| # | Guide | Answers | Assumes you know | Reference half |
| - | ----- | ------- | ---------------- | -------------- |
| 1 | [`cmake-usage-requirements-guide.md`](cmake-usage-requirements-guide.md) | What does `PUBLIC` mean, if not access control? | What a compiler and linker do | *(the root `CMakeLists.txt` and `cmake/stripseq_variant.cmake`)* |
| 2 | [`build-variants-guide.md`](build-variants-guide.md) | Why does one library need five builds? | #1 | *(`include/stripseq/config.h`)* |
| 3 | [`testing-guide.md`](testing-guide.md) | How does one binary become a result for every case — and how do you test what must not compile? | #2, for §5; #6 §4, for §7; #8, lightly, for §8; #7, lightly, for §9 | *(`tests/cookbook.cpp` and `cmake/stripseq_compile_fail.cmake`)* |
| 4 | [`dependencies-guide.md`](dependencies-guide.md) | Where does someone else's library come from? | #1 | *(the `STRIPSEQ_BUILD_TESTS` block in the root `CMakeLists.txt`)* |
| 5 | [`cross-compiling-guide.md`](cross-compiling-guide.md) | What does compiling for a machine you cannot run actually prove? | #1 §3–§5, lightly; roughly what a microcontroller is | *(`cmake/arm-none-eabi.cmake` and `scripts/cross-check.sh`)* |
| 6 | [`constant-initialisation-guide.md`](constant-initialisation-guide.md) | Why does nothing here run before `main`, and which keyword decides that? | #5 §5–§6; C++ classes and functions | *(`SequenceBuilder::build_const()` in `include/stripseq/sequence.h`)* |
| 7 | [`integer-arithmetic-guide.md`](integer-arithmetic-guide.md) | How do you do exact arithmetic with no floating point and no 64-bit divide? | #5 §6, for `nm -u`; C++ integer types | *(`src/sequence.cpp`, `Color::lerp()` in `include/stripseq/color.h`, and `make_gamma_table()` in `src/color.cpp`)* |
| 8 | [`wrapping-clocks-guide.md`](wrapping-clocks-guide.md) | How do you keep a position from a clock that wraps, for as long as the device runs? | #7 §1–§3, lightly; C++ integer types | *(`include/stripseq/sequencer.h` and `src/sequencer.cpp`)* |
| 9 | [`gamma-and-color-guide.md`](gamma-and-color-guide.md) | Why does a fade snap off at the bottom, and where does a brightness knob belong? | #7 §1 and §4, lightly; C++ integer types | *(`src/color.cpp`, `to_bytes()` in `src/color_internal.h` and `Color::hsv()` in `include/stripseq/color.h`)* |
