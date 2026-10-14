# Usage requirements: what `PUBLIC` actually means

*A from-first-principles guide to the CMake keyword that decides what your build tells the builds that depend on it.*

This is a teaching document, not project documentation. It explains what a CMake target really is, what the three keywords `PRIVATE`, `PUBLIC` and `INTERFACE` control, and why one of them causes a bug that neither the compiler nor the linker can see. It uses this repository — a small C++ library built five times over — as its running example, but nothing here is specific to it: the same rules govern every CMake project.

**The shape of this document:**

- **§1–§2** — the two ideas everything follows from: a build is a graph, and the keywords are edges on it rather than access control.
- **§3–§5** — what actually propagates, why the C++ standard travels as a *requirement* rather than a flag, and why warnings must not travel at all.
- **§6–§7** — the failure with no diagnostic: a definition that changes a type's layout, and the second path this repo uses to catch it.
- **§8** — four exercises, two of which break the build on purpose.
- **§9–§10** — the model in a paragraph, and where to go next.

---

## 1. The mental model: a build is a graph of targets

The first thing to unlearn is the idea that a CMake file is a script that compiles files. It is a *description of targets* — libraries and executables — and of the edges between them. `add_library(stripseq STATIC ...)` does not compile anything; it declares a node. The compiler command line is generated much later, by walking the graph.

That distinction matters because most of what a target carries is not for the target itself. When something links `stripseq`, that consumer needs to know where the headers are, what C++ standard they need, and which preprocessor definitions they were compiled under. None of that is discoverable from the archive file. So a target carries it, and CMake calls it a **usage requirement**: a property that travels along an edge to whoever links you.

Everything in this guide is one question asked repeatedly: *does this property stop at the target, or travel to its consumers?*

## 2. The three keywords are not access control

`PRIVATE` and `PUBLIC` are borrowed words, and the borrowing is unfortunate — they have nothing to do with encapsulation. They name the two halves of every target-level property, and the third keyword names the case where only one half is used:

| Keyword | Applies when building this target | Applies to consumers |
| --- | --- | --- |
| `PRIVATE` | yes | no |
| `PUBLIC` | yes | yes |
| `INTERFACE` | no | yes |

`INTERFACE` looks exotic until you meet a header-only library, which is exactly the case it exists for: there is nothing to compile, so nothing applies "when building this target", but consumers still need the include directory. `doctest::doctest` is an `INTERFACE` target of precisely this kind — see [`dependencies-guide.md`](dependencies-guide.md) §2.

The useful way to read a line is as a sentence about reach. `target_include_directories(stripseq PUBLIC include/)` says *"I compile with this on my include path, and so does anyone who links me."* `target_compile_options(stripseq PRIVATE -Wall)` says *"I compile with this, and it is nobody else's business."*

## 3. What propagates

Four properties carry the keywords, and they are the four that matter:

- **Include directories** — `target_include_directories`. Almost always `PUBLIC` for a library with headers: consumers compile those headers, so they need the path. This repo's `include/` is the prefixed directory that makes consumers write `#include <stripseq/strip.h>`.
- **Compile definitions** — `target_compile_definitions`. This is the interesting one, because a definition can change what the headers *mean*. §6 is entirely about that.
- **Compile features** — `target_compile_features`. A language-level requirement rather than a flag; §4.
- **Linked libraries** — `target_link_libraries`. `PUBLIC` here means "and my consumers link it too", which is how a dependency of a dependency reaches the final executable without anyone naming it twice.

**Compile options** — `target_compile_options` — take the keywords as well, and are the one property that should almost always be `PRIVATE`; §5.

## 4. `cxx_std_20` is a requirement, not a flag

There are two ways to ask for C++20, and only one of them composes:

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")   # a flag: last writer wins
target_compile_features(stripseq PUBLIC cxx_std_20)    # a requirement: CMake reconciles
```

The first is a string appended to a command line. If a consumer already asked for C++23, the two now fight, and which wins depends on argument order — a coin flip decided by the generator. The second states a *minimum*: CMake takes the maximum of everything requested across the graph and emits one `-std=` flag. A consumer on C++23 stays on C++23 and this library still compiles.

`PUBLIC` is right here for a reason worth saying out loud: the requirement is a property of the **headers**, not of the `.cpp` files. `SequenceBuilder::build_const()` is `consteval`, so any translation unit that includes `stripseq/sequence.h` needs C++20 whether or not it ever links the archive.

This is also the *only* thing this library says about the toolchain. That is a project ground rule rather than a CMake rule: Zephyr owns the compiler, the standard library, the optimisation level and the warning set for anything built inside it, so the library sets no `-O`, no `-fno-exceptions`, no `CMAKE_CXX_FLAGS`. Where those flags legitimately live is the subject of [`cross-compiling-guide.md`](cross-compiling-guide.md) §3.

## 5. Warnings are `PRIVATE`, and so is everything opinionated

A warning set is a statement about how *you* want to be told about your own code. Propagating it means a consumer who linked your library suddenly gets `-Wshadow` diagnostics from their own source, which they did not ask for and cannot easily remove.

This repo goes one step further and puts its warnings behind `PROJECT_IS_TOP_LEVEL`, a variable that is true only when this project is the one being built directly, rather than pulled in by someone else's `add_subdirectory`:

```cmake
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(STRIPSEQ_WARNINGS -Wall -Wextra -Wpedantic -Wshadow)
    target_compile_options(stripseq PRIVATE ${STRIPSEQ_WARNINGS})
