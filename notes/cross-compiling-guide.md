# Compiling for a machine you cannot run

*A from-first-principles guide to CMake toolchain files, freestanding C++, and what a compile-only check does and does not prove.*

This is a teaching document, not project documentation. It explains what changes when the machine you are compiling *for* is not the machine you are compiling *on*, how CMake is told about that, and what you can learn about embedded correctness from a build that never links and never runs. The running example is this repository's cross-compile check, which compiles the library for two bare-metal Cortex-M cores on every CI run and on demand via `scripts/cross-check.sh`. It assumes [`cmake-usage-requirements-guide.md`](cmake-usage-requirements-guide.md) for what a target is.

**The shape of this document:**

- **§1–§2** — what CMake means by cross-compiling, and the first thing that breaks when you try it.
- **§3** — where compiler flags legitimately live, which is a question this library answers unusually strictly.
- **§4–§5** — freestanding versus hosted, the narrower floor Zephyr sets, and — more important — the three things compiling does not catch.
- **§6** — what to reach for instead: `nm` and `objdump`, and the allowlist that turns the first into a check.
- **§7** — six exercises, all of which break something on purpose.
- **§8–§9** — the model in a paragraph, and where to go next.

---

## 1. Two machines, and the vocabulary for them

Compiling normally involves one machine playing both roles: it runs the compiler, and it runs the result. Cross-compiling splits them. The **host** runs the compiler; the **target** runs the output. Here the host is a Mac or a CI runner, and the target is an ARM Cortex-M with no operating system.

Everything awkward follows from one consequence: **the build system cannot run anything it produces.** It cannot execute a test binary to see whether a feature works, cannot probe the target's `sizeof(long)` by compiling and running a program, and cannot use "did it run?" as a check at any point.

CMake's model for this is a *toolchain file*: a small script, passed at configure time, that describes the target before any of the normal configuration happens.

```cmake
set(CMAKE_SYSTEM_NAME Generic)      # "an OS I will not name" -- i.e. bare metal
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
```

Setting `CMAKE_SYSTEM_NAME` at all is what tells CMake it is cross-compiling; `Generic` is the conventional value for a target with no operating system. From that point `CMAKE_CROSSCOMPILING` is true, and CMake stops assuming it can execute what it builds.

The file is passed at configure time and cannot be changed afterwards, which is why cross builds live in their own build directory:

```sh
cmake -S . -B build-cross -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DSTRIPSEQ_BUILD_TESTS=OFF
```

## 2. The compiler check, and why it fails first

The first thing CMake does with a new compiler is verify it works — by compiling *and linking* a tiny program. On a hosted system that succeeds silently. On bare metal it fails, and the error is confusing the first time: the compiler is fine, but linking a complete executable needs a startup file (`crt0`) and a linker script describing where flash and RAM live, and a general-purpose toolchain has no idea what those are for your board.

One line fixes it:

```cmake
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
```

This tells CMake to stop at the archive for its own internal checks — compile, do not link. It is the standard bare-metal answer, and it also happens to describe exactly what this repo's check does with the real library: compile everything, link nothing. Which is a limitation as much as a convenience, and §5 is about that.

The other toolchain-file boilerplate worth knowing is the `FIND_ROOT_PATH` block:

```cmake
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BEFORE)   # host tools are fine
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)     # but never host libraries
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)     # and never host headers
```

Without it, `find_package` and friends will happily hand your ARM build a library compiled for the host. The rule is: programs may come from the host (they run during the build), headers and libraries may not (they end up in the target image).

## 3. Where the flags live

This library sets no compiler flags at all — no `-Os`, no `-fno-exceptions`, no `CMAKE_CXX_FLAGS`. That is a project ground rule with a specific reason: the library is consumed by Zephyr, which owns the compiler, the standard library, the optimisation level and the warning set for everything built inside it. A library that hard-codes `-Os` fights its consumer and wins in whichever direction the argument order happens to fall.

So where do the target's flags come from? The toolchain file, which is the consumer side of that boundary:

```cmake
set(CMAKE_CXX_FLAGS_INIT "-mcpu=${STRIPSEQ_CROSS_CPU} -mthumb -ffreestanding -fno-exceptions -fno-rtti -Os -nostdinc++ -isystem ${CMAKE_CURRENT_LIST_DIR}/minimal-libcpp")
```

Note the `_INIT` suffix. `CMAKE_CXX_FLAGS_INIT` seeds the cache variable the first time the build directory is configured, so it acts as a *default* that a caller can still override on the command line — where a plain `set(CMAKE_CXX_FLAGS ...)` would silently overwrite whatever they asked for. As a general rule: toolchain files should set `_INIT` variables, not the cache variables themselves. The flip side is that a seed only takes on the first configure: change the toolchain file and an existing build directory keeps checking with the old flags. `scripts/cross-check.sh` therefore configures with `cmake --fresh`, which discards the cache and starts again — a few seconds for five source files, and never a stale check.

The core deserves a note, because the bench board for this project is a Cortex-M7 and the check builds for neither. It builds for two others, each stricter than the M7 in one direction. The M4 with no `-mfpu` is soft-float, so any floating-point arithmetic that sneaks into the library becomes a library call rather than an instruction — which is precisely the thing the project's no-floating-point rule exists to prevent, and §6 shows how to see it. The M0 is the bottom of the family: no divide instruction and no 32 × 32 → 64 multiply, so every division and every wide product becomes a call to a run-time helper as well (see [`integer-arithmetic-guide.md`](integer-arithmetic-guide.md) §1). The core is a cache variable, `STRIPSEQ_CROSS_CPU`, which the toolchain file reads into `-mcpu`, and the script configures one build directory per core: `build-cross` for the M4, `build-cross-m0` for the M0.

## 4. Freestanding versus hosted

C and C++ both define two flavours of implementation. A **hosted** one has an operating system underneath: a heap, a filesystem, `<vector>`, `<iostream>`, threads, `main()` called after a runtime has started. A **freestanding** one guarantees far less — the language, the headers that are pure compile-time machinery (`<cstdint>`, `<cstddef>`, `<type_traits>`, `<limits>`), and nothing that requires an OS to mean anything.

`-ffreestanding` tells the compiler which one you are. Historically that mostly changed assumptions about `main` and about the compiler's freedom to turn your loop into a `memcpy` call. Since GCC 13 it does something much more useful: the standard headers themselves check it.

```
bits/requires_hosted.h:34:4: error: #error "This header is not available in freestanding mode."
```

That is the actual error from `#include <vector>` under `-ffreestanding`. The header refuses, by name, at the top of the include chain — which turns "we do not use the hosted library" from a claim into something the compiler enforces.

Freestanding is a floor the standard sets, though, and a real platform can sit below it. Zephyr's default C++ library is its *minimal* one, which ships exactly three headers — `<cstddef>`, `<cstdint>` and `<new>`, in `lib/cpp/minimal/include/` — and when it is selected Zephyr compiles C++ with `-nostdinc++` (`CMakeLists.txt:2441`, the flag itself at `cmake/compiler/gcc/compiler_flags.cmake:131`), so the compiler's own C++ headers are not on the include path at all. `<type_traits>` is a freestanding header, compiles cleanly under `-ffreestanding`, and does not exist in a Zephyr build. A check that only asks for freestanding passes a library that Zephyr cannot compile.

So the check does what Zephyr does. `-nostdinc++` removes GCC's C++ headers, and `cmake/minimal-libcpp/` puts back the two the project's ground rule allows, each a C header plus its names in `std`, as Zephyr's are. A third standard header is then not refused but simply absent:

```
src/color.cpp:1:10: fatal error: type_traits: No such file or directory
```

The ground rule — only `<cstdint>` and `<cstddef>` — is enforced by the build, where `-ffreestanding` alone only enforced the floor beneath it.

`-fno-exceptions` and `-fno-rtti` are separate switches, and they fail loudly and immediately: `throw` becomes *"exception handling disabled"*, `typeid` becomes *"cannot use 'typeid' with -fno-rtti"*. They are in the check because that is how Zephyr builds C++ by default, so code that quietly relies on either would compile here and fail there.

