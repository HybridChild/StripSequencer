# Integer arithmetic that never steps backwards

*A from-first-principles guide to exact integer arithmetic on a small target: signed remainders, overflow in intermediates, rounding that stays monotonic, and the division the chip cannot do.*

This is a teaching document, not project documentation. It explains the handful of ways integer arithmetic goes wrong when there is no floating point to hide behind, and the few habits that make it go right: fold a remainder, negate in unsigned, size the intermediate, floor once, and divide by a power of two — and, when the function is irrational, compare instead of computing. The running example is the arithmetic at the heart of this repository — `Sequence::at()`, which maps a signed playhead onto a looping timeline; `Color::lerp()`, which blends two 16-bit colors; the easing curves behind every fade; the length check in `SequenceBuilder::build()`; and the generator of the gamma table in `src/color.cpp`. It assumes [`cross-compiling-guide.md`](cross-compiling-guide.md) §6 for reading undefined symbols with `nm`, and nothing else beyond C++ itself.

**The shape of this document:**

- **§1** — the budget: what a Cortex-M does in one instruction and what it calls a library for.
- **§2–§3** — signed numbers: the remainder that comes out negative, and the one value with no positive twin.
- **§4** — overflow in intermediates, including the overflow nothing will warn you about.
- **§5–§6** — rounding: why one floor stays monotonic and two can step backwards, and how a division becomes a shift.
- **§7** — rounding a function you cannot compute, by comparing instead.
- **§8** — testing arithmetic exhaustively, because 65,536 inputs is nothing.
- **§9** — the synthesis, as a checklist.
- **§10** — six exercises.
- **§11–§12** — the model in a paragraph, and where to go next.

---

## 1. The budget

The library has no floating point anywhere, by rule — on a Cortex-M4 built soft-float, `a * 1.5` is a call to `__aeabi_dmul` (see [`cross-compiling-guide.md`](cross-compiling-guide.md) Exercise 2). Everything is integers, and integers are not all equally cheap either:

| Operation | Cortex-M3/M4/M7 | Cortex-M0/M0+ |
| --- | --- | --- |
| 32-bit add, subtract, shift | one instruction | one instruction |
| 32 × 32 → 32 multiply | one instruction | one instruction |
| 32 × 32 → 64 multiply | one instruction (`UMULL`) | library call |
| 32-bit divide | one instruction (`UDIV`), a few cycles | `__aeabi_uidiv` |
| 64-bit divide | `__aeabi_uldivmod` | `__aeabi_uldivmod` |

The row that shapes this library is the last one. A 64-bit multiply on an M4 is inline code, but a 64-bit divide is a call into libgcc that loops over the bits, and the per-pixel path runs once per pixel per frame. The rule that falls out is: **use 64 bits for products when you must, and never divide one.** Nothing in the compiler enforces it — a 64-bit division compiles exactly like a 32-bit one — so the evidence is the same as for floating point, an undefined symbol in the object file (§10, Exercise 3).

Two facts about C++ itself complete the budget. **Integer promotion:** any arithmetic on types narrower than `int` happens in `int`, so two `uint16_t` values are multiplied as *signed* 32-bit integers, and `65535 * 65535` overflows — undefined behaviour from two unsigned operands. **Unsigned wrap:** arithmetic on `uint32_t` is defined to be modulo 2³², so it never traps and never warns. The first is why every product in this library starts with an explicit `uint32_t{x}`; the second is §4's subject.

## 2. The remainder that comes out negative

An endless sequence is a ring: playhead `p` on a loop of `L` milliseconds means position `p mod L`. And the playhead is signed, because a pixel delayed further than the time since `play()` has a negative playhead — for a staggered effect that is the common case, not an edge case.

C++ integer division rounds toward zero, and the remainder takes the sign of the dividend so that `(p / L) * L + p % L == p` holds. So `-1 % 1000` is `-1`, not `999`. The mathematical modulo needs one fold back into range:

```cpp
local = ((p % L) + L) % L;
```

`p % L` lies in `(-L, L)`; adding `L` moves it to `(0, 2L)`, and the second `%` brings it to `[0, L)`. No term can overflow as long as `2L` fits in an `int32_t`, which the builder's 2²⁹ ms limit guarantees.

The fold is easy to get almost right, and the interesting thing is which playheads catch which almost-right version. For `L = 1000` (§10, Exercise 1):