endif()
```

Two mechanisms, two different jobs, and it is worth keeping them apart: `PRIVATE` stops the flags travelling *along an edge*, and `PROJECT_IS_TOP_LEVEL` stops the whole opinion existing when we are not the main project. The same guard is what makes `STRIPSEQ_BUILD_TESTS` default to off for a consumer, so nobody builds our test suite by accident.

## 6. The failure with no diagnostic

Now the case this whole guide exists for.

This library has a build-time macro, `STRIPSEQ_WHITE_CHANNEL`, that adds a fourth channel to `Color`:

```cpp
struct Color {
    uint16_t r, g, b;
#if STRIPSEQ_WHITE_CHANNEL
    uint16_t w;
#endif
    static constexpr Color rgb(uint16_t r, uint16_t g, uint16_t b);
};
```

`sizeof(Color)` is 6 or 8 depending on a `-D` on the command line. Suppose the definition is passed `PRIVATE` to the library and the tests are compiled without it. Then the library was built believing `Color` is eight bytes and the tests believe it is six. Ask yourself which tool notices.

The compiler cannot: it sees one consistent world per translation unit, and each of them *is* internally consistent. The linker cannot either, and the reason is specific. C++ name mangling encodes a function's *signature* — its name, its parameter types, its class — but nothing about the layout of those types. `Color::rgb(uint16_t, uint16_t, uint16_t)` mangles to the same string whether or not the class carries a fourth member, so the reference and the definition match and the link succeeds.

You can watch this directly. The `default` and `rgbw` builds of this library disagree about the size of nearly every type in it, and export **identical** symbol lists — 24 names, byte-for-byte the same (§8, Exercise 2).

What you get is a program in which one half writes eight-byte colors into an array the other half indexes by six, and the symptom is corruption somewhere unrelated to the mistake. That is why the one-word fix — `PUBLIC` rather than `PRIVATE` — is worth an entire guide:

```cmake
target_compile_definitions(${lib} PUBLIC ${ARGN})
```

## 7. Catching it anyway: a second, independent path

`PUBLIC` is correct, but "correct and unchecked" is a poor place to leave something with no diagnostic. The obvious test does not work: if the tests assert `sizeof(Color) == 6` in a build where the macro never reached them, they are *self-consistently wrong* and pass.

Catching it needs a second source of truth that does not travel the same edge. This repo's variant helper hands the test target the same definitions again, renamed, straight onto the executable:

```cmake
set(expect "")
foreach(def IN LISTS ARGN)
    string(REPLACE "STRIPSEQ_" "STRIPSEQ_EXPECT_" renamed "${def}")
    list(APPEND expect ${renamed})
endforeach()
target_compile_definitions(${exe} PRIVATE ${expect})
```

and `tests/layout.cpp` compares the two:

```cpp
#ifdef STRIPSEQ_EXPECT_WHITE_CHANNEL
static_assert(STRIPSEQ_WHITE_CHANNEL == STRIPSEQ_EXPECT_WHITE_CHANNEL, STRIPSEQ_PROPAGATION_MSG);
#endif
```

The library gets its copy through the propagation being tested; the tests get theirs through a path that bypasses it. If propagation breaks, the two disagree and the build stops with a message naming the file and the fix. The `#ifdef` guard is what lets the `default` variant — which passes no definitions at all — skip the check honestly rather than assert against a value nobody supplied.

The general shape is worth keeping: **to test a mechanism, get your expectation from somewhere the mechanism cannot reach.**

---

## 8. Exercises

Four, all on the host, seconds each. Two work by breaking the build on purpose; each says what to revert.

```sh
./scripts/build.sh          # into ./build
```

### Exercise 1 — Read the propagation off the generated build

*Demonstrates §2 and §3: usage requirements are resolved into ordinary command-line flags.*

```sh
ninja -C build -t commands stripseq_rgbw | grep -oE '\-DSTRIPSEQ_[A-Z_]+=[0-9]+' | sort -u
ninja -C build -t commands tests_rgbw    | grep -oE '\-DSTRIPSEQ_[A-Z_]*[A-Z_]+=[0-9]+' | sort -u
```

```
-DSTRIPSEQ_WHITE_CHANNEL=1

-DSTRIPSEQ_EXPECT_WHITE_CHANNEL=1
-DSTRIPSEQ_WHITE_CHANNEL=1
```

