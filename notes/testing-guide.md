# One binary, a result for every case

*A from-first-principles guide to how a test framework and a test runner divide the work, using doctest and CTest.*

This is a teaching document, not project documentation. It explains what a C++ test framework actually does, why CTest is a separate layer that knows nothing about your framework, and what test *discovery* buys over the naive wiring. The running example is this repository's suite: one set of test cases compiled into five differently configured binaries, and reported by CTest as one independent result per case per configuration. It assumes [`build-variants-guide.md`](build-variants-guide.md) for where the five come from.

**The shape of this document:**

- **§1–§2** — what a framework provides, and the one mechanism behind `TEST_CASE`.
- **§3–§4** — CTest as a separate layer, and what `doctest_discover_tests` generates on your behalf.
- **§5** — the cost this repo pays: the suite is compiled once per configuration, and why that drives the choice of framework.
- **§6** — the one-line translation unit, and the honest limit of what it proves.
- **§7** — tests that must *not* compile, which no test framework can run, and what `try_compile` does instead.
- **§8** — seven exercises.
- **§9–§10** — the model in a paragraph, and where to go next.

---

## 1. What a test framework is for

Strip away the marketing and a C++ test framework does three things:

1. **Collect** test cases without you maintaining a list of them.
2. **Run** them, catching a failure in one without abandoning the rest.
3. **Report** what failed, with the values involved rather than just "assertion failed".

Only the first is interesting, because the other two are ordinary code. Collection is what makes `TEST_CASE("...") { ... }` work with no registration call anywhere.

doctest is the framework here for reasons that are mostly about §5, but one is immediate: it is a single header. There is no library to build, no `find_package` dance in the common case, and adding it to a target is one `target_link_libraries` line against an `INTERFACE` target that carries an include directory and nothing else (see [`dependencies-guide.md`](dependencies-guide.md) §2).

## 2. How `TEST_CASE` collects itself

The mechanism is worth knowing because it explains both the convenience and one of its costs.

`TEST_CASE("name") { body }` expands to roughly: a free function containing the body, plus a namespace-scope object whose constructor registers a pointer to that function in a global list. Namespace-scope objects are constructed before `main` runs, so by the time doctest's `main` starts, every case in every translation unit has already put itself on the list. Nobody maintains an index, and adding a file to the build is all it takes to add its tests.

That is also, precisely, the "dynamic initialisation at file scope" that this library forbids in its own code (see [`cross-compiling-guide.md`](cross-compiling-guide.md) §5). The rule is about the *library*, which runs on a microcontroller with no startup code to spare; the tests run on a host under a real OS, where pre-`main` registration is exactly the right trade. Knowing *why* a rule exists is what lets you see where it stops applying.

The `main` itself is one macro in one translation unit:

```cpp
// tests/doctest_main.cpp -- the whole file
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
```

It is a separate file on purpose: that macro makes the header emit its implementation, which is slow to compile, and putting it in its own TU means editing a test file does not recompile the framework.

## 3. CTest is a different layer

Here is the part that confuses people who meet CTest through a framework: **CTest does not know what doctest is.** It has no notion of test cases, assertions or C++. Its model is far simpler:

> A test is a command line. It passes if the process exits 0.

That is the whole contract. `add_test(NAME foo COMMAND some_program --flag)` registers one, and `ctest` runs them, in parallel if asked, and reports pass or fail per command.

So there are two obvious ways to connect the layers, and the difference matters:

**One test per binary.** `add_test(NAME tests_rgbw COMMAND tests_rgbw)` — the binary runs all its cases and exits non-zero if any failed. Simple, and this repo would report *five* results, one per binary, however many cases each holds: a failure tells you the `rgbw` binary failed, and you read its output to find out which case. Under `ctest -j` you get five parallel jobs at most, each internally serial.

**One test per case.** Register every case as its own command line, invoking the binary with a filter that selects exactly that case. Five times as many results as there are cases, as many parallel jobs, and a failure names itself. The cost is that somebody has to enumerate the cases — which is what discovery does.

