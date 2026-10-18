# One library, five builds

*A from-first-principles guide to build-time configuration that changes a type, and what that costs the build.*

This is a teaching document, not project documentation. It explains why some configuration options can be compiled once and others cannot, what a build looks like when it has to carry several configurations at once, and how to choose which combinations are worth building. The running example is this repository, whose four `#ifndef`-guarded macros produce five libraries and five test binaries from one source tree. It assumes [`cmake-usage-requirements-guide.md`](cmake-usage-requirements-guide.md), whose `PUBLIC`/`PRIVATE` distinction is the reason any of this works.

**The shape of this document:**

- **§1–§2** — the distinction the whole design turns on: a knob that changes a *value* versus one that changes a *type*.
- **§3–§4** — one target per combination, and why this repo builds five rather than four.
- **§5–§6** — keeping the variants out of the product, and writing tests that only apply to one of them.
- **§7** — four exercises, including adding a variant of your own.
- **§8–§9** — the model in a paragraph, and where to go next.

---

## 1. Two kinds of build-time knob

This library has four compile-time macros, and it is tempting to treat them as one category. They are not:

```c
#define STRIPSEQ_MAX_STEPS 8        /* steps a sequence can hold */
#define STRIPSEQ_MAX_SEGMENTS 4     /* segments a strip can hold */
#define STRIPSEQ_WHITE_CHANNEL 0    /* 1 adds the fourth channel everywhere */
#define STRIPSEQ_GAMMA 1            /* 1 links the correction table */
```

The first two change **how big an array is**. Code compiled with `STRIPSEQ_MAX_STEPS=8` and code compiled with `=64` still agree about what a `Step` is and what `Sequence::at()` does; they disagree only about a capacity, and every function still means the same thing.

The last two change **what the code is**. `STRIPSEQ_WHITE_CHANNEL` adds a member to `Color`, which changes `sizeof(Color)`, `sizeof(Step)`, the layout of everything containing one, and `Strip::bytes_per_pixel`. `STRIPSEQ_GAMMA` changes what the output conversion computes and whether a 256-byte table is linked at all.

The line between the two is not "does it affect behaviour" — all four do. It is **whether two translation units compiled with different values can be linked into one program without lying to each other**. For a capacity, mostly yes. For a layout, never, and silently: as [`cmake-usage-requirements-guide.md`](cmake-usage-requirements-guide.md) §6 shows, the two halves export identical symbols and link cleanly.

## 2. Why the layout knob is a macro at all

The reflex when reading `#if STRIPSEQ_WHITE_CHANNEL` in a modern C++ codebase is that it should have been a template parameter. For this particular knob, it could not have been.

A template parameter can change a value, an algorithm, even a base class — but it cannot add a member to a type that other, non-template types embed by value. Making `Color<W>` a template means `Step<W>`, `Sequence<W>`, `Sample<W>`, `Segment<W>` and `Strip<W>` all follow, because each holds a `Color` directly, and then no two of them can be mixed and the parameter has infected the entire public surface. The macro puts the same decision in one place at the cost of one build per setting — which is what the rest of this guide is about. The longer argument, including why the two *capacity* macros are macros as well when they did not have to be, is beside the macros themselves, in `include/stripseq/config.h`.

## 3. One compiled artifact per combination

If no single artifact can represent both settings of a behavioural macro, the build must produce several. In CMake that means one target per combination, which is a function:

```cmake
function(stripseq_add_variant name)
    set(lib stripseq_${name})
    set(exe tests_${name})

    add_library(${lib} STATIC ${STRIPSEQ_SOURCES})
    target_include_directories(${lib} PUBLIC ${STRIPSEQ_INCLUDE_DIR})
    target_compile_features(${lib} PUBLIC cxx_std_20)
    target_compile_definitions(${lib} PUBLIC ${ARGN})     # see the other guide
    target_compile_options(${lib} PRIVATE ${STRIPSEQ_WARNINGS})

    add_executable(${exe} ${STRIPSEQ_TEST_SOURCES})
    target_link_libraries(${exe} PRIVATE ${lib} doctest::doctest)
    ...
    doctest_discover_tests(${exe} TEST_PREFIX "${name}:")
endfunction()
```