| Version | `p = -1` | `p = -1000` | `p = INT32_MIN` |
| --- | --- | --- | --- |
| `p % L` | −1 | 0 | −648 |
| `uint32_t(p) % L` | 295 | 296 | 648 |
| `L - (-p % L)` | 999 | **1000** | undefined behaviour |
| `((p % L) + L) % L` | 999 | 0 | 352 |
| magnitude, then mirror (§3) | 999 | 0 | 352 |

The unsigned cast reinterprets −1 as 2³² − 1 and takes *that* modulo 1000, which is right only when `L` divides 2³²; −1 catches it. The mirror is right at −1 and wrong at −1000, where it returns `L` itself, one past the end of the ring; only a playhead that is an exact negative multiple of the loop catches it. And it negates `p`, which for `INT32_MIN` is §3's problem. A test at −1 alone passes two of the three wrong versions. The boundary set is **−1, −L, and `INT32_MIN`** — and in `tests/sequence.cpp`, `INT32_MIN` is checked to land at 352 with a hold of 148.

## 3. The one value with no positive twin

A two's-complement `int32_t` runs from −2³¹ to 2³¹ − 1. Every value but one can be negated; `-INT32_MIN` would be 2³¹, which does not fit, so it is signed overflow and undefined behaviour.

`Sequence::at()` meets this before a finite sequence starts. A playhead of −250 means this pixel starts moving in 250 ms, so the hold it reports is `-p` — and the playhead can be anything, including `INT32_MIN`. The fix is to negate in unsigned arithmetic, where every operation is defined:

```cpp
return Sample{start_, 0u - static_cast<uint32_t>(playhead_ms)};
```

Converting a negative `int32_t` to `uint32_t` is defined as adding 2³², and subtracting that from zero modulo 2³² gives back exactly the magnitude: 250 for −250, and 2,147,483,648 for `INT32_MIN`, which fits comfortably in a `uint32_t`. There is no special case because the arithmetic has no special case. UBSan names this trick in its own diagnostic for the signed version: *"cast to an unsigned type to negate this value to itself"*.

The same trick gives §2's fold a cheaper form, and it is the one the library uses. Take the remainder of the playhead's *magnitude*, in unsigned arithmetic, and mirror it when the playhead was negative:

```cpp
const uint32_t magnitude = p < 0 ? 0u - static_cast<uint32_t>(p) : static_cast<uint32_t>(p);
const uint32_t r = magnitude % L;
local = p < 0 && r != 0 ? L - r : r;
```

This is §2's mirrored version with both of its faults fixed: the negation happens in unsigned, so `INT32_MIN` is no special case, and a remainder of 0 stays 0 rather than becoming `L`. It agrees with the double fold everywhere, and costs one remainder where that costs two. The bigger difference is the sign. On a Cortex-M0, which has no divide instruction, a signed remainder and an unsigned one are different library calls, and an unsigned one is usually linked already — Zephyr's own clock code needs `__aeabi_uldivmod` — while a signed one may be the only reason its helper is in the image. `Sequence::at()` folds in 32 bits this way, and `Sequencer::playhead()` folds a 64-bit position the same way, in `uint64_t`.

## 4. Overflow in intermediates

Most overflow bugs are not in the result, which fits, but in a value computed on the way to it. Three from this library, and the habit each one teaches.

**Blending: size the numerator, and use the form that has no sign.** `Color::lerp()` blends each channel `x → y` by `t` in 0…65535. The form everyone writes first is

```cpp
x + (y - x) * t / 65535
```

and it is wrong twice. Promotion makes `y - x` and the product signed `int`, so `(65535 - 0) * 65535` overflows: undefined behaviour at full scale, in the most common blend there is. And even where it wraps "harmlessly", the end it lands on is 65534 rather than 65535 (§10, Exercise 2). The library writes it as a weighted sum instead:

```cpp
(uint32_t{x} * (65535u - t) + uint32_t{y} * t) / 65535u
```

The two weights sum to 65535, so the largest the numerator can be is 65535 × 65535 = 4,294,836,225 — under 2³², so `uint32_t` is enough, with no sign anywhere and no direction in which `y < x` is a special case. At `t = 0` it is exactly `x`; at `t = 65535` exactly `y`.

**Progress: shift both sides until the product fits.** How far through a fade, as 0…65535, is `elapsed * 65535 / duration`. With `elapsed < duration ≤ 65536` the product fits in 32 bits. A fade longer than about 65.5 seconds breaks that: 70,000 ms into a 100,000 ms fade the product is 4.59 × 10⁹, which wraps, and the fade reads as about 4 % through instead of 70 % — a visible jump backwards in the middle of a slow fade. Promoting to 64 bits would fix it with a 64-bit division per pixel (§1). The library halves both `elapsed` and `duration` until the duration fits instead:

```cpp
while (duration > 65536) {
    elapsed >>= 1;
    duration >>= 1;
}
return static_cast<uint16_t>(elapsed * full / duration);
```

The ratio barely moves. What is lost is resolution in `elapsed`, and the loop stops as soon as the duration is back in (32768, 65536], so one remaining unit of `elapsed` is worth at most two of the 65,536 levels of progress, however long the fade. Nobody can see that. And halving a smaller number never produces a bigger one, so progress stays monotonic.

**Validation: when the check itself wraps.** `SequenceBuilder::build()` rejects anything over 2²⁹ ms in total. The total is a sum of up to eight `uint32_t` durations times a `uint16_t` repeat count, and in `uint32_t` both operations wrap silently: 2¹⁷ ms repeated 2¹⁵ times is 2³², which wraps to exactly **0**, and a zero-length total passes a "too long?" check with room to spare. So the check is done in `uint64_t`, with a written bound — at most 8 × (2³² − 1) × (2¹⁶ − 1), under 2⁵¹ — showing that *it* cannot wrap. A multiply in 64 bits is cheap (§1), and this one runs once per build, not per pixel.

The last example is the important one, because unsigned wrap is *defined*. UBSan reports the signed overflow in the naive lerp; it says nothing about the wrapped total, because there is nothing undefined to report. The only defence is a test at the boundary that wrapping would cross — which is what the "totals that overflow `uint32_t`" case in `tests/sequence.cpp` is, and §10 Exercise 5 shows it catching exactly this mistake.

## 5. One floor stays monotonic; two can step backwards

Integer division floors, so every curve in the library is a real-valued function rounded down. Two properties matter for a fade, and both are visible if they fail:

- **Exact at both ends.** A fade that tops out at 65534 leaves a one-step jump to the next step's 65535 at every boundary — a tick in a slow breathe, repeated forever.
- **Monotonic.** A fade from dark to bright must never get darker, even by one step, even once.

The tool for both is one fact: **the floor of a monotonic function is monotonic.** If `f(a) ≤ f(b)`, then `⌊f(a)⌋ ≤ ⌊f(b)⌋`, because the floor itself never decreases. So an expression computed as a single division of an exact integer numerator inherits the monotonicity of the real curve, for free and for every input. `lerp` is one such floor of a function linear in `t`; ease-in is `⌊t² / 65535⌋`; ease-out is 65535 minus a floor of a function that falls as `t` rises.

The fact does not survive arithmetic *between* floors. Smoothstep, `t²(3 − 2t)`, is the textbook ease-in-out curve, and the tempting way to keep it inside 32 bits is to floor the square and the cube separately:

```cpp
const uint32_t sq   = uint32_t{t} * t / 65535;
const uint32_t cube = sq * t / 65535;
return 3 * sq - 2 * cube;
```

Each floor is a little low, by a different amount, and the second is subtracted. When `sq` happens to stay put across a step of `t` while `cube` ticks up, the result drops. Checked over every input, this version steps backwards **3,655 times**, the first at `t = 1639`, where it goes from 120 to 118 (§10, Exercise 4). Both ends are exact, the curve looks right on a plot, and it flickers.

The library's `EaseInOut` stays clear of this by choosing a curve made of pieces that are each one floor: ease-in at double speed up to the middle, `⌊2t² / 65535⌋`, and ease-out at double speed after it, `65535 − ⌊2(65535 − t)² / 65535⌋`. Each half is monotonic on its own, and where they join the first half's last value, 32766 at `t = 32767`, is below the second half's first, 32769 at `t = 32768`, so the whole never steps backwards. **A piecewise curve needs one floor per piece, and pieces that meet in order.** The halves are each other turned through 180°, so the curve is exactly symmetric about its middle, and the largest product, 2 · 32767², fits in 32 bits. The price is the shape: two parabolas are a little steeper through the middle than smoothstep's cubic.

## 6. Turning a division into a shift

A single floor of smoothstep, done directly, is `t²(3·65535 − 2t) / 65535²`. The numerator needs 64 bits, so the division is 64-bit: `__aeabi_uldivmod`, per pixel, in the breathe every status light runs (§10, Exercise 3). §5's two parabolas avoid that by changing the formula. When the formula is fixed, the way out is to change the scale instead.