## 4. What `doctest_discover_tests` actually does

The trick is that the binary can enumerate itself. Every framework of this kind has a listing flag; doctest's is `--list-test-cases`. So discovery is:

1. At **build** time, immediately after linking, run the freshly built binary with `--list-test-cases`.
2. Turn each line into an `add_test` call, in a generated `.cmake` file.
3. Have CTest include that file when it loads the directory's tests.

You can read the output. It is a real file in the build tree:

```cmake
add_test( [==[rgbw:color and step layout follow the white-channel setting]==]
          .../build/tests_rgbw
          [==[--test-case=color and step layout follow the white-channel setting]==])
```

That is the whole magic: an ordinary `add_test`, with an ordinary command line, generated rather than typed. The `TEST_PREFIX "${name}:"` this repo passes is prepended to the CTest name only — the `--test-case=` filter still carries the case's real name, because that is what the binary understands.

Two consequences follow from *when* it runs. Because listing happens at build time, a newly added test case appears in `ctest` only after a build — not after a mere reconfigure. And because it runs the binary, discovery needs a binary that can run on the build machine: a cross-compiled test suite cannot be discovered this way, which is one more reason this project's tests are host tests (`docs/IMPLEMENTATION-PLAN.md` makes that argument for the library as a whole).

## 5. Why the suite is compiled five times

The tests include the library's headers. The headers are what `STRIPSEQ_WHITE_CHANNEL` changes. Therefore there is no such thing as a configuration-neutral test binary: a test compiled against a six-byte `Color` cannot meaningfully exercise a library built with an eight-byte one — and, as [`cmake-usage-requirements-guide.md`](cmake-usage-requirements-guide.md) §6 shows, it will link cleanly anyway and lie to you.

So the cost is inherent: five configurations means five compiles of every test file. It cannot be shared, only made cheap — and that is what drives the framework choice. A header-only framework with no separate build and a fast-compiling header is worth more here than a feature-rich one, because its compile time is multiplied by the number of variants and paid on every incremental build. This is a specific instance of a general point: **the right tool depends on the shape of the build, not only on the tool's features.**

## 6. The one-line translation unit, and what it does not prove

`tests/umbrella_only.cpp` is a single `#include <stripseq/stripseq.h>` and nothing else. It exists because a header that silently depends on something else having been included first is a real and common bug: it works everywhere in your project and breaks in the first consumer who includes it on a clean page.

Be precise about what it establishes, though. It proves the **umbrella** header is self-sufficient — that a consumer who includes only `stripseq/stripseq.h` gets a working set, in all five configurations. It does *not* prove each individual header stands alone: `sequence.h` could rely on `color.h` having been pulled in first, and the umbrella would still compile because it includes both. Proving the stronger property needs one such TU per header, generated or written out.

That is a deliberate stopping point rather than an oversight — but it is the kind of stopping point worth writing down, because "we have a header self-containment test" invites people to assume the stronger claim.

## 7. Tests that must not compile

Everything above tests what code does when it runs. Some promises are about what the compiler *refuses*. `SequenceBuilder::build_const()` promises that an invalid sequence written in source is a compile error, naming the mistake — see [`constant-initialisation-guide.md`](constant-initialisation-guide.md) §4 for how. That promise has no run-time behaviour at all, and every tool so far is the wrong shape for it:

- **A test case** lives in a file that has to compile. A file containing the invalid sequence does not, so it cannot also be the file that checks the result.
- **`static_assert`** asserts that a constant expression is true. The failure being tested *is* the expression not being a constant, which is a compile error, not a `false`.
- **A `requires`-expression** looks like the answer and is not. It checks that an expression is well-formed, and `SequenceBuilder{}.fade_to(colors::red, 0).build_const()` is perfectly well-formed: it fails only when evaluated, and a `requires`-expression never evaluates. Both Clang and GCC report it as satisfied.

So the test has to be a whole compiler run, observed from outside — and the thing that runs compilers is the build system. CMake's `try_compile` compiles a source file at configure time and reports two things: whether it compiled, and everything the compiler printed. `cmake/stripseq_compile_fail.cmake` uses it on one probe file, `tests/compile_fail/build_const.cpp`, which selects one sequence per case with `#if STRIPSEQ_PROBE == n`, and compiles it nine times.

