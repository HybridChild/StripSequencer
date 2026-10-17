# Nothing runs before `main`

*A from-first-principles guide to constant initialisation in C++20: `constexpr`, `consteval`, `constinit`, `inline constexpr`, and what a function-local static costs.*

This is a teaching document, not project documentation. It explains when a C++ object gets its value — while the compiler runs, or while the target boots — which keyword decides that, and which keywords only sound as though they do. The running example is this repository's compile-time sequences: `SequenceBuilder::build_const()`, the `colors::` palette, and a `Sequence` table that is meant to sit in flash and cost nothing at startup. It assumes [`cross-compiling-guide.md`](cross-compiling-guide.md) §5–§6, which shows what dynamic initialisation looks like in an object file; this guide is about why it appears and how to keep it from appearing.

**The shape of this document:**

- **§1–§2** — the two kinds of initialisation, who runs the second, and why `const` does not choose between them.
- **§3–§5** — the three keywords that do: `constexpr` ("may"), `consteval` ("must"), and `constinit` ("is initialised at compile time, and nothing more").
- **§6–§7** — one object across translation units, and the function-local static.
- **§8** — the synthesis: which keyword for which job.
- **§9** — seven exercises, all on the cross compiler.
- **§10–§11** — the model in a paragraph, and where to go next.

---

## 1. Two kinds of initialisation, and who runs the second

Every object with static storage duration — anything at namespace scope, and any `static` — gets its first value in one of two ways.

**Static initialisation** happens before the program exists as a running thing. Either the compiler can work the value out, in which case the bytes are written straight into the image (*constant initialisation*), or there is no initialiser at all, in which case the object is zero (*zero initialisation*). Either way no code of yours runs. A `const` object initialised this way lands in `.rodata`, which on a microcontroller is flash; a writable one lands in `.data`, which the startup code copies from flash into RAM in one bulk `memcpy` at reset; a zero one lands in `.bss`, which the startup code clears with one `memset`. Neither loop knows how many objects there are or what types they have.

**Dynamic initialisation** is everything else: an initialiser the compiler cannot evaluate, so it has to emit code that computes the value at run time. For a namespace-scope object that code goes into a generated function — `_GLOBAL__sub_I_<file>` in GCC's naming — and a pointer to that function goes into `.init_array`. Something has to walk that array and call every entry before `main`.

On a hosted system that something is the C runtime, and nobody thinks about it. On a microcontroller it is whatever startup code you have, and it is worth reading once. In Zephyr it is `z_static_init_gnu()` at `kernel/init.c:261`, a loop over `__zephyr_init_array_start` … `__zephyr_init_array_end`, and the place it is called from is the surprising part: `kernel/init.c:314`, in `bg_thread_main()`, **after** the `POST_KERNEL` init level has run. Every driver and every `SYS_INIT` hook at `PRE_KERNEL_*` or `POST_KERNEL` executes before any C++ dynamic initialiser has. A global whose value is computed at startup is not merely slower to set up; for a large part of boot it does not have its value yet, and code that runs then reads zeros. The walk itself exists only because `CONFIG_STATIC_INIT_GNU` defaults to `y` whenever C++ is enabled (`kernel/Kconfig:1122`).

That is the whole case for this library's ground rule — no dynamic initialisation, anywhere. A constant-initialised object has its value from the first instruction, in every init level, on every startup path. A dynamically initialised one has it from some point that depends on the RTOS's boot order.

## 2. `const` does not decide which

The natural assumption is that `const` means "fixed at compile time". It does not. `const` is a promise that *the program* will not modify the object after initialisation; it says nothing about when initialisation happens.

Two equally `const` objects of the same type:

```cpp
constexpr Sequence in_flash =
    SequenceBuilder{}.hold(colors::red, 500).hold(colors::black, 500).build_const();

Sequence make();                       // defined in some other file
const Sequence at_startup = make();
```

Compiled for a Cortex-M, they could hardly be more different (§9, Exercise 1):