The brightness knob is the library's example. Scaling a 16-bit channel by `level / 255` and keeping its top byte is `⌊v · level / 255⌋ >> 8`, which is `⌊v · level / 65280⌋` — a division by a number that is not a power of two. Move the knob from 0…255 onto 1…256 and the divisor becomes 65536, a shift:

```cpp
return (uint32_t{v} * (level + 1)) >> 16;      // v < 2¹⁶, level + 1 ≤ 2⁸: under 2²⁴
```

The two scales agree at the ends: level 255 gives `v >> 8` exactly, and level 0 gives 0. In between the shift is never below the division and never more than one above it, which `tests/convert.cpp` checks over all 16.7 million pairs of `v` and `level`. It is one floor of a function rising in both `v` and `level`, so it is monotonic in both: turning the knob up never darkens a pixel, and neither does a channel rising through a fade. What it gives up is exactness between the ends, by at most one of the 256 levels the wire carries.

Some divisors cannot be rescaled away, because the scale is the data's own. `lerp` divides by 65535 because its two weights, `65535 − t` and `t`, sum to 65535, and the easing curves divide `t²` by 65535 to bring it back to 16 bits. On an M4 that is one `UDIV`. On an M0 it is a call to `__aeabi_uidiv` for every channel of every pixel mid-fade, and the compiler at `-Os` emits exactly that rather than the multiply-by-reciprocal it uses at `-O2`. But 65535 is 2¹⁶ − 1, and dividing by one less than a power of two has a form made of shifts. Since 1/(2¹⁶ − 1) = 2⁻¹⁶ · (1 + 2⁻¹⁶ + 2⁻³² + …), the first correction term and a carry of one are enough:

```cpp
constexpr uint32_t div65535(uint32_t x) {
    return (x + 1 + (x >> 16)) >> 16;      // ⌊x / 65535⌋ for every x ≤ 65535²
}
```

It is exact over the whole range a product of two 16-bit values can take, and at the top of that range the sum is 4,294,901,760, inside `uint32_t`. The true limit is a little higher — the first wrong answer is at 65535 · 65536, one too low — but 65535² is the bound the callers need, so it is the one in the comment. The library has it once, as `stripseq::detail::div65535` in `color.h`, where `lerp`, being `constexpr` in a public header, can reach it. How one knows "exact for every x" without trying four billion of them is §8.

The progress loop in §4 is the same idea in another form: instead of dividing by a number the hardware cannot handle, scale the problem until it is one it can.

## 7. Rounding a function you cannot compute

Some curves are not a ratio of integers at all. The gamma table in `src/color.cpp` needs, for each of 256 inputs, `round(255 · (in / 255)^2.5)` — a fractional power, which is irrational for almost every input. There is no floating point to compute it with, not even at compile time, and an integer square root followed by a division would floor twice, the trap in §5.

The way out is to stop computing the value and ask a yes-or-no question about it instead. Rounding to nearest is a pair of comparisons: `n = round(y)` exactly when `n − ½ ≤ y < n + ½`. So the answer is the largest `n` whose lower edge is still at or below `y`, which a loop can find by asking, for each candidate, whether `n + ½ ≤ y`. That question has a root in it, and the root can be squared away, because both sides are non-negative and squaring preserves their order:

```
n + ½ ≤ 255 · (in/255)^2.5
⇔  (2n + 1)² / 4 ≤ in⁵ / 255³        square both sides, collect 255s
⇔  (2n + 1)² · 255³ ≤ 4 · in⁵         multiply out the denominators
```

Every term is now an integer, and the question has an exact answer. That is the whole of `make_gamma_table()`: for each input, count up while `(2n + 1)² · 255³ ≤ 4 · in⁵`. Checked against `double` over all 256 inputs it disagrees nowhere, and the same search without the halves — `(n + 1)² · 255³ ≤ in⁵`, which finds the floor — is wrong at 111 of them (§10, Exercise 6).

Two costs remain, and both are §1 and §4 again. **The comparison has to fit.** `4 · 255⁵` is about 4.3 × 10¹², which needs `uint64_t`, and the bound is worth writing beside the code as for any intermediate. **The exponent decides whether it can fit at all.** An exponent `p/q` becomes a comparison of `q`-th powers: 2.5 = 5/2 needs squares and `in⁵`, but 2.2 = 11/5 needs fifth powers and `in¹¹`, and 255¹¹ is about 3 × 10²⁶ — far past 2⁶⁴. 2.8 = 14/5 is worse. That is part of why the library's curve is 2.5: of the exponents people actually use for LEDs, it is the one this method reaches in 64 bits.