Four decisions in that file are the general lessons.

**Failing is not enough; name the failure.** A probe that fails to compile proves nothing on its own, because it might have failed for an unrelated reason — a typo, a missing include, a header that moved, or a *different* rule firing first. So each case carries the name of the failure function its sequence should reach, and the compiler's output must contain that name. Because `build_const()` names each error after itself, this checks *which* error, not merely *an* error. §8 Exercise 5 shows the difference: with one rule removed, the probe for it still fails to compile — because another rule caught the same sequence — and only the name check notices.

**A positive control.** Case 0 is a valid sequence, through the same probe file with the same flags, and it must compile. Without it, a probe broken for every case — a wrong include path, a renamed header — would make every negative case pass, because every one of them would fail to compile.

**`try_compile`, never `check_cxx_source_compiles`.** CMake's `check_*()` modules look like the obvious tool, and they are built for feature detection: whether the compiler supports a flag does not change between configures, so once a result is in the cache they skip the check and report the cached answer. For a test that is fatal — it would pass once per build tree and report that first answer forever, whatever the headers became (§8 Exercise 7). `try_compile` itself always compiles. It does store its result as a cache entry — CMake 3.25 added `NO_CACHE` to stop that, and this project's minimum is 3.21 — but it never reads one back.

**Make the build reconfigure when the probe's inputs change.** A configure-time check runs when CMake configures, and editing a header is not something CMake configures for: it tracks the headers each object file includes, so `cmake --build` after an edit to `sequence.h` recompiles every test, and runs no probe. A mistake that only the probes can see — a failure branch that no longer calls its function — would survive an edit, a build and a full `ctest` run. The fix is the directory property `CMAKE_CONFIGURE_DEPENDS`: every file listed there is a dependency of the build system itself, so a change to one makes the next build start with *Re-running CMake*. The check lists the probe and every header it reaches, which costs nine small compiles on any build after one of them changes. The list is maintained by hand, so a new `#include` in `sequence.h` has to be added to it (§8 Exercise 6).

---

## 8. Exercises

Seven, all on the host.

```sh
./scripts/build.sh
```

### Exercise 1 — Ask the binary and the runner the same question

*Demonstrates §3–§4: two layers, two vocabularies.*

```sh
./build/tests_rgbw --list-test-cases
ctest --test-dir build -N -R '^rgbw:'
```

The binary lists its cases by their real names and knows nothing about CTest. CTest lists the same number of *tests*, each named `rgbw:` plus a case's name, and knows nothing about doctest — only that many command lines it can run. The last line of each listing is the count; they match, and `ctest -N` with no filter shows five times as many.

**Proves:** the framework collects and runs cases; the runner runs processes. Discovery is the translation between them, and neither side depends on the other's existence.

### Exercise 2 — Read the generated wiring

*Demonstrates §4: discovery is code generation, not magic.*

```sh
grep -m1 'white-channel' build/tests_rgbw_tests-*.cmake
```

```
add_test( [==[rgbw:color and step layout follow the white-channel setting]==] .../tests_rgbw [==[--test-case=color and step layout follow the white-channel setting]==])
```

**Proves:** every discovered test is a plain `add_test` with a filter flag, written by a script that ran your binary with `--list-test-cases` after linking it. You could have typed these by hand; you would have to retype them whenever a case is renamed.

### Exercise 3 — Fail one configuration and see who reports it

*Demonstrates §3–§4 and [`build-variants-guide.md`](build-variants-guide.md) §3: per-case, per-variant reporting.*

In `tests/layout.cpp`, replace the `bytes_per_pixel` check with a value that is only right for RGB:

```cpp
CHECK(Strip::bytes_per_pixel == 3u);
```

```sh
./scripts/build.sh && ./scripts/test.sh build -R 'white-channel'
```

The `-R` filter, passed through to `ctest`, narrows the run to the one case's five results; without it the same two failures appear among every case in every configuration.