| Declaration | Section | Costs |
| --- | --- | --- |
| `constexpr Sequence in_flash` | `.rodata`, 0x70 bytes | 112 bytes of flash, nothing else |
| `const Sequence at_startup` | `.bss`, 0x70 bytes | 112 bytes of RAM, plus a 4-byte `.init_array` entry and a 16-byte `_GLOBAL__sub_I_…` that calls `make()` and writes the result |

The `const` one lives in RAM, not flash, because something has to write it at startup and flash cannot be written by an ordinary store. Read the table as a rule: **whether an object is initialised at compile time is decided by its initialiser, not by its qualifiers.** `const` is about mutation; three other keywords are about initialisation, and they promise different things.

## 3. `constexpr`: "may"

A `constexpr` **function** is one that *may* be evaluated at compile time. When every input is a constant and the context asks for a constant — a `constexpr` variable's initialiser, a `static_assert`, an array bound, a template argument — the compiler evaluates it. Anywhere else, it is an ordinary function, compiled to ordinary code and called at run time like any other.

That double life is exactly what `SequenceBuilder` needs. Every builder method and `build()` itself is `constexpr`, so the one implementation serves both a sequence assembled at run time from user input — where an invalid one must come back as an `Error`, not a crash — and a sequence written in source and evaluated by the compiler. The repo's tests read `build()`'s verdict both ways: a `CHECK` at run time, and the same call inside a `static_assert` lambda at compile time.

A `constexpr` **variable** is stricter, and this is where the guarantee lives. It must be initialised by a constant expression — if the compiler cannot evaluate the initialiser, the declaration does not compile. It is implicitly `const`. And its value is usable in later constant expressions, which is what lets `tests/sequence.cpp` write `static_assert(blink.loop_duration_ms() == 1000)` against a `constexpr Sequence blink`.

So the variable half of `constexpr` is a promise and the function half is an invitation. That asymmetry is what §4 is about.

## 4. `consteval`: "must", and how a function refuses to compile

Validation is where "may" stops being enough. `build_const()` is meant to turn an invalid sequence written in source into a compile error. If it were `constexpr`, it would only do that when the compiler happened to evaluate it at compile time; called anywhere else, an invalid sequence would quietly become a run-time call (§9, Exercise 5).

A `consteval` function — an *immediate function* — removes the choice. Every call to it must be a constant expression. The compiler either evaluates it at compile time or rejects the call; there is no run-time version, and the function never appears in the object file.

That leaves the question of how a function evaluated by the compiler signals failure. The rule it relies on: **constant evaluation stops at anything a constant expression may not do, and reports it.** Calling a function that is not `constexpr` is one such thing. So `build_const()` calls `build()`, and on failure calls a function that is declared and never defined:

```cpp
namespace invalid_sequence {
void sequence_has_a_zero_duration_fade();
// ... one per Error
}

consteval Sequence build_const() const {
    Sequence out{};
    using namespace invalid_sequence;
    switch (build(out)) {
    case Error::None:
        break;
    case Error::ZeroDurationFade:
        sequence_has_a_zero_duration_fade();
        break;
    // ...
    }
    return out;
}
```

On the valid path the compiler never reaches a call, so the evaluation succeeds. On an invalid path it reaches exactly one, and the diagnostic names it:

```
sequence.h:262:46: error: call to non-'constexpr' function 'void stripseq::invalid_sequence::sequence_has_a_zero_duration_fade()'
```

Three details carry most of the weight.

**Only the branch taken is evaluated.** Constant evaluation executes the function, it does not compile it, so the seven other failure functions in the `switch` are never touched. That is why one function per `Error` works: the name in the diagnostic is the error message, and it says what is wrong rather than only that something is. Clang prints only the unqualified name in its note, which is why each name reads as a sentence on its own.

**Not `throw`.** The usual version of this trick is `throw "message";` in the failure branch — a throw is also something a constant expression may not do. But under `-fno-exceptions`, which is how Zephyr builds C++, a `throw` is ill-formed wherever it appears, evaluated or not: GCC rejects the function with *"exception handling disabled, use '-fexceptions' to enable"*. A call to an undefined function is legal everywhere and fails only when evaluated.