The library was given one definition. The test executable was never given `STRIPSEQ_WHITE_CHANNEL` directly — it arrived by propagation, which is the edge in §2 made visible. `STRIPSEQ_EXPECT_WHITE_CHANNEL` is the second path from §7. Now count the same flags on the `default` variant:

```sh
ninja -C build -t commands stripseq_default | grep -c 'DSTRIPSEQ_'
```

```
0
```

**Proves:** the `default` variant compiles with no definitions at all, so `config.h`'s `#ifndef` values are exercised *as* defaults rather than being shadowed by an explicit copy of themselves.

### Exercise 2 — Prove the linker cannot see the difference

*Demonstrates §6: mangling encodes signatures, not layouts.*

```sh
for v in default rgbw; do
  nm -C build/libstripseq_$v.a | grep ' T ' | sed 's/^[0-9a-f]* //' | sort > /tmp/sym_$v.txt
done
diff /tmp/sym_default.txt /tmp/sym_rgbw.txt && echo "identical: $(wc -l < /tmp/sym_rgbw.txt) symbols"
```

```
identical:       24 symbols
```

Two libraries that disagree about the size of `Color`, `Step`, `Sample`, `Sequence` and `Segment` export exactly the same 24 names.

**Proves:** there is no link-time check to fall back on. If the definitions do not reach both sides, nothing downstream will tell you — which is the entire justification for §7.

### Exercise 3 — Break `PUBLIC` and read the error

*Demonstrates §7: the second path catches what §6 says nothing else can.*

In `cmake/stripseq_variant.cmake`, change `target_compile_definitions(${lib} PUBLIC ${ARGN})` to `PRIVATE`:

```sh
cmake --build build --target tests_rgbw
```

```
tests/layout.cpp:32:15: error: static assertion failed due to requirement '0 == 1':
The variant's definitions did not reach the tests. Check that
target_compile_definitions in cmake/stripseq_variant.cmake is PUBLIC.
```

**Revert it.** Note what the diagnostic is: not a link error, not a crash, but a `static_assert` this repo wrote on purpose. Nothing in the toolchain produced it.

**Proves:** the mistake is one word, it is silent by default, and a deliberate check turns it into a compile error that names its own fix.

### Exercise 4 — Delete the witness and watch the bug go quiet

*Demonstrates §7: why the expectation has to come from outside the mechanism.*

Leave `PRIVATE` in place from Exercise 3, and also comment out `target_compile_definitions(${exe} PRIVATE ${expect})` in the same file.

```sh
cmake --build build --target tests_rgbw && ./build/tests_rgbw
```

```
[doctest] test cases:  4 |  4 passed | 0 failed
```

Four green tests, including `color and step layout follow the white-channel setting` — in a binary whose library thinks `Color` is eight bytes and whose tests think it is six. **Revert both changes** and rebuild.

**Proves:** a test that derives its expectation from the same macros it is checking cannot fail. Self-consistency is not verification.

---

## 9. The model in one paragraph

A CMake build is a graph of targets, and most of what a target carries is aimed at whoever links it: include directories, compile definitions, language requirements, and its own dependencies. `PRIVATE`, `PUBLIC` and `INTERFACE` are not access control but reach — build-me, build-me-and-my-consumers, consumers-only — and choosing correctly is mostly a question of whether the property is visible in the headers. Include directories and anything that changes what a header *means* are `PUBLIC`; the C++ standard travels as a *feature* rather than a flag so consumers can reconcile it upward; warnings and optimisation are `PRIVATE` and ideally guarded by `PROJECT_IS_TOP_LEVEL`, because they are opinions about your own code. The keyword matters most where getting it wrong is silent: a definition that changes a type's layout produces two internally consistent halves whose symbols mangle identically, so neither the compiler nor the linker can object, and the only defence is a check whose expectation reaches it by a path the broken mechanism does not control.

## 10. Where to go next

- **[`build-variants-guide.md`](build-variants-guide.md)** — the direct sequel: given that a layout-changing macro cannot be shared, what does a build that supports several of them look like? It is where the `stripseq_add_variant` helper quoted above is explained end to end.
- **[`dependencies-guide.md`](dependencies-guide.md)** — the same graph, extended to targets you did not write: imported targets, `INTERFACE` libraries, and where `doctest::doctest` comes from.
- **[`cross-compiling-guide.md`](cross-compiling-guide.md) §3** — the other half of §4's rule: if the library sets no flags, who does?
- **`cmake/stripseq_variant.cmake` and the root `CMakeLists.txt`** — thirty lines each, and after this guide they should read as prose.
- **CMake's own documentation on [`cmake-buildsystem(7)`](https://cmake.org/cmake/help/latest/manual/cmake-buildsystem.7.html)** — the reference half of §3, with the full list of properties that carry usage requirements.