Nothing here runs on the chip. The generator is `consteval` (see [`constant-initialisation-guide.md`](constant-initialisation-guide.md) §4), so the 64-bit products and the loop exist only inside the compiler, and the target holds 256 finished bytes.

## 8. Testing arithmetic exhaustively

Every curve here has 65,536 inputs. Checking all of them takes milliseconds, so there is no reason to pick samples, and a strong reason not to: the two-floor smoothstep in §5 passes at `t = 0`, `32768` and `65535`, and at nearly any handful of points anyone would think to choose.

So the tests state *properties* and check them over the whole domain:

- `tests/color.cpp` checks that `lerp` is exact at both ends and monotonic in `t`, rising and falling, for every one of the 65,536 values of `t` against a grid of channel values chosen at the edges — 0, 1, 254, 255, 256, 257, 32767, 32768, 65534, 65535 and a few others.
- `tests/sequence.cpp` runs a 65,535 ms fade through `Sequence::at()` for every easing and checks each one is monotonic and exact at both ends, through the same path a pixel takes.

Where the domain is too large to sweep — a signed 32-bit playhead — the tests take the boundary set instead: zero, ±1, each side of every step edge, `-L`, and `INT32_MIN`/`INT32_MAX`. §2 shows why the boundary set has to include the values that look silly.

A boundary set can also be a *proof*, when the function is monotonic. §6's `div65535` takes 4.3 billion inputs, and sweeping them takes seconds rather than milliseconds — affordable once, too slow for a suite that runs on every build. But `⌊x / 65535⌋` is a staircase: constant at `k` from `k · 65535` to `k · 65535 + 65534`, then a step up. And `div65535` never decreases, because each operation in it is non-decreasing in `x`. So if it equals `k` at both the bottom and the top of every stair, it is squeezed to `k` everywhere between. The bottom of stair `k` is `k · 65535`, and one below it is the top of stair `k − 1`, so `tests/color.cpp` checks exactly the two sides of every step, `f(k · 65535 − 1) == k − 1` and `f(k · 65535) == k` for every `k` from 1 to 65535: 131,070 checks, a few milliseconds, and nothing left untried. The monotonicity is what the argument stands on, so it is the claim to be sure of; here it is visible in the expression, and where it is not, test it.

## 9. The checklist

- **Every product of narrow types starts with a cast** to the width it needs: `uint32_t{x} * t`, never `x * t`.
- **Every remainder of a signed value is folded**: take it of the magnitude, unsigned, and mirror a non-zero one, `p < 0 && r != 0 ? L - r : r` (or `((p % L) + L) % L` where a signed remainder costs nothing).
- **Every negation of an arbitrary signed value is done in unsigned**: `0u - static_cast<uint32_t>(p)`.
- **Every intermediate is bounded in writing**: a comment that names the largest value it can take and the type that holds it.
- **Every check that guards a limit is done in a type that cannot wrap** below the limit's inputs.
- **Every rounded curve is one floor** of a function that is monotonic over its domain.
- **Every irrational value is rounded by comparison**, with the root squared away, never by an integer root and a second division.
- **No 64-bit division on a per-pixel path**: rescale until the divisor is a power of two, and confirm with `nm -u`.
- **No division by 2ⁿ − 1 where a shift form will do**: `(x + 1 + (x >> n)) >> n`, with the range it is exact over written beside it and tested at both sides of every step.
- **Every curve is tested over its whole domain**, and every signed input over its boundary set.

---

## 10. Exercises

Six. Exercise 5 breaks the library on purpose; the others are small standalone programs.

```sh
mkdir -p /tmp/stripseq-lab && cd /tmp/stripseq-lab
REPO=~/path/to/StripSequencer                 # wherever you cloned it
```

### Exercise 1 — Three wrong remainders and two right ones

*Demonstrates §2–§3.*

```cpp
// mod.cpp
#include <cstdint>
#include <cstdio>
#include <initializer_list>

int main() {
    const int32_t L = 1000;
    for (int32_t p : {-1, -1000, INT32_MIN}) {
        const uint32_t r = (0u - static_cast<uint32_t>(p)) % L;   // p < 0 throughout
        std::printf("p=%11d  p%%L=%5d  unsigned=%4u  mirrored=%5d  folded=%4d  magnitude=%4u\n",
                    p, p % L,
                    static_cast<uint32_t>(p) % L,
                    L - (-p % L),
                    ((p % L) + L) % L,
                    r != 0 ? L - r : r);
    }
}
```