**Never defined, so never callable at run time either.** If someone weakened `consteval` to `constexpr`, the failure functions would become real call sites in real code, and the program would fail at link time instead of compiling — late, but not silently.

## 5. `constinit`: initialised at compile time, and nothing more

`constinit` answers a narrower question than `constexpr`. Applied to a variable with static storage duration, it asserts that the variable is constant-initialised: if its initialiser is not a constant expression, the declaration does not compile. That is all it says.

In particular, it does not say:

- **That the variable is `const`.** A `constinit` object can be modified at run time. That is its point.
- **That its value can be read at compile time.** The compiler guarantees the bytes are in the image, but it does not let a later constant expression read them, because the program might have changed them by then.

The second one is easy to walk into. This compiles:

```cpp
constinit const Sequence blink = SequenceBuilder{}.hold(colors::red, 500).build_const();
```

and this, on the next line, does not:

```cpp
static_assert(blink.loop_duration_ms() == 500);
```

```
error: the value of 'blink' is not usable in a constant expression
note: 'blink' was not declared 'constexpr'
```

Adding `const` does not rescue it. The language's old exception — a `const` variable of *integral or enumeration* type with a constant initialiser is usable in constant expressions — still applies to `constinit const int`, which is why the misconception survives. It does not extend to class types.

So the two keywords split the territory cleanly. A `Sequence` table never changes after the compiler builds it, so it wants `constexpr`: in flash, and readable by `static_assert`. A `Strip` is the opposite — its segment list and brightness change constantly at run time, so it cannot be `const`, but it must still not run a constructor before `main`. That is exactly `constinit`, and it is how the API sketch declares one:

```cpp
static uint8_t buffer[11 * Strip::bytes_per_pixel];
constinit static Strip strip(buffer);
```

For that to compile, `Strip`'s constructor has to be `constexpr`, which is why it is. `tests/layout.cpp` declares a strip exactly this way, so the day the constructor stops being a constant expression, the tests stop compiling.

## 6. `inline constexpr`: one object, not one per file

A namespace-scope variable that is `const` — and `constexpr` implies `const` — has **internal linkage** unless it says otherwise. Each translation unit that includes the header gets its own private copy, with its own address.

For a 6-byte `Color` that is usually invisible: the compiler folds the value into the instructions that use it and never emits the object at all. For a 112-byte `Sequence` whose address is taken — `seq.load(kRainbow)` keeps a pointer to it — every file that loads it carries its own 112 bytes of flash, and `&kRainbow` compares unequal across files (§9, Exercise 6).

C++17's inline variables fix this. `inline` gives the variable external linkage and tells the linker that identical definitions in many object files are the same object: GCC emits it as a weak symbol (`V` in `nm`), and the linker keeps one. That is why the palette in `color.h` is

```cpp
inline constexpr Color red = Color::rgb8(255, 0, 0);
```

and why the cookbook's shared rainbow is `inline constexpr Sequence kRainbow = SequenceBuilder{}…build_const();`, and the library's empty `no_sequence`, whose address a default sequencer holds and a test compares. All are constant-initialised into flash, nothing runs before `main`, and there is one of each in the image however many files use them.

## 7. The function-local static

A `static` inside a function is initialised the first time control passes through its declaration — *if* its initialiser is dynamic. C++11 made that first-time initialisation thread-safe, so the compiler wraps it: a guard variable records whether initialisation has happened, and `__cxa_guard_acquire` / `__cxa_guard_release` serialise the first call against any concurrent one. [`cross-compiling-guide.md`](cross-compiling-guide.md) Exercise 3 shows those symbols appearing. On a target with no threads, or with an RTOS whose threads the C++ runtime knows nothing about, that is a lock implementation pulled into the image to protect a race that either cannot happen or is not actually prevented.

The guard is the cost of the *dynamic* initialiser, not of being a local static. Give the same static a constant initialiser and it is constant-initialised like any namespace-scope object — no guard, no runtime call, and the object sits in `.rodata` (§9, Exercise 7). The project rule bans function-local statics outright anyway, because the difference between the two is one edit to the initialiser, and the costly version looks exactly like the free one.