Three things about this shape generalise beyond this repo.

**The test binary is part of the variant, not shared between variants.** It has to be: the tests include the headers, so they are compiled under the same macros and there is no such thing as a configuration-neutral test binary. This is the cost of the whole approach — the suite is compiled once per variant — and it is why the framework choice matters (see [`testing-guide.md`](testing-guide.md) §5).

**`${ARGN}` carries the whole configuration.** Everything after `name` is passed straight to `target_compile_definitions`, so adding a combination is one line at the call site rather than an edit inside the function.

**The prefix keeps the results legible.** `TEST_PREFIX "${name}:"` means a failure reports as `rgbw_nogamma:hsv is continuous at the sextant boundaries` rather than as one of five identical-looking failures. Configuration-dependent bugs are common enough that the configuration belongs in the test's name.

## 4. Five calls, not four

Two behavioural macros give four combinations. This repo makes five calls:

```cmake
stripseq_add_variant(default)                                        # config.h's own defaults
stripseq_add_variant(rgbw          STRIPSEQ_WHITE_CHANNEL=1)
stripseq_add_variant(nogamma       STRIPSEQ_GAMMA=0)
stripseq_add_variant(rgbw_nogamma  STRIPSEQ_WHITE_CHANNEL=1 STRIPSEQ_GAMMA=0)
stripseq_add_variant(tight         STRIPSEQ_MAX_STEPS=6 STRIPSEQ_MAX_SEGMENTS=2)
```

**Each call passes only its deviations from the defaults, and `default` passes nothing at all.** That is deliberate and easy to get wrong in the other direction. Writing `stripseq_add_variant(default STRIPSEQ_WHITE_CHANNEL=0 STRIPSEQ_GAMMA=1)` looks more explicit and is strictly worse: the variant named for the defaults would then compile a copy of them rather than `config.h`'s, and whether the declared values were compiled at all would depend on which other variants happened to leave a knob alone. A default is only tested by *not* setting it.

Not setting it is still only half of testing it, because compiling a value is not checking it. Every test reads the macros it was compiled with, so a mistyped default builds a different library that agrees with itself, and the whole suite passes. What checks the value is a second copy of it held somewhere else: `tests/layout.cpp` writes the documented defaults out again and asserts them in every variant that leaves a knob unset. It is the move [`cmake-usage-requirements-guide.md`](cmake-usage-requirements-guide.md) §7 makes for propagation — an expectation that does not come from the mechanism under test — and changing a default then means changing it in both places, which is right for a decision about every consumer's build.

**The fifth call is not part of any cross product.** `tight` exists because the capacity macros, while safe to share, are still a place where code quietly assumes the default: an off-by-one in a capacity check, a loop bounded by a literal `8`, a test that hardcodes how many steps fit. One cheap variant at awkward values catches that class of mistake, and unlike the behavioural macros it does not need its own cross product — the capacities do not interact with the layout, so one sample is enough.

The general rule: **cross-product the knobs that change meaning, sample the knobs that change size.** The first grows exponentially and you cannot skip any of it; the second grows linearly and one awkward value buys most of the coverage.

## 5. The variants are scaffolding, not the product

There is a sixth target, and it is the one consumers actually use:

```cmake
add_library(stripseq STATIC ${STRIPSEQ_SOURCES})
add_library(StripSequencer::stripseq ALIAS stripseq)
```

built with no definitions at all, outside `STRIPSEQ_BUILD_TESTS`. It would have saved a compile to alias one of the five instead — and it would have been a mistake, because that variant's `PUBLIC` definitions would then travel to every consumer, pinning their configuration from inside the library. A consumer must be able to set `STRIPSEQ_WHITE_CHANNEL=1` in their own build and have it mean something; that only works if the library they link was compiled with the macro *unset* and their build supplies it.

