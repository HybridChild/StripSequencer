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

A template parameter can change a value, an algorithm, even a base class — but it cannot add a member to a type that other, non-template types embed by value. Making `Color<W>` a template means `Step<W>`, `Sequence<W>`, `Sample<W>`, `Segment<W>` and `Strip<W>` all follow, because each holds a `Color` directly, and then no two of them can be mixed and the parameter has infected the entire public surface. The macro puts the same decision in one place at the cost of one build per setting — which is what the rest of this guide is about. The longer argument, including why the two *capacity* macros are macros as well when they did not have to be, is in `docs/API-SKETCH.md`, "Build-time configuration".

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
stripseq_add_variant(tight         STRIPSEQ_MAX_STEPS=3 STRIPSEQ_MAX_SEGMENTS=1)
```

**Each call passes only its deviations from the defaults, and `default` passes nothing at all.** That is deliberate and easy to get wrong in the other direction. Writing `stripseq_add_variant(default STRIPSEQ_WHITE_CHANNEL=0 STRIPSEQ_GAMMA=1)` looks more explicit and is strictly worse: the values in `config.h` would then never be compiled by anything, and a typo turning the declared default white channel on would pass the entire suite, because every variant overrode it. A default is only tested by *not* setting it.

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
   4 default:
   4 nogamma:
   4 rgbw:
   4 rgbw_nogamma:
   4 tight:
```

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

Four more passing cases, and `sizeof(Sequence)` in that binary is 788 bytes rather than the default build's 116. **Revert it** — a sixth variant costs a compile on every build and this one buys nothing the other five do not cover.

**Proves:** the helper is the unit of configuration. Nothing inside it knows how many variants exist, and nothing outside it needs to know how a variant is built.

### Exercise 3 — Watch a default stop being tested

*Demonstrates §4: why `default` passes nothing.*

Change the first call to `stripseq_add_variant(default STRIPSEQ_WHITE_CHANNEL=0 STRIPSEQ_GAMMA=1)`, then edit `include/stripseq/config.h` so the declared default becomes `#define STRIPSEQ_WHITE_CHANNEL 1` — a plausible typo.

```sh
./scripts/build.sh && ./scripts/test.sh
```

```
100% tests passed out of 20
```

Every variant is green, and the library's declared default is now wrong for every consumer who does not override it. Restore both, rebuild, and watch `default:` fail as it should have all along.

**Proves:** a default is tested by *not* setting it. An explicit copy of a default in the build system hides the declaration it duplicates.

### Exercise 4 — Confirm the capacity variant earns its place

*Demonstrates §4: sampling the size knobs.*

In `tests/layout.cpp`, replace `CHECK(sizeof(Sequence) >= sizeof(Step) * STRIPSEQ_MAX_STEPS);` with a hardcoded `CHECK(sizeof(Sequence) >= sizeof(Step) * 8);` — exactly the kind of assumption that creeps into tests written against the defaults.

```sh
./scripts/build.sh && ./scripts/test.sh
```

Four variants pass. `tight:` fails, because there is no room for eight steps in a build configured for three. **Revert it.**

**Proves:** one variant at awkward capacities catches the whole class of "assumes the default" mistakes, without a cross product over values that do not change meaning.

---

## 8. The model in one paragraph

Build-time configuration comes in two kinds, and conflating them is what makes builds subtly wrong. A knob that changes a *value* — a capacity, a limit — can be compiled once and sampled, because translation units that disagree about it still agree about what the types and functions mean. A knob that changes a *type or the code itself* cannot be shared at all: the two worlds link cleanly and corrupt each other, so the build must produce one library per combination, with the tests compiled into each because they see the headers too. Given that, the build becomes a small function called once per configuration, cross-producting the knobs that change meaning and sampling the ones that change size, with the *declared defaults* exercised by a call that passes nothing so a mistyped default cannot hide behind an explicit copy of itself. Finally, none of those targets is the product: consumers link a separate, definition-free canonical target, because a variant's configuration would otherwise propagate into their build and pin a choice that is theirs to make.

## 9. Where to go next

- **[`cmake-usage-requirements-guide.md`](cmake-usage-requirements-guide.md) §6–§7** — the failure mode this whole arrangement is defending against, and why it needs a check whose expectation comes from outside the mechanism.
- **[`testing-guide.md`](testing-guide.md) §5** — the direct cost of §3: the suite compiles once per variant, and what that means for choosing a framework.
- **`docs/API-SKETCH.md`, "Build-time configuration"** — the reference half of §2: why each of the four knobs is a macro, including the two that had a genuine choice.
- **`docs/IMPLEMENTATION-PLAN.md`, Phase 0** — where the five calls were decided, and the Zephyr module in Phase 6 that turns `CONFIG_STRIPSEQ_*` into the same `-D` flags.