```sh
c++ -std=c++20 -fsanitize=undefined mod.cpp -o mod && ./mod
```

```
mod.cpp:13:26: runtime error: negation of -2147483648 cannot be represented in type 'int32_t' (aka 'int'); cast to an unsigned type to negate this value to itself
p=         -1  p%L=   -1  unsigned= 295  mirrored=  999  folded= 999  magnitude= 999
p=      -1000  p%L=    0  unsigned= 296  mirrored= 1000  folded=   0  magnitude=   0
p=-2147483648  p%L= -648  unsigned= 648  mirrored= 1648  folded= 352  magnitude= 352
```

**Proves:** each wrong version is caught by a different playhead, and only the two folds are right at all three. The magnitude column is the mirrored one with its two faults removed — negated in unsigned, and a zero remainder left alone — and UBSan has nothing to say about it. UBSan finds the negation, because signed overflow is undefined; it would find nothing wrong with the unsigned column, which is defined and simply wrong.

### Exercise 2 — The lerp everyone writes first

*Demonstrates §1 (promotion) and §4.*

```cpp
// lerp.cpp
#include <cstdint>
#include <cstdio>

uint16_t naive(uint16_t x, uint16_t y, uint16_t t) {
    return static_cast<uint16_t>(x + (y - x) * t / 65535);
}

uint16_t weighted(uint16_t x, uint16_t y, uint16_t t) {
    return static_cast<uint16_t>((uint32_t{x} * (65535u - t) + uint32_t{y} * t) / 65535u);
}

int main() {
    std::printf("naive    %u\n", naive(0, 65535, 65535));
    std::printf("weighted %u\n", weighted(0, 65535, 65535));
}
```

```sh
c++ -std=c++20 -fsanitize=undefined lerp.cpp -o lerp && ./lerp
```

```
lerp.cpp:6:46: runtime error: signed integer overflow: 65535 * 65535 cannot be represented in type 'int'
naive    65534
weighted 65535
```

**Proves:** every operand was unsigned and the overflow is signed, because `uint16_t` promotes to `int`. And the naive form misses the endpoint it is asked for, at full scale, in the most ordinary blend there is.

### Exercise 3 — Find the 64-bit division

*Demonstrates §1 and §6.*

```cpp
// div.cpp
#include <cstdint>
uint16_t exact_smoothstep(uint16_t t) {
    const uint64_t x = t;
    return static_cast<uint16_t>(x * x * (3 * 65535 - 2 * x) / (uint64_t{65535} * 65535));
}
```

```sh
arm-none-eabi-g++ -std=c++20 -mcpu=cortex-m4 -mthumb -ffreestanding -Os -c div.cpp
arm-none-eabi-nm -u div.o
```

```
         U __aeabi_uldivmod
```

Now the library's own `Sequence::at()`, which contains the easing curves, the progress loop and two 32-bit remainders:

```sh
"$REPO/scripts/cross-check.sh" > /dev/null
arm-none-eabi-nm -u "$REPO/build-cross/CMakeFiles/stripseq.dir/src/sequence.cpp.obj"
```

No output: no undefined symbols at all. For contrast, a single 32-bit division on a Cortex-M0, which has no divide instruction:

```sh
echo 'unsigned f(unsigned a, unsigned b) { return a / b; }' |
    arm-none-eabi-g++ -mcpu=cortex-m0 -mthumb -Os -x c++ -c - -o m0.o && arm-none-eabi-nm -u m0.o
```

```
         U __aeabi_uidiv
```

**Proves:** the source of a 64-bit division looks exactly like any other division. Only the object file tells you the chip is about to call a library for it — and which division counts as expensive depends on the core.

### Exercise 4 — Two floors step backwards

*Demonstrates §5 and §8.*