The namespaced alias is convention with a purpose: `StripSequencer::stripseq` cannot be mistaken for a filename, so a typo becomes a configure-time error naming the missing target rather than a link-time hunt for `-lstripseq`.

And the whole helper — all five variants and the test binaries — lives behind `if(STRIPSEQ_BUILD_TESTS)`, which defaults to `PROJECT_IS_TOP_LEVEL`. CMake target names are global across a build tree, so a consumer pulling this in with `add_subdirectory` should see exactly one target, not five libraries and five executables competing for names with their own.

## 6. Tests that only apply to one configuration

Some tests only make sense in one variant: a W-channel test cannot compile in an RGB build, and a gamma-table test has nothing to assert when the table is compiled out. There are two ways to arrange that, and only one of them survives contact with a growing suite.

The tempting one is a separate file per configuration, added to the source list conditionally. It fails quietly: a test that is not in anyone's list simply stops running, the suite stays green, and nothing says the coverage went away.

The other is that every test file is compiled into every variant and guards on the macro:

```cpp
#if STRIPSEQ_WHITE_CHANNEL
TEST_CASE("the W channel survives the conversion unchanged") { ... }
#endif
```

The file is always in the build, so the guard is visible where the test is, and deleting or mis-spelling a variant name cannot silently remove a case. `tests/layout.cpp` uses the same style for its expectations, choosing values with `STRIPSEQ_WHITE_CHANNEL ? 8 : 6` rather than splitting into two files.

---

## 7. Exercises

Four, all on the host.

```sh
./scripts/build.sh
```

### Exercise 1 — See the five, and what each one carries

*Demonstrates §3 and §4.*

```sh
ctest --test-dir build -N | grep -oE '#[0-9]+: [a-z_]+:' | sed 's/.*: //' | sort | uniq -c
```

```
  … default:
  … nogamma:
  … rgbw:
  … rgbw_nogamma:
    2 spdx:
  … tight:
    2 verbatim:
```

Each count is the number of test cases compiled into that configuration, and every case runs once in every configuration it compiles in. The counts are not all equal, and they differ by exactly the cases §6 describes: the two RGBW builds have one more, about the white channel, and `tight` has two fewer, which need three segments where it holds two. `spdx:` and `verbatim:` are not variants: they are checks on the repository's sources, registered once each.

**Proves:** one source tree, one `ctest` invocation, five independently compiled configurations — and each case is reported under the configuration it ran in, not pooled.

### Exercise 2 — Add a variant

*Demonstrates §3: a combination is one line.*

Add to the root `CMakeLists.txt`, after the other five:

```cmake
stripseq_add_variant(fat STRIPSEQ_MAX_STEPS=64 STRIPSEQ_MAX_SEGMENTS=16)
```

```sh
./scripts/build.sh && ./scripts/test.sh build -R '^fat:'
```

Every case passes once more, under `fat:`, and `sizeof(Sequence)` in that binary is 788 bytes in an RGB build against the default build's 116 — 56 more steps of 12 bytes each, all of it capacity that no recipe uses. **Revert it** — a sixth variant costs a compile on every build and this one buys nothing the other five do not cover.

**Proves:** the helper is the unit of configuration. Nothing inside it knows how many variants exist, and nothing outside it needs to know how a variant is built.

### Exercise 3 — Watch a default stop being tested

*Demonstrates §4: why `default` passes nothing, and what checks the value it compiles.*

Edit `include/stripseq/config.h` so the declared default becomes `#define STRIPSEQ_WHITE_CHANNEL 1` — a plausible typo. Build with ninja's `-k 0`, so that one failure does not stop the others from being reported:

```sh
cmake --build build -- -k 0 2>&1 | grep -E 'FAILED|error:'
```

