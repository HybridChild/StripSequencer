# The guides

Teaching documents, each building one concept from first principles and using this repo — an OS-agnostic C++20 library for animating LED strips, built five times over and cross-compiled for a Cortex-M — as the running example. They are meant to be readable by someone who has never seen this project.

The split mirrors [`../docs/`](../docs/): **`docs/` records what *this* project decided; `notes/` teaches the concept behind it.** Where a guide has a reference half, the table below names it. Several of these guides pair with a *file* rather than a document — the build system is small enough that `CMakeLists.txt` and `cmake/*.cmake` are their own reference.

Guides arrive with the phase that produced them, so this index grows as the implementation does. Everything here so far comes out of **Phase 0**, which built every build path before there was any code worth building.

## Where to start

Only one of these is a prerequisite for the others. Read [`cmake-usage-requirements-guide.md`](cmake-usage-requirements-guide.md) first; after that, take whichever of the rest you need.

1. **[`cmake-usage-requirements-guide.md`](cmake-usage-requirements-guide.md)** — the foundation. What a CMake target is, and what `PUBLIC` really controls. Everything below assumes it, and the bug in its §6 is the reason the other four exist in the shape they do.
2. **[`build-variants-guide.md`](build-variants-guide.md)** — the direct consequence: when a build-time macro changes a *type*, one compiled library is not enough, and the build has to carry several at once.
3. **[`testing-guide.md`](testing-guide.md)** — how four test cases become twenty CTest results, and why the suite is compiled once per variant. Wants #2 for that last argument.
4. **[`dependencies-guide.md`](dependencies-guide.md)** — *self-contained after #1.* Where `doctest::doctest` comes from: installed packages, fetched ones, and the imported targets both produce.
5. **[`cross-compiling-guide.md`](cross-compiling-guide.md)** — *off to the side.* Compiling for a machine you cannot run: toolchain files, freestanding C++, and the sharp distinction between what a compile-only check proves and what it does not.

### The dependencies, at a glance

Arrows mean "assumes". There is one root, and the other four hang off it.

```
                                    ┌─▶ build-variants ──▶ testing
cmake-usage-requirements ───────────┤     one library,       doctest + CTest,
  targets, PUBLIC/PRIVATE,          │     five builds        and the five-times cost
  the invisible mismatch            │
                                    ├─▶ dependencies
                                    │     find_package, FetchContent
                                    │
                                    └─▶ cross-compiling   (only §3 leans on the root)
                                          toolchain files, freestanding, nm
```

### Shortcuts

- **New to CMake?** #1 alone answers most day-to-day confusion; its §8 exercises take about ten minutes and two of them break the build on purpose.
- **Here for the embedded side?** #5 stands almost alone — read §3 of #1 first if the "the library sets no flags" rule looks arbitrary.
- **Wondering why `ctest` prints twenty results for four tests?** #3 §3–§4, and nothing else.
- **Chasing a build failure?** The lab at the end of each guide is a set of numbered exercises with commands, expected output and a **Proves:** line. Several of them *are* the failure you are looking at, reproduced deliberately.

## The five

Rows are in the reading order above, so the column to scan for "can I start here?" is **Assumes you know**.

| # | Guide | Answers | Assumes you know | Reference half |
| - | ----- | ------- | ---------------- | -------------- |
| 1 | [`cmake-usage-requirements-guide.md`](cmake-usage-requirements-guide.md) | What does `PUBLIC` mean, if not access control? | What a compiler and linker do | *(the root `CMakeLists.txt` and `cmake/rgbseq_variant.cmake`)* |
| 2 | [`build-variants-guide.md`](build-variants-guide.md) | Why does one library need five builds? | #1 | [`../docs/API-SKETCH.md`](../docs/API-SKETCH.md), "Build-time configuration" |
| 3 | [`testing-guide.md`](testing-guide.md) | How does one binary become twenty test cases? | #2, for §5 | [`../docs/IMPLEMENTATION-PLAN.md`](../docs/IMPLEMENTATION-PLAN.md), Phase 0 and Phase 5 |
| 4 | [`dependencies-guide.md`](dependencies-guide.md) | Where does someone else's library come from? | #1 | *(the `RGBSEQ_BUILD_TESTS` block in the root `CMakeLists.txt`)* |
| 5 | [`cross-compiling-guide.md`](cross-compiling-guide.md) | What does compiling for a machine you cannot run actually prove? | #1 §3–§5, lightly; roughly what a microcontroller is | *(`cmake/arm-none-eabi.cmake` and `scripts/cross-check.sh`)* |

## Not yet written

- **Constant initialisation — "why does nothing here run before `main`?"** `constexpr`, `inline constexpr`, `constinit`, `consteval`, and what a function-local static costs on a target with no threads. Deliberately deferred to **Phase 2**, when `SequenceBuilder::build_const()` makes `consteval` real; today the material would have to be rewritten twice. Until then, [`cross-compiling-guide.md`](cross-compiling-guide.md) §5–§6 covers the object-file evidence and its Exercises 3 and 4 show the two costs directly.