```cpp
// smooth.cpp
#include <cstdint>
#include <cstdio>

uint16_t one_floor(uint16_t t) {
    const uint64_t x = t;
    return static_cast<uint16_t>(x * x * (3 * 65535 - 2 * x) / (uint64_t{65535} * 65535));
}

uint16_t two_floors(uint16_t t) {
    const uint32_t sq = uint32_t{t} * t / 65535;
    const uint32_t cube = sq * t / 65535;
    return static_cast<uint16_t>(3 * sq - 2 * cube);
}

uint16_t two_halves(uint16_t t) {
    if (t < 32768)
        return static_cast<uint16_t>(2 * uint32_t{t} * t / 65535);
    const uint32_t u = 65535 - t;
    return static_cast<uint16_t>(65535 - 2 * u * u / 65535);
}

void check(const char* name, uint16_t (*f)(uint16_t)) {
    int backwards = 0;
    uint32_t first = 0;
    for (uint32_t t = 1; t <= 65535; ++t) {
        if (f(static_cast<uint16_t>(t)) < f(static_cast<uint16_t>(t - 1)) && backwards++ == 0)
            first = t;
    }
    std::printf("%-10s ends %u..%u, steps backwards %d times", name, f(0), f(65535), backwards);
    if (backwards)
        std::printf(", first at t=%u (%u -> %u)", first,
                    f(static_cast<uint16_t>(first - 1)), f(static_cast<uint16_t>(first)));
    std::printf("\n");
}

int main() {
    check("one_floor", one_floor);
    check("two_floors", two_floors);
    check("two_halves", two_halves);
}
```

```sh
c++ -std=c++20 -O2 smooth.cpp -o smooth && ./smooth
```

```
one_floor  ends 0..65535, steps backwards 0 times
two_floors ends 0..65535, steps backwards 3655 times, first at t=1639 (120 -> 118)
two_halves ends 0..65535, steps backwards 0 times
```

Then pick five values of `t` you might have tested by hand and evaluate `two_floors` at them. It is very hard to choose five that expose it.

**Proves:** the same curve floored once and floored twice agree at both ends and look the same plotted, but only one of them flickers, and only a sweep over every input tells them apart. `two_halves`, the library's `EaseInOut`, needs no 64-bit division to stay monotonic. The whole sweep takes less time than compiling the program.

### Exercise 5 — Let the length check wrap

*Demonstrates §4: unsigned overflow is defined, so only a boundary test catches it.*

In `SequenceBuilder::build()` in `include/stripseq/sequence.h`, change the two `uint64_t` declarations — `loop` and `total` — to `uint32_t`.

```sh
cd "$REPO" && cmake --build build --target tests_default && ./build/tests_default -tc='TooLong*'
```

```
TEST CASE:  TooLong: totals that overflow uint32_t are still too long
tests/sequence.cpp:480: ERROR: CHECK( error_of(SequenceBuilder{}.hold(colors::red, 1u << 17).repeat(32768)) == Error::TooLong ) is NOT correct!
  values: CHECK( 0 == 7 )
tests/sequence.cpp:482: ERROR: CHECK( error_of(SequenceBuilder{}.hold(colors::red, 1u << 17).repeat(32769)) == Error::TooLong ) is NOT correct!
  values: CHECK( 0 == 7 )
tests/sequence.cpp:485: ERROR: CHECK( error_of(SequenceBuilder{}.hold(colors::red, 1u << 31).hold(colors::red, 1u << 31)) == Error::TooLong ) is NOT correct!
  values: CHECK( 8 == 7 )
[doctest] test cases:  3 | 2 passed | 1 failed | 34 skipped
```

**Revert it.** The values are `Error` enumerators: 7 is `TooLong`, 0 is `None` — a sequence of 2³² ms *accepted* — and 8 is `ZeroLengthLoop`. That last one is worth a second look: two steps of 2³¹ ms wrapped to an endless loop of 0 ms, and a different check caught it for the wrong reason. The other two `TooLong` cases, which never leave 32 bits, still pass.

**Proves:** the compiler, the sanitizers and the ordinary tests all accept a check that wraps. Only a test whose inputs are chosen to cross 2³² notices — so the bound has to be written down to know which inputs those are.

### Exercise 6 — Round a root without taking it

*Demonstrates §7.*