```
1/5 Test #…: rgbw:color and step layout follow the white-channel setting ...........***Failed
2/5 Test #…: nogamma:color and step layout follow the white-channel setting ........   Passed
3/5 Test #…: rgbw_nogamma:color and step layout follow the white-channel setting ...***Failed
4/5 Test #…: default:color and step layout follow the white-channel setting ........   Passed
5/5 Test #…: tight:color and step layout follow the white-channel setting ..........   Passed
60% tests passed, 2 tests failed out of 5
```

The test numbers depend on how many cases the suite holds, and the order on `-j`.

**Revert it.** Note what the summary gives you without opening any log: which case, and that it is exactly the two RGBW configurations — so the bug is about the white channel and not about gamma or capacity.

**Proves:** the prefix turns a configuration-dependent failure into a diagnosis. With one test per binary you would have seen "2 of 5 binaries failed" and gone reading output to learn the same thing.

### Exercise 4 — Break a header's self-sufficiency

*Demonstrates §6.*

Delete `#include <cstdint>` from the top of `include/stripseq/color.h`.

```sh
cmake --build build --target tests_default
```

```
include/stripseq/color.h:26:5: error: unknown type name 'uint16_t'
```

**Revert it.** Then try the weaker version of the same mistake: delete `#include "stripseq/config.h"` from `sequence.h` instead. Everything still compiles — `sequence.h` includes `color.h`, which includes `config.h` — and the umbrella TU is happy. **Revert that too.**

**Proves:** §6's limit exactly. The umbrella catches a header that depends on nothing having been included; it cannot catch one that depends on a *sibling* having been included first, because it includes the sibling too.

### Exercise 5 — A probe that fails for the wrong reason

*Demonstrates §7: why each case names its failure.*

In `SequenceBuilder::fade_to()` in `include/stripseq/sequence.h`, disable the zero-duration rule by replacing `if (ms == 0 && t != Transition::Step)` with `if (false)`. Probe 3 is `fade_to(colors::red, 0)`, which that rule exists to refuse.

```sh
cmake -S . -B build
```

```
CMake Error at cmake/stripseq_compile_fail.cmake:86 (message):
  build_const() probe 3 failed to compile, but not by calling
  sequence_has_a_zero_duration_fade:
  ...
  note: non-constexpr function 'sequence_is_endless_but_its_loop_is_0_ms'
  cannot be used in a constant expression
```