## 8. Choosing

Each keyword answers one question. Combining them is how you say what you mean.

| You want | Write | Because |
| --- | --- | --- |
| A table fixed at compile time, readable by `static_assert` | `constexpr` (in a header: `inline constexpr`) | Constant-initialised, `const`, and usable in constant expressions; `inline` makes it one object |
| An object that changes at run time but has its value from reset | `constinit` | Constant initialisation guaranteed, mutation allowed, nothing in `.init_array` |
| A function usable both at run time and at compile time | `constexpr` | "May": the context decides |
| A function that must never run at run time | `consteval` | "Must": a call that cannot be evaluated is a compile error |
| A compile-time computation that fails with a message | `consteval` + a call to a declared, undefined function on the failure path | The diagnostic names the function; legal under `-fno-exceptions` |

And the one row that is not on the table: **`const` alone decides nothing about initialisation.** If a declaration's cost matters, one of the first two rows is the way to make the compiler enforce it.

---

## 9. Exercises

Seven, all compile-only, using the same cross compiler and flags as `scripts/cross-check.sh`. Set up a scratch directory and a helper first; the function form works in both bash and zsh:

```sh
mkdir -p /tmp/stripseq-lab && cd /tmp/stripseq-lab
REPO=~/path/to/StripSequencer                 # wherever you cloned it
xcxx() { arm-none-eabi-g++ -std=c++20 -mcpu=cortex-m4 -mthumb -ffreestanding \
           -fno-exceptions -fno-rtti -Os -I"$REPO/include" "$@"; }
```

### Exercise 1 — Two `const` objects, two sections

*Demonstrates §1–§2.*

```cpp
// init.cpp
#include <stripseq/sequence.h>
using namespace stripseq;

constexpr Sequence in_flash =
    SequenceBuilder{}.hold(colors::red, 500).hold(colors::black, 500).build_const();

Sequence make();
const Sequence at_startup = make();

const Sequence* use[] = {&in_flash, &at_startup};   // so neither is optimised away
```

```sh
xcxx -c init.cpp && arm-none-eabi-nm -C init.o
arm-none-eabi-objdump -h init.o | grep -E 'rodata|bss|init_array|\.data|text.startup'
```

```
00000000 t _GLOBAL__sub_I_use
         U make()
00000000 b at_startup
00000000 r in_flash
00000000 D use
  1 .data         00000008  ...
  2 .bss          00000070  ...
  3 .text.startup 00000010  ...
  4 .init_array   00000004  ...
  5 .rodata       00000070  ...
```

Note `use` as well: an array of addresses is constant-initialised, but it is not `const`, so it lands in `.data` — RAM, filled by the startup code's bulk copy, with no `.init_array` entry of its own.

**Proves:** `const` did not decide where either object went. The `constexpr` one is 112 bytes of flash; the `const` one is 112 bytes of RAM plus an initialiser function and the array entry that calls it.

### Exercise 2 — `constinit` turns the silent cost into an error

*Demonstrates §5: the guarantee half.*

In `init.cpp`, change the second declaration to `constinit const Sequence at_startup = make();`.

```sh
xcxx -c init.cpp
```

```
error: 'constinit' variable 'at_startup' does not have a constant initializer
error: call to non-'constexpr' function 'stripseq::Sequence make()'
```

**Proves:** `constinit` is a check, not an optimisation. It cannot make `make()` run at compile time; it can only refuse to let the object be dynamically initialised without anyone noticing.

### Exercise 3 — `constinit` is not `constexpr`

*Demonstrates §5: the half it does not promise.*

```cpp
// readback.cpp
#include <stripseq/sequence.h>
using namespace stripseq;

constinit const Sequence blink = SequenceBuilder{}.hold(colors::red, 500).build_const();
static_assert(blink.loop_duration_ms() == 500);
```

```sh
xcxx -c readback.cpp
```