## 5. What compiling does not catch

This is the section that keeps the check honest, because a check you overestimate is worse than no check at all. Three things compile cleanly that should not, and each has a different reason.

**Heap allocation.** `return new int(3);` compiles without complaint. `operator new` is implicitly declared by the language, so the compiler is happy; the failure would come from the *linker*, which would look for `operator new` and find that pulling it in drags a heap implementation into the image. Since we deliberately never link (§2), we never ask that question.

**Floating point.** `return a * 1.5;` compiles silently, because soft-float is not an error — it is a library call. Nothing in the compile step objects to arithmetic the target will emulate at fifty times the cost.

**Dynamic initialisation before `main`.** A file-scope `int x = g();` compiles, and so does a function-local `static int x = g();`. Both are legal C++. Both quietly cost you something the project has ruled out: the first emits an `.init_array` entry that only runs if the startup code walks it, and the second emits a guard variable plus calls to `__cxa_guard_acquire`/`__cxa_guard_release`, which drags in threading support and defeats `constinit`.

The pattern behind all three: **the compile step catches what the language and the headers can refuse, and nothing else.** Anything that is legal C++ but expensive or forbidden *for your target* needs a different instrument — §6, which the check also runs.

## 6. The instrument for the rest: `nm` and `objdump`

The object files the check produces are themselves the evidence, and two ordinary binutils tools read them. Undefined symbols say what an object will demand at link time:

```sh
arm-none-eabi-nm -u some.o
```

A `U __aeabi_dmul` is a double multiply the target cannot do in hardware. A `U __cxa_guard_acquire` is a function-local static. A `U _Znwj` is `operator new`. None of these required running anything.

The check reads them itself. `scripts/cross-check.sh` takes every symbol an object leaves undefined, drops those another of the library's objects defines, and compares the rest against an **allowlist** of the run-time helpers the design accepts: `__aeabi_uidiv` and `__aeabi_uidivmod`, the M0's 32-bit division; `memcpy` and `memset`, which the compiler emits on its own for copies and clearing loops; and `__aeabi_uldivmod` in `sequencer.cpp` and nowhere else, for `Sequencer::playhead`'s one 64-bit remainder. Anything else fails, by name and core:

```
error: cortex-m0: segment.cpp.obj calls __aeabi_idivmod, which is not on the allowlist
```

It is an allowlist rather than a list of forbidden symbols because the ways to go wrong are open-ended — a float routine, `operator new`, a static's guard, any libc call — while what the design accepts is short and known. And it is per object, which is what lets it accept the one 64-bit remainder where it was designed in and refuse a second one anywhere else.

Section headers say what will execute before your code does:

```sh
arm-none-eabi-objdump -h some.o | grep init_array
```

An `.init_array` section of non-zero size means something runs before `main`. Across this library's five object files there is no such section at all, which is the mechanical form of the "no dynamic initialisation at file scope" rule. The allowlist does not see it — an initialiser that calls one of the library's own functions leaves nothing undefined — so turning that observation into a CI check is one of Phase 7's jobs.

The general lesson is worth carrying to other projects: **compiling for the target proves the code is legal there; reading the object files proves it is affordable there.** They are different questions and they need different tools.

---

## 7. Exercises

Six, all on the host, no board. Every one of them edits a file on purpose; each says what to revert.

```sh
./scripts/cross-check.sh          # configures build-cross and build-cross-m0, compiles and checks the library
```

### Exercise 1 — Two floors, and the header between them

*Demonstrates §4.*

Add `#include <type_traits>` to the top of `src/color.cpp`.

```sh
./scripts/build.sh && echo "host: fine"
./scripts/cross-check.sh
```

```
host: fine
src/color.cpp:1:10: fatal error: type_traits: No such file or directory
```

**Revert it.** Now put the compiler's own headers back and ask only for freestanding, first for a hosted header and then for the same one:

```sh
echo '#include <vector>' | arm-none-eabi-g++ -std=c++20 -mcpu=cortex-m4 -mthumb -ffreestanding -x c++ -c - -o /dev/null
echo '#include <type_traits>' | arm-none-eabi-g++ -std=c++20 -mcpu=cortex-m4 -mthumb -ffreestanding -x c++ -c - -o /dev/null && echo "freestanding: fine"
```

```
bits/requires_hosted.h:34:4: error: #error "This header is not available in freestanding mode."
freestanding: fine
```

**Proves:** `-ffreestanding` is enforced by the standard headers themselves, so a hosted header like `<vector>` is refused rather than merely discouraged. But `<type_traits>` is freestanding and passes that floor; only the check's `-nostdinc++` and two-header shim, which is Zephyr's floor, refuses it. The host build would have told you neither.

### Exercise 2 — Find the floating point the compiler let through

*Demonstrates §5 and §6: legal, silent, and expensive.*

Add to `src/color.cpp`:

```cpp
double stripseq_scale(double a) { return a * 1.5; }
```

```sh
./scripts/cross-check.sh
```

```
error: cortex-m4: color.cpp.obj calls __aeabi_dmul, which is not on the allowlist
error: cortex-m0: color.cpp.obj calls __aeabi_dmul, which is not on the allowlist
```

Both trees compiled; what failed is the allowlist. The evidence it read is one command away:

```sh
arm-none-eabi-nm -u build-cross/CMakeFiles/stripseq.dir/src/color.cpp.obj
```

```
         U __aeabi_dmul
```

**Revert it.**

**Proves:** compiling successfully for the target says nothing about cost. The soft-float call is invisible to the compiler and obvious in the object file, which is why the no-floating-point rule needs `nm` and not just a green build — and why the check runs it.

### Exercise 3 — See what a function-local static actually costs

*Demonstrates §5: why the project bans them outright.*

Add to `src/sequence.cpp`:

```cpp
int stripseq_counter();
int stripseq_next() { static int x = stripseq_counter(); return x; }
```

```sh
./scripts/cross-check.sh
```

```
error: cortex-m4: sequence.cpp.obj calls stripseq_counter(), which is not on the allowlist
error: cortex-m4: sequence.cpp.obj calls __cxa_guard_acquire, which is not on the allowlist
error: cortex-m4: sequence.cpp.obj calls __cxa_guard_release, which is not on the allowlist
```

and the same three for `cortex-m0`. `stripseq_counter()` is there only because the exercise declares it without defining it; the guard calls are the static's. The object holds the rest:

```sh
arm-none-eabi-nm -C build-cross/CMakeFiles/stripseq.dir/src/sequence.cpp.obj
```

```
00000000 b guard variable for stripseq_next()::x
00000004 b stripseq_next()::x
         U __cxa_guard_acquire
         U __cxa_guard_release
```

**Revert it.**

**Proves:** one convenient-looking line emits a guard variable and two calls into a thread-safety runtime. The rule against function-local statics is not stylistic, and the evidence is four lines of `nm` output.

### Exercise 4 — Make something run before `main`

*Demonstrates §5–§6: dynamic initialisation, and the section that reveals it.*

Add to `src/strip.cpp`:

```cpp
int stripseq_seed();
int stripseq_value = stripseq_seed();
```

```sh
./scripts/cross-check.sh
arm-none-eabi-objdump -h build-cross/CMakeFiles/stripseq.dir/src/strip.cpp.obj | grep -E 'init_array|text.startup'
```

The check fails, naming `stripseq_seed()` — but only because the exercise leaves it undefined. Had the initialiser been one of the library's own functions, the allowlist would have passed it. The section is the evidence:

```
  3 .text.startup 00000010  ...
  4 .init_array   00000004  ...
```

Now revert it and run the same command over every object:

```sh
arm-none-eabi-objdump -h build-cross/CMakeFiles/stripseq.dir/src/*.obj | grep -c init_array
```

```
0
```

**Proves:** "nothing runs before `main`" is a property you can measure in one command, and the four-byte section is exactly what Phase 7 will assert stays absent.

### Exercise 5 — Break the toolchain file's compiler check

*Demonstrates §2.*

Comment out `set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)` in `cmake/arm-none-eabi.cmake` and configure into a fresh directory:

```sh
rm -rf /tmp/xc && cmake -S . -B /tmp/xc -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DSTRIPSEQ_BUILD_TESTS=OFF
```

```
CMake Error ... The C++ compiler "arm-none-eabi-g++" is not able to compile a simple test program.
...
undefined reference to `_exit'
```

**Revert it.** Read the error carefully: the compiler works perfectly. What is missing is `_exit` — a *libc* symbol that only exists once you supply startup code for a specific board.

**Proves:** CMake's compiler check is a link, and on bare metal a link needs board-specific pieces the toolchain cannot invent. Telling it to stop at a static library is what makes cross-configuration possible at all.

### Exercise 6 — The remainder only one core calls

*Demonstrates §3 and §6: why the check builds for an M0.*

Add to `src/segment.cpp`:

```cpp
int stripseq_wrap(int p, int n) { return p % n; }
```

```sh
./scripts/cross-check.sh
```

```
error: cortex-m0: segment.cpp.obj calls __aeabi_idivmod, which is not on the allowlist
```

One error, for one core. Compare what each object asks for:

```sh
for d in build-cross build-cross-m0; do echo "== $d"; arm-none-eabi-nm -u $d/CMakeFiles/stripseq.dir/src/segment.cpp.obj | grep aeabi; done
```

```
== build-cross
== build-cross-m0
         U __aeabi_idivmod
         U __aeabi_uidiv
```

**Revert it.** `__aeabi_uidiv` is the library's own and on the allowlist; `__aeabi_idivmod` is the planted line.

**Proves:** the same line is a divide instruction on the M4 and a library call on the M0, so a check that builds only for the M4 passes it. A signed remainder in particular is a helper nothing else in the library links — which is why [`integer-arithmetic-guide.md`](integer-arithmetic-guide.md) §3 takes remainders of the unsigned magnitude.

---

## 8. The model in one paragraph

Cross-compiling means the machine running the compiler is not the machine running the output, and the entire difficulty is that the build system can no longer execute anything it makes. CMake is told about it by a toolchain file naming the target system and the compilers, plus `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY`, because its own compiler check would otherwise try to link a full executable and fail for want of a startup file and a linker script. That file is also where target flags belong — via `_INIT` variables, so callers can still override them — which keeps a library from setting flags on behalf of a consumer that legitimately owns them. Compiling the library for the target with `-ffreestanding -fno-exceptions -fno-rtti` is a genuine test, because since GCC 13 the standard headers refuse to be included in freestanding mode and the two `-fno-` flags reject `throw` and `typeid` outright; and a platform can set a lower floor than the standard's, as Zephyr's minimal C++ library does with two headers and `-nostdinc++`, which the check reproduces with a shim. But it is only a test of *legality*: heap allocation, floating point and dynamic initialisation are all legal C++ that compiles clean, so proving they are absent means reading the object files with `nm` and `objdump` instead — undefined symbols for what the code will demand, checked against a short allowlist per object, and `.init_array` for what runs before `main`. What counts as expensive depends on the core, so the check builds for the M0, which divides in software, as well as for the soft-float M4.

## 9. Where to go next

- **`cmake/arm-none-eabi.cmake` and `scripts/cross-check.sh`** — two short files; after this guide, every line in them should be one of §1–§3.
- **[`cmake-usage-requirements-guide.md`](cmake-usage-requirements-guide.md) §4–§5** — the other half of §3: what a library may say about the toolchain, and why `cxx_std_20` is the only thing this one says.
- **`docs/IMPLEMENTATION-PLAN.md`, Phase 7** — where §6's `objdump` observation becomes a CI check too, alongside UBSan, ASan and the footprint numbers.
- **`docs/IMPLEMENTATION-PLAN.md`, Phase 6** — the real thing this check is a cheap proxy for: the library built inside Zephyr for an actual Cortex-M7, where the linker finally gets a say.
- **GCC's `-ffreestanding` documentation and `bits/requires_hosted.h` in your ARM toolchain** — the twenty-line header behind §4's error message, worth reading once to see how little machinery the enforcement needs.