```cpp
// root.cpp
#include <cmath>
#include <cstdint>
#include <cstdio>

constexpr uint64_t cube = uint64_t{255} * 255 * 255;

// round(255 * (in / 255)^2.5): the largest n with n - 1/2 <= sqrt(in^5 / 255^3).
uint64_t rounded(uint64_t in) {
    const uint64_t limit = 4 * in * in * in * in * in;
    uint64_t n = 0;
    while (n < 255 && (2 * n + 1) * (2 * n + 1) * cube <= limit) ++n;
    return n;
}

// The same search with the halves left out: the largest n with n <= sqrt(...).
uint64_t floored(uint64_t in) {
    const uint64_t limit = in * in * in * in * in;
    uint64_t n = 0;
    while (n < 255 && (n + 1) * (n + 1) * cube <= limit) ++n;
    return n;
}

int main() {
    int round_off = 0, floor_off = 0;
    for (uint64_t in = 0; in < 256; ++in) {
        const long ref = std::lround(255.0 * std::pow(in / 255.0, 2.5));
        round_off += rounded(in) != static_cast<uint64_t>(ref);
        floor_off += floored(in) != static_cast<uint64_t>(ref);
    }
    std::printf("rounded disagrees with double at %d of 256 entries\n", round_off);
    std::printf("floored disagrees with double at %d of 256 entries\n", floor_off);
    std::printf("in=128: rounded %llu, floored %llu, double %.3f\n",
                (unsigned long long)rounded(128), (unsigned long long)floored(128),
                255.0 * std::pow(128 / 255.0, 2.5));
}
```

```sh
c++ -std=c++20 -O2 root.cpp -o root && ./root
```

```
rounded disagrees with double at 0 of 256 entries
floored disagrees with double at 111 of 256 entries
in=128: rounded 46, floored 45, double 45.521
```

Then change `rounded` to compute gamma 2.2 the same way — fifth powers of `2n + 1`, `in` to the eleventh — and look at what `limit` holds for `in = 255`.

**Proves:** an irrational value can be rounded exactly with nothing but integer multiplies, because rounding is a comparison and a comparison survives squaring. The halves are what make it round rather than floor, and the exponent decides whether the comparison fits in 64 bits at all.

---

## 11. The model in one paragraph

Without floating point, every value is an integer and every rounding is a floor, and the arithmetic goes wrong in a small number of recurring ways. C++'s remainder takes the sign of the dividend, so a signed position on a ring has to be folded back — most cheaply by taking the remainder of its magnitude, unsigned, and mirroring a non-zero one — and the cases that catch the near-misses are −1, −L and `INT32_MIN`. `INT32_MIN` cannot be negated in `int32_t`, but can be in `uint32_t`, where arithmetic is modular and defined. Narrow types promote to signed `int`, so products start with an explicit unsigned cast; intermediates are sized by writing down their largest value, which is what makes `lerp`'s weighted sum fit in 32 bits and pushes the length check into 64; and because unsigned wrap is defined, no sanitizer will catch a check that wraps — only a test at the boundary it crosses will. Rounding stays monotonic exactly when the result is one floor of a monotonic function, or a piecewise curve whose pieces each are and meet in order, while differences of separately floored terms can step backwards. On a Cortex-M a 64-bit multiply is an instruction and a 64-bit divide is a library call, so a division by an awkward constant is removed by rescaling until the divisor is a power of two, and `nm -u` confirms it is gone. A value that is irrational, like a fractional power, is rounded by asking a comparison rather than computing it: `n + ½ ≤ y` squares into a test on integers, exact as long as the products fit, which is what limits the exponents within reach. Every curve has only 65,536 inputs, so the properties are tested over all of them.

## 12. Where to go next

- **`src/sequence.cpp`** — `progress()`, `ease()` and `Sequence::at()`, about a hundred lines in which every section of this guide appears once, with the bound or the argument written beside it.
- **`include/stripseq/color.h`** — `Color::lerp()`, and `Color::rgb8()`'s ×257, the same "exact at both ends" argument applied to widening 8 bits to 16.
- **`tests/color.cpp` and `tests/sequence.cpp`** — §8's sweeps and boundary sets, as written.
- **`src/color.cpp`** — `convert()`, §6's rescaled knob in one line, and `make_gamma_table()`, §7 in twenty lines, and **[`gamma-and-color-guide.md`](gamma-and-color-guide.md)** for why the table exists and why its exponent is a choice about the eye.
- **[`cross-compiling-guide.md`](cross-compiling-guide.md) §5–§6** — `nm -u` as the general instrument for costs the compiler does not report, of which the 64-bit division is one more.
- **[`constant-initialisation-guide.md`](constant-initialisation-guide.md)** — the other half of the builder's story: the same builder, evaluated by the compiler instead of the chip.
- **The ARM run-time ABI (`IHI0043`, "Run-time ABI for the Arm Architecture")** — the specification that names `__aeabi_uldivmod`, `__aeabi_uidiv` and the rest, and so the list of what to grep for.