```
readback.cpp:6:40: error: non-constant condition for static assertion
include/stripseq/sequence.h:94:33: error: the value of 'blink' is not usable in a constant expression
readback.cpp:5:26: note: 'blink' was not declared 'constexpr'
```

Change `constinit const` to `constexpr` and it compiles. Then try `constinit const int n = 3; static_assert(n == 3);` — that one compiles too.

**Proves:** constant initialisation and usability in constant expressions are separate properties. Only `constexpr` gives both for a class type; the integral case is a special rule that makes the general one look true.

### Exercise 4 — `consteval` names the error

*Demonstrates §4.*

```cpp
// invalid.cpp
#include <stripseq/sequence.h>
using namespace stripseq;

constexpr Sequence bad = SequenceBuilder{}.fade_to(colors::red, 0).build_const();
```

```sh
xcxx -c invalid.cpp
```

```
include/stripseq/sequence.h:262:46: error: call to non-'constexpr' function 'void stripseq::invalid_sequence::sequence_has_a_zero_duration_fade()'
```

Change the `0` to `500` and it compiles. Then try each of the other mistakes — no steps, `.repeat(0)`, `.landing_color(…)` on an endless sequence — and read which function each one names.

**Proves:** constant evaluation runs only the branch taken, so the one failure function reached is the one in the diagnostic, and its name is the message.

### Exercise 5 — Demote `consteval` and watch the error move

*Demonstrates §3–§4: "may" against "must".*

```cpp
// runtime.cpp
#include <stripseq/sequence.h>
using namespace stripseq;

int main() {
    const Sequence s = SequenceBuilder{}.fade_to(colors::red, 0).build_const();
    return s.loop_duration_ms() == 0;
}
```

```sh
xcxx -c runtime.cpp
```

With the header as it is, that is the same compile error as Exercise 4: `s` is not `constexpr`, but a call to a `consteval` function must be a constant expression wherever it appears.

Now edit `include/stripseq/sequence.h` and change `consteval Sequence build_const()` to `constexpr Sequence build_const()`.

```sh
xcxx -O0 -c runtime.cpp && arm-none-eabi-nm -C runtime.o | grep invalid_sequence
```

```
         U stripseq::invalid_sequence::sequence_has_no_steps()
         U stripseq::invalid_sequence::sequence_repeat_count_is_zero()
         U stripseq::invalid_sequence::sequence_has_a_zero_duration_fade()
         ... eight in all
```

```sh
xcxx -c runtime.cpp && arm-none-eabi-nm -C runtime.o | grep invalid_sequence
```

```
         U stripseq::invalid_sequence::sequence_has_a_zero_duration_fade()
```

Both compile. At `-O0` the object references all eight failure functions, because the `switch` is now ordinary code and every branch is a real call; at `-Os` the optimiser folds the builder and only the branch it can prove is taken survives. Either way the first sign of trouble is a linker saying a symbol is undefined, and only one of the two tells you which mistake you made. Change `fade_to(colors::red, 0)` to `hold(colors::red, 500)` and both versions compile and reference nothing: GCC happened to evaluate this call at compile time, which it was free to do and equally free not to. So the invalid sequence was caught late and by accident, and whether the valid one costs anything at run time is now up to the optimiser. **Revert the header.**

**Proves:** a `constexpr` function is evaluated at compile time only when the context demands it. `consteval` is what turns "the compiler might check this" into "the compiler has checked this".

### Exercise 6 — `inline` makes one object

*Demonstrates §6.*

```cpp
// k.h
#include <stripseq/sequence.h>
#ifdef ONE
inline
#endif
constexpr stripseq::Sequence kBlink =
    stripseq::SequenceBuilder{}.hold(stripseq::colors::red, 500).build_const();
const void* from_a();
```

```cpp
// ka.cpp
#include "k.h"
const void* from_a() { return &kBlink; }
```

```cpp
// kb.cpp
#include "k.h"
#include <cstdio>
int main() { std::printf("%s\n", from_a() == &kBlink ? "same object" : "two copies"); }
```

On the host, where it can run:

```sh
c++ -std=c++20 -I"$REPO/include"      ka.cpp kb.cpp -o k && ./k
c++ -std=c++20 -I"$REPO/include" -DONE ka.cpp kb.cpp -o k && ./k
```

```
two copies
same object
```

And on the target, where the symbol kind says why:

```sh
xcxx      -c ka.cpp && arm-none-eabi-nm -C ka.o | grep kBlink
xcxx -DONE -c ka.cpp && arm-none-eabi-nm -C ka.o | grep kBlink
```

```
00000000 r kBlink
00000000 V kBlink
```

**Proves:** a namespace-scope `constexpr` is private to its file (`r`, local read-only data), so every file that takes its address pays for its own copy. `inline` makes it a weak object (`V`) that the linker deduplicates into one.

### Exercise 7 — The guard is the initialiser's fault

*Demonstrates §7, and extends [`cross-compiling-guide.md`](cross-compiling-guide.md) Exercise 3.*

```cpp
// local.cpp
#include <stripseq/sequence.h>
using namespace stripseq;

Sequence make();

const Sequence& constant() {
    static const Sequence s = SequenceBuilder{}.hold(colors::red, 500).build_const();
    return s;
}

const Sequence& dynamic() {
    static const Sequence s = make();
    return s;
}
```

```sh
xcxx -c local.cpp && arm-none-eabi-nm -C local.o
```

```
         U make()
00000008 T dynamic()
00000000 T constant()
00000000 b guard variable for dynamic()::s
00000004 b dynamic()::s
00000000 r constant()::s
         U __cxa_guard_acquire
         U __cxa_guard_release
```

**Proves:** two declarations that differ only in their initialiser. The constant one is a `.rodata` object like any other; the dynamic one costs RAM, a guard, and two calls into a thread-safety runtime. The rule bans both because nothing at the declaration shows which one you wrote.

---

## 10. The model in one paragraph

An object with static storage gets its value either from the image — written there by the compiler, or zero — or from code that runs at startup, and on a microcontroller the difference is not only cost but timing: Zephyr walks `.init_array` only after its `POST_KERNEL` init level, so a dynamically initialised global has no value yet while drivers start. `const` does not choose between the two; it only forbids later mutation, and a `const` object with a run-time initialiser lands in RAM with an initialiser function behind it. Three keywords do choose. A `constexpr` variable must be constant-initialised and can be read by later constant expressions, while a `constexpr` function merely may run at compile time and silently runs at run time otherwise. A `consteval` function must run at compile time, which is what lets `build_const()` fail by calling a declared, undefined function: constant evaluation executes only the branch taken, so the diagnostic names exactly one error, and unlike `throw` it is legal under `-fno-exceptions`. `constinit` guarantees constant initialisation and nothing else — the object stays mutable and cannot be read by a `static_assert` — which makes it the right tool for a `Strip` and the wrong one for a `Sequence`. At namespace scope `constexpr` implies internal linkage, so a header constant needs `inline` to be one object rather than one per file, and a function-local static costs a guard and `__cxa_guard_*` calls exactly when its initialiser is dynamic.

## 11. Where to go next

- **[`cross-compiling-guide.md`](cross-compiling-guide.md) §5–§6** — the object-file evidence this guide's exercises read, and the Exercise 4 `.init_array` that the library's own objects never contain.
- **`include/stripseq/sequence.h`** — `SequenceBuilder::build_const()` and the `invalid_sequence` namespace above it, about forty lines that use §3–§4 in full.
- **`tests/sequence.cpp`, "Compile time"** — a `constexpr Sequence` read back by `static_assert`, and `build()`'s verdict read inside a `static_assert` lambda.
- **[`testing-guide.md`](testing-guide.md) §7** — how the failure half of `build_const()` is tested, when the test is a file that must not compile.
- **`docs/IMPLEMENTATION-PLAN.md`, Phase 7** — where "`.init_array` is empty and no `__cxa_guard` symbol appears" becomes a CI check instead of an exercise.
- **Zephyr's `kernel/init.c:256–315`** — `z_static_init_gnu()` and the place in `bg_thread_main()` where it runs, relative to every init level around it.