**Revert it.** The probe *did* fail to compile. With the rule gone, the zero-length fade went into the sequence as an ordinary step, and the result — an endless sequence whose whole loop is 0 ms — was refused by a different rule. A check that asked only "did it fail?" would have passed with the rule deleted. (The ordinary build catches this particular mistake too, through a `static_assert` in `tests/sequence.cpp` that reads `build()`'s verdict; Exercise 6 is a mistake nothing else catches.)

**Proves:** a negative test needs a positive identification of *why* it failed, or it tests nothing in particular.

### Exercise 6 — A mistake that only the probes can see

*Demonstrates §7: what the check catches, and when it runs.*

In `SequenceBuilder::build_const()`, comment out the call in the `ZeroDurationFade` case, leaving the `break`:

```cpp
        case Error::ZeroDurationFade:
            // sequence_has_a_zero_duration_fade();
            break;
```

`build()` still reports the error; `build_const()` now ignores it and returns an empty sequence. Build as usual:

```sh
cmake --build build
```

```
[0/1] Re-running CMake...
CMake Error at cmake/stripseq_compile_fail.cmake:82 (message):
  build_const() accepted an invalid sequence (probe 3, expected
  sequence_has_a_zero_duration_fade).
```

The header edit made the build reconfigure, and the probes caught it. Now see what they are catching that nothing else does. **Revert the header**, then in `cmake/stripseq_compile_fail.cmake` comment out the `set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS …)` call, and build once so the change takes effect. Make the same header edit again:

```sh
cmake --build build && ./scripts/test.sh
```

```
100% tests passed out of 185
```

No reconfigure, no probe, and every test green. **Revert both files** and build again; the check reports that `build_const()` refuses every invalid sequence.

**Proves:** the compile-failure check is the only thing standing between this edit and a library that silently accepts a zero-duration fade at compile time, and a configure-time check is only as current as the last configure. The configure dependency is what ties it to the build.

### Exercise 7 — Watch a `check_*()` module report a stale answer

*Demonstrates §7: why the check is a bare `try_compile`.*

```sh
mkdir -p /tmp/cache-demo && cd /tmp/cache-demo
cat > CMakeLists.txt <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(cache_demo CXX)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("int main() { return VALUE; }" HAVE_VALUE)
message(STATUS "HAVE_VALUE=${HAVE_VALUE}")
EOF
cmake -S . -B b -DCMAKE_REQUIRED_DEFINITIONS=-DVALUE=0 | grep HAVE_VALUE
cmake -S . -B b -UCMAKE_REQUIRED_DEFINITIONS          | grep HAVE_VALUE
rm -rf b && cmake -S . -B b                           | grep HAVE_VALUE
```

```
-- Performing Test HAVE_VALUE
-- Performing Test HAVE_VALUE - Success
-- HAVE_VALUE=1
-- HAVE_VALUE=1
-- Performing Test HAVE_VALUE
-- Performing Test HAVE_VALUE - Failed
-- HAVE_VALUE=
```

The second configure removed the definition the snippet needs, and still reported success — without performing the test at all. Only a fresh build tree tells the truth.

**Proves:** the `check_*()` modules answer "has this ever compiled in this build tree?", which is the right question for a compiler feature and the wrong one for a test. `try_compile` answers "does this compile now?".

---

## 9. The model in one paragraph

A test framework and a test runner solve different problems, and the seam between them is where the useful choices live. The framework — doctest here, a single header — collects cases through namespace-scope objects that register themselves before `main`, runs them, and reports values rather than just failures. CTest knows none of that: to it a test is a command line that passes if the process exits zero, so the two layers must be joined either by one test per binary or by one test per case, and the second is worth having because it gives per-case names, real parallelism, and a failure that diagnoses itself. Discovery bridges them by running the freshly linked binary with `--list-test-cases` at build time and generating ordinary `add_test` calls with `--test-case=` filters — which means new cases appear only after a build, and a cross-compiled suite cannot be discovered at all. What that costs is set by the shape of the build rather than the framework: because tests include the headers that build-time macros change, the whole suite compiles once per configuration, and a header-only framework's compile speed is multiplied by the number of variants on every build. One kind of test sits outside both layers: a promise that some code *fails* to compile cannot live in a file that has to compile, so it becomes a compiler run that CMake's `try_compile` performs at configure time: each probe must fail, must fail by naming the expected error, and is paired with a control that must succeed — and the headers it compiles are configure dependencies, so a change to them makes the next build run it again.

## 10. Where to go next

- **[`build-variants-guide.md`](build-variants-guide.md)** — where the five binaries come from, and why the tests cannot be shared between them.
- **[`dependencies-guide.md`](dependencies-guide.md)** — how `doctest::doctest` reaches the build in the first place, and where `doctest_discover_tests` is defined.
- **`tests/layout.cpp`** — cases that check the build system rather than the code: that each variant's definitions reached the tests, and that a `constinit` strip links against the library.
- **`cmake/stripseq_compile_fail.cmake` and `tests/compile_fail/build_const.cpp`** — §7 in full: about eighty lines of CMake and one probe file with nine cases.
- **[`constant-initialisation-guide.md`](constant-initialisation-guide.md) §4** — what the probes are testing: how `build_const()` turns an invalid sequence into a compile error that names it.
- **`docs/IMPLEMENTATION-PLAN.md`, Phase 5** — where the suite becomes an acceptance test: the eleven cookbook recipes compiled unmodified, with property assertions and golden-frame hashes.
- **doctest's own `doctest.cmake`** — the ~200 lines behind §4, and short enough to read once to see that discovery really is just generated `add_test` calls.