```
FAILED: … CMakeFiles/tests_default.dir/tests/layout.cpp.o
tests/layout.cpp:54:15: error: static assertion failed due to requirement '1 == 0': config.h's default changed. If that is intended, change it here too.
```

and the same for `tests_nogamma` and `tests_tight`, the three variants that leave the white channel unset. The two RGBW variants set it themselves, so the typo does not reach them.

Now see what the check is for. In `tests/layout.cpp`, comment out the four `#ifndef STRIPSEQ_EXPECT_…` blocks after `STRIPSEQ_DEFAULT_MSG`, and keep the typo:

```sh
./scripts/build.sh && ./scripts/test.sh
```

```
100% tests passed out of …
```

Every variant is green. The `default` variant is now an RGBW build, every test in it agrees with that, and the library's declared default is wrong for every consumer who does not override it.

Last, put the four blocks back and change the first call to `stripseq_add_variant(default STRIPSEQ_WHITE_CHANNEL=0 STRIPSEQ_GAMMA=1)`. Build with `-k 0` again: `tests_default` compiles now, and only `tests_nogamma` and `tests_tight` still fail. The variant named for the defaults has stopped testing them, and the typo is caught only because two other variants happen to leave the white channel alone. **Restore all three files.**

**Proves:** a default is tested by *not* setting it, and checked only by a copy of its value held outside the code under test. Compiled alone, a mistyped default is self-consistent and passes everything; an explicit copy in the build system takes even the compiling away from the variant meant to do it.

### Exercise 4 — Confirm the capacity variant earns its place

*Demonstrates §4: sampling the size knobs.*

In `tests/sequence.cpp`, in the case "TooManySteps, counted against this build's capacity", replace the loop bound `i < STRIPSEQ_MAX_STEPS` with a hardcoded `i < 8` — exactly the kind of assumption that creeps into tests written against the defaults.

```sh
./scripts/build.sh && ./scripts/test.sh
```

```
99% tests passed, 1 tests failed out of …
	… - tight:TooManySteps, counted against this build's capacity (Failed)
```

Four variants pass. `tight:` fails, because a build configured for six steps refuses the seventh, and the test expected all eight to fit. **Revert it.**

**Proves:** one variant at awkward capacities catches the whole class of "assumes the default" mistakes, without a cross product over values that do not change meaning.

---

## 8. The model in one paragraph

Build-time configuration comes in two kinds, and conflating them is what makes builds subtly wrong. A knob that changes a *value* — a capacity, a limit — can be compiled once and sampled, because translation units that disagree about it still agree about what the types and functions mean. A knob that changes a *type or the code itself* cannot be shared at all: the two worlds link cleanly and corrupt each other, so the build must produce one library per combination, with the tests compiled into each because they see the headers too. Given that, the build becomes a small function called once per configuration, cross-producting the knobs that change meaning and sampling the ones that change size, with the *declared defaults* exercised by a call that passes nothing so a mistyped default cannot hide behind an explicit copy of itself. Finally, none of those targets is the product: consumers link a separate, definition-free canonical target, because a variant's configuration would otherwise propagate into their build and pin a choice that is theirs to make.

## 9. Where to go next

- **[`cmake-usage-requirements-guide.md`](cmake-usage-requirements-guide.md) §6–§7** — the failure mode this whole arrangement is defending against, and why it needs a check whose expectation comes from outside the mechanism.
- **[`testing-guide.md`](testing-guide.md) §5** — the direct cost of §3: the suite compiles once per variant, and what that means for choosing a framework.
- **`include/stripseq/config.h`** — the reference half of §2: why each of the four knobs is a macro, including the two that had a genuine choice.
- **The root `CMakeLists.txt` and `zephyr/CMakeLists.txt`** — the five `stripseq_add_variant` calls with the reason there are five, and the Zephyr module that turns `CONFIG_STRIPSEQ_*` into the same `-D` flags.
