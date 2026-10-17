# A position from a clock that wraps

*A from-first-principles guide to keeping time on a millisecond counter: deriving a position instead of counting it, why a 32-bit counter's wrap is harmless, what the real limit is, and the two ways to get past it for good.*

This is a teaching document, not project documentation. It explains how to turn a free-running millisecond counter into a position that never drifts, survives the counter rolling over, and stays correct however long the device runs, plus the one way to get each of those wrong that looks right. The running example is this repository's `Sequencer`: a few dozen lines that keep a playhead for an LED animation on a board nobody plans to reboot. It assumes [`integer-arithmetic-guide.md`](integer-arithmetic-guide.md) for two facts it uses without re-deriving: unsigned arithmetic is modular and defined, and signed overflow is undefined (§1 and §3 there). Beyond that, only C++ itself.

**The shape of this document:**

- **§1** — the choice everything else follows from: count a position, or derive it from a clock.
- **§2–§3** — the arithmetic of a 32-bit counter: why subtracting before converting makes the wrap disappear, and what the limit really is once it has.
- **§4** — the two ways past that limit, moving the origin or widening the type, and what each one costs.
- **§5–§6** — what widening leaves to do: narrowing the answer for the code that consumes it, and refusing the narrow type at compile time.
- **§7** — play and pause: two ways to hold a position, and why each call must do nothing in the state it produces.
- **§8** — testing sixty days in milliseconds.
- **§9** — the same pattern in Zephyr, Arduino, Linux and TCP.
- **§10** — the synthesis, as a checklist.
- **§11** — five exercises.
- **§12–§13** — the model in a paragraph, and where to go next.

---

## 1. Counting a position, or deriving it

An animation needs to know where it is: how many milliseconds into the sequence, which this library calls the *playhead*. There are two ways to keep one.

**Counting.** Store the playhead and advance it every frame: `playhead += 16` for a 60 Hz loop, or `playhead += now - last_now` if the loop measures its own frames. This is the obvious design, the one a tape deck uses, and it has three problems that all come from the same place. Each frame adds its own error, and the errors accumulate: a 60 Hz loop that adds 16 ms when 16.67 ms has passed falls a full second behind every 25 seconds (§11, Exercise 1). A dropped frame is lost time for good. And the playhead is state that some code path must update every frame, so a path that forgets — a paused render, an early return — moves it out of step.

**Deriving.** Store the clock reading at which the position was 0, the *origin*, and work the position out on every call:

```cpp
position = now - origin;
```

Nothing accumulates, because nothing is added up: the answer depends only on the clock reading in hand. A late frame shows the right position late; a dropped frame is simply never asked about. Two calls in the same millisecond agree. And there is no update for any path to forget, because there is nothing to update.

`Sequencer` derives. Its whole state is a pointer to the sequence it plays and three fields:

```cpp
int64_t         origin_ms_ = 0;   /* playing: the instant position 0 sits at */
int64_t         frozen_ = 0;      /* not playing: the position itself */
const Sequence* sequence_ = &no_sequence;   /* never null */
bool            playing_ = false;
```

While playing, `position(now)` derives the answer from `origin_ms_`. While paused, there is nothing to derive from — the clock moves and the position must not — so it stores the position itself in `frozen_`. §7 is about moving between the two. Everything before it is about the playing case, which is where the arithmetic lives.

Both fields are 64 bits wide, and why is the subject of §2–§4. The short answer is that a 32-bit counter has a limit that is easy to misplace, and §4 compares the two ways of getting past it.

## 2. Subtract, then convert

A 32-bit millisecond counter — `k_uptime_get_32()` under Zephyr, `millis()` on an Arduino, `HAL_GetTick()` on an STM32 — wraps: after 2³² − 1 = 4,294,967,295 ms, about 49.7 days, it reads 0 again. An origin taken shortly before the wrap is a large number, and a clock reading shortly after it is a small one. Take an origin of 4,294,967,000 and a reading of 200, 496 ms later:

```text
origin  4294967000   (296 ms before the wrap)
now            200   (200 ms after it)
```

The fix for this is no fix at all: subtract in `uint32_t`, and let the arithmetic be modular. `200 − 4294967000` modulo 2³² is 496, exactly the elapsed time, because modular subtraction does not care where zero is. The wrap is a point where the *representation* jumps, and a difference taken in the same modular arithmetic never sees it. The same holds for any two readings: as long as the true elapsed time is less than 2³² ms, the unsigned difference *is* the elapsed time.

A playhead is signed — a pixel delayed past the start has a negative one — so the difference has to become signed at some point. The order of the two steps is the whole lesson:

```cpp
const int32_t p = static_cast<int32_t>(now - origin);   // subtract, then convert
```

There are two other orders, and both are wrong in instructive ways (§11, Exercise 2):

- **Widen first:** `int64_t{now} - int64_t{origin}`. It looks safer, since nothing can overflow 64 bits, and that is exactly the problem. In 64-bit arithmetic there is no modular wrap to cancel the clock's, so across the wrap the answer is −4,294,966,800: the clock's 2³² jump, faithfully preserved.
- **Convert first:** `int32_t(now) - int32_t(origin)`. This one usually gives the right answer, which makes it worse. Converting a reading past 2³¹ to `int32_t` makes it negative, so the conversion has its own wrap, at 2³¹ rather than 2³². Across *that* point the subtraction of the two signed values overflows `int32_t`: undefined behaviour. On today's compilers and hardware it happens to produce 496 anyway, so no test sees it, and only UBSan does.

Subtracting first leaves no wrap to cross and nothing undefined: one modular subtraction, then one conversion, which C++20 defines as modular too.

This library takes no 32-bit reading, but the idiom has not gone anywhere; it has moved to the one place a 32-bit counter is turned into a 64-bit one. A caller with only a wrapping tick extends it like this, and the subtraction inside is §2 exactly:

```cpp
/* Application code: call at least once per 49.7 days. */
struct Clock64 {
    uint32_t last = 0;
    int64_t  total = 0;
    int64_t operator()(uint32_t now) {
        total += static_cast<uint32_t>(now - last);   /* the wrap cancels here */
        last = now;
        return total;
    }
};
```

## 3. The real limit is elapsed time, not the clock

With §2 in place, the clock's wrap at 49.7 days is a non-event. But a limit has not gone away; it has moved.

The unsigned difference is correct for any elapsed time under 2³² ms. The conversion to `int32_t` keeps it correct only under 2³¹ ms, about **24.8 days**. One millisecond past that, a playhead that should read 2,147,483,648 converts to −2,147,483,648 — and to an animation, a large negative playhead means "not started yet". On a finite sequence that is the color it shows before it starts. On this library's Wipe recipe, a strip that swept to green and is meant to stay green would go black after 24.8 days, and the difference would come back round to small positive values and wipe again at 49.7 (§11, Exercise 3).

So the question to ask of any code like this is not "does it survive the clock wrapping?" — subtraction handles that — but **"what is the largest elapsed time this difference will ever hold?"** If it is bounded by something shorter than 24.8 days, by design rather than by hope, there is nothing more to do. A sequencer that plays for as long as the device is powered has no such bound unless it makes one.

RFC 1982 states the same rule for sequence numbers, the general form of a clock reading: two values on a modular counter can be compared correctly only when they are less than half the counter's range apart. Half the range of a 32-bit counter is 2³¹, which is why the limit is 24.8 days and not 49.7.

## 4. Two ways past the limit

The difference grows without bound for two reasons together: the origin stays put, and the type is too narrow for how long the device runs. Remove either and the limit goes. Each removal is a design in its own right, and they cost different things.

### Moving the origin

Keep 32 bits, and move the origin forward so the elapsed time never grows — in a way nobody downstream can observe. There is one such move for each kind of sequence.

*An endless sequence: re-base by whole loops.* An endless sequence repeats every `loop` milliseconds, and `Sequence::at()` treats its timeline as a ring, so a playhead of 1730 on a 500 ms loop is the same moment as 230. Whenever the derived playhead reaches the loop length, move the origin forward by as many whole loops as fit and return the remainder:

```cpp
const uint32_t into_loop = static_cast<uint32_t>(p) % loop;
origin += static_cast<uint32_t>(p) - into_loop;
return static_cast<int32_t>(into_loop);
```

The origin and the answer move by the same whole number of loops, so every pixel sees exactly what it would have seen, and the origin never trails the clock by more than a loop plus the gap since the last call.

*A finite sequence: cap it.* A finite sequence cannot be re-based, because its position is absolute: 1730 ms into three 500 ms loops and a landing is not the same moment as 230. But once it is far enough past the end, every pixel shows the landing color, so it only has to *stay* there: when the playhead passes some cap, move the origin to `now - cap` and return the cap. Clamping only the answer — `return p > cap ? cap : p` — is a trap here, because it leaves the origin where it was: the 32-bit difference underneath keeps growing, and at 24.8 days the conversion in §2 turns it negative before the clamp ever sees it.

Both moves change state while answering a question, and that has consequences. The query cannot be `const`. Something must ask at least once every 24.8 days. And every point where the state *starts* following the clock has to make the move too, not only the next query: seek to `INT32_MAX − 5` and play, and a difference six milliseconds from overflowing is handed to a `playhead()` that may not be asked for a whole frame. **An invariant maintained "on every call" has to be established at every point where the state enters the regime it protects.** All of it is manageable, and all of it is reasoning that has to be done again for every new way into the state.

### Widening the type

Take the clock as `int64_t` milliseconds. A difference of two such readings overflows after about 292 million years, so in any span a device runs for, the elapsed time simply fits. Nothing moves, nothing has to be asked often enough, no transition needs special handling, and the query is `const`.

What it costs:

- **Arithmetic.** A 64-bit subtraction is two instructions on a 32-bit core. A 64-bit division is not an instruction at all on a Cortex-M but a call into the compiler's runtime library, and §5 needs a 64-bit remainder per query on an endless sequence. The library takes it in 32-bit pieces instead, one division and a shift-and-subtract per bit of the low word, which is about a hundred cycles once per segment per frame and links nothing.
- **Space.** Two 8-byte fields where there were two 4-byte ones.
- **The wrap moves to the edge.** A counter that wraps still wraps; widening it is someone's job. Under Zephyr that job is already done, because `k_uptime_get()` is 64-bit. A caller with only a 32-bit tick runs it through §2's `Clock64`, and the "at least once every 24.8 days" condition becomes "at least once every 49.7 days", on the extender rather than inside the library.

This library widens. Moving the origin works, but its reasoning touches every entry point and every limit around it, and the reasoning has to be redone whenever one changes. Widening replaces all of it with one decision at the edge, and leaves two smaller jobs, which are the next two sections: the consumers of the position still want 32 bits, and the edge has to be guarded against the one mistake widening makes easy.

## 5. Narrowing the answer, not the difference

A wide position is exact, but the code that consumes it works in `int32_t`. Each pixel staggers the sequencer's playhead by its own offset, and that per-pixel arithmetic is the hot loop, which is no place for 64-bit operations. So `Sequencer::playhead()` hands out the position *narrowed*, into a range that any accepted stagger can be added to or taken from:

```cpp
if (sequence_.total_duration_ms() == hold_forever) {
    const uint32_t loop = sequence_.loop_duration_ms();
    if (loop == 0)
        return 0;
    const uint64_t magnitude = p < 0 ? 0 - static_cast<uint64_t>(p) : static_cast<uint64_t>(p);
    const uint32_t into_loop = remainder(magnitude, loop);
    return static_cast<int32_t>(p < 0 && into_loop != 0 ? loop - into_loop : into_loop);
}
const int64_t clamped = p < -finite_limit_ms ? -finite_limit_ms
                      : p > finite_limit_ms  ? finite_limit_ms
                                             : p;
return static_cast<int32_t>(clamped);
```

Both are exact through `at()`, which is what lets them apply on every path. An endless sequence's playhead is the position modulo the loop, which is what `at()` would read it as anyway; the remainder is of the magnitude and folded back up because C++'s `%` takes the sign of the dividend ([`integer-arithmetic-guide.md`](integer-arithmetic-guide.md) §2–§3); `remainder()` is the 64-bit remainder in 32-bit pieces that §4 counted the cost of. A finite sequence's is the position clamped to 2³⁰ ms, about 12.4 days, either side of zero: everything past the end shows the landing color and everything before the start shows the start color, so a value far out stands for every value beyond it. A sequence with no steps has a loop of zero and answers 0, since the remainder would divide by zero and `at()` answers black whatever it is asked.

Note that the clamp is the very thing §4 called a trap. It is safe here for one reason: the difference underneath is `int64_t`, so it cannot have gone wrong before the clamp sees it. **Clamping an answer is sound exactly when the value it clamps was computed in a type that could not overflow on the way** (§11, Exercise 3 breaks that and watches the trap return).

Why 2³⁰, and not the sequence's own total? Because a segment of pixels is *staggered*: its last pixel runs some fixed delay behind the sequencer, so it asks `at()` about the playhead minus that stagger, and a playhead clamped at the total would leave every staggered pixel short of the end forever. The sequencer does not know which segments read from it, so it clamps at a constant chosen to clear every case at once. The builder refuses any total over 2²⁹ ms, and the segment's overflow guard keeps every stagger within 2²³ ms either way — a stagger is a pixel index times a delay that must fit in an `int32_t` with 8 fractional bits. So 2³⁰ − 2²³ is still well past 2²⁹, 2³⁰ + 2²³ is still inside `int32_t`, and the same holds in mirror image below zero. Two limits enforced elsewhere are what make the constant exact, and the test called "a narrowed playhead plus or minus any stagger is still exact" writes that arithmetic down where a change to either limit would break it.

Narrowing applies to the answer, never to what is stored. `pause()` records `position(now)`, not `playhead(now)`, so the stored value is exact however far out it is, and a position sought to −2³¹ plays in from −2³¹ rather than from the −2³⁰ it reads as.

## 6. Refusing the narrow type

Widening at the edge has one way to go wrong that nothing warns about. A `uint32_t` passed where an `int64_t` is wanted converts implicitly, and the conversion is value-preserving, so `-Wconversion` has nothing to say. But it is §2's *widen first*: the reading arrives without its wrap cancelled, and 49.7 days in, the position jumps back by 2³² ms. On the Wipe recipe the strip goes black (§11, Exercise 5).

So every function that takes `now_ms` declares a second overload, and deletes it:

```cpp
template <typename T>
concept wrapping_tick = requires(T t) { t %= t; } && T(-1) > T(0) && sizeof(T) <= 4;

void play(int64_t now_ms);
template <wrapping_tick T> void play(T) = delete;
```

A deleted function still takes part in overload resolution; choosing it is what makes the call an error. For a `uint32_t` argument the template deduces `T = uint32_t` as an exact match, which beats the conversion to `int64_t`, and the call fails to compile, naming the deleted candidate. For an `int64_t` the non-template is the exact match. For an `int` literal like `play(1000)` the constraint fails, so the template is not a candidate at all and the literal converts to `int64_t` as it always did.

Two simpler spellings look right and are not. A plain `void play(uint32_t) = delete;` makes `play(1000)` ambiguous, since `int` converts to `uint32_t` and to `int64_t` with equal rank. And naming `uint32_t` at all misses the point on some target: it is `unsigned int` on the host and `unsigned long` on arm-none-eabi, so the concept asks about the property — unsigned, at most 32 bits — rather than the typedef.

The property is asked of the type directly, with no header at all. A concept is a language feature, and `concept` needs no include; what `<concepts>` and `<type_traits>` provide are ready-made answers, and the library includes neither, because Zephyr's minimal C++ library ships neither. Both halves of "unsigned integer" have definitions the language can check by itself. An integer type is one with `%=`: a floating-point type, a pointer and a class without the operator have none, and an enum has none either, since `t % t` promotes to `int` and an `int` does not assign back to an enum. Inside a `requires`-expression an operation that does not compile is simply false. An unsigned type is one in which −1 converts to the largest value, so `T(-1) > T(0)`; in a signed one it stays −1. Together they answer what `std::is_integral_v<T> && std::is_unsigned_v<T>` answers for every fundamental type — `bool` and `char` included, the latter unsigned on arm-none-eabi and signed on the host.

The order of the two is not cosmetic. A constraint is a conjunction checked left to right, and the right side is never looked at once the left is false. That matters because the two failures are different kinds of thing. `t %= t` not compiling is a *substitution failure*: the constraint is unsatisfied and the deleted overload drops out quietly. `T(-1)` for a pointer does compile — it is a `reinterpret_cast` — but it is not a *constant expression*, and an atomic constraint that is not a constant expression makes the program ill-formed outright. Put `T(-1) > T(0)` first and `play(some_pointer)` stops saying "no conversion to `int64_t`" and starts reporting an error inside the library's concept instead. The one thing the standard concepts would add is a shared vocabulary for *subsumption*: when two overloads are both constrained, the one whose constraint implies the other's wins, and the compiler decides that by matching the pieces each constraint is built from. Here the deleted template only ever competes with a plain `int64_t` function, so there is nothing to order.

The guard is tested the way any "this must not compile" is tested when the failure is a substitution failure: a `requires`-expression asks whether the call is well-formed, and a `static_assert` insists on the answer.

```cpp
template <typename T>
concept plays_with = requires(Sequencer s, T t) { s.play(t); };
static_assert(!plays_with<uint32_t>, "k_uptime_get_32() must not compile");
static_assert(plays_with<int>, "a literal such as 1000 must still be accepted");
```

A call that selects a deleted function is ill-formed in the requirement's immediate context, which makes the requirement false rather than an error, so the test file can ask the question and still compile. A failure that only shows during constant evaluation, like `build_const()`'s, cannot be asked about this way, which is why that one needs a compiler run of its own ([`testing-guide.md`](testing-guide.md) §7).

## 7. Two representations, one bit

A paused position does not follow the clock, so it cannot be derived from one; `frozen_` holds it directly. That gives the sequencer two representations of one value and one bit, `playing_`, saying which is current:

| Call | Stores | Effect |
| --- | --- | --- |
| `play(now)` | `origin_ms_ = now - frozen_` | Picks the origin that puts the position where it stood. |
| `pause(now)` | `frozen_ = position(now)` | Records where the position is, and keeps it there. |
| `seek(p)` | `frozen_ = p` | Sets it; no clock involved. |

`play` and `pause` take `now` because each converts between a value and an origin; `seek` only ever sets a value. Worked through, with clock time on the left:

```text
t=1000  play(1000)    origin = 1000 - 0    = 1000   playhead(1300) = 300
t=1500  pause(1500)   frozen = 1500 - 1000 = 500    playhead(9999) = 500    stays put
t=4000  play(4000)    origin = 4000 - 500  = 3500   playhead(4200) = 700    carries on from 500
```

A 2.5-second pause costs nothing: the new origin sits 500 ms in the past, so the position resumes at exactly the value it was paused at.

Each conversion reads the field the *other* state keeps current, and that is the trap. While playing, `frozen_` is stale — it still holds whatever the last pause or seek left. So `play()` called while already playing would rebuild the origin from that stale value, and the playhead would jump back to it. A redundant `play()` is not exotic: any code that calls it "to make sure" does it, on every frame. So `play()` while playing must do nothing (§11, Exercise 4):

```cpp
if (playing_)
    return;
```

`pause()` while paused has the mirror-image problem in principle — `origin_ms_` is stale while paused — but here it needs no guard, because `pause()` records `position(now)` rather than computing `now - origin_ms_` itself, and while paused `position()` answers `frozen_` without reading the origin at all. Pausing again stores `frozen_` into `frozen_`. Routing every conversion from a clock reading to a position through the one function that knows how is what made the guard unnecessary, and is the better habit of the two.

There is one bit here, not a state machine. "Stopped" is `seek(0)`; "resume" is `play`; "finished" is a property of the playhead and the sequence, worked out when someone asks. The obvious extra states are all derivable, and a derivable state stored separately is one more thing that can disagree with what it was derived from.

## 8. Testing sixty days in milliseconds

None of this can be tested by waiting. It can be tested at all because the clock is an *argument*: `playhead(now_ms)` reads no hardware, so a test can hand it any reading it likes, in any order it likes — including day 60, in one call. Taking the time as a parameter rather than reading it inside is what makes time-dependent code testable, and it is worth designing for from the start.

Three habits make such tests trustworthy.

**Ask about the edges, not about every millisecond.** The interesting positions are few and known in advance: either side of zero, one loop and a few, either side of the finite limit, and the points where an `int32_t` and a `uint32_t` would run out — 2³¹ and 2³² ms, about 24.8 and 49.7 days — plus some far beyond. `tests/sequencer.cpp` puts them in one table and asks both kinds of sequence about every entry. Where a property is local — no discontinuity across 2³¹ or 2³² — the test goes to single milliseconds, but only over the two thousand around the edge.

**Compare against a reference that cannot have the bug.** "Is the narrowing exact?" is answered by asking `at()` about the narrowed playhead and about the exact position, wherever an `int32_t` can hold the position, and requiring the same color and hold. A reference that shares the implementation's arithmetic shares its mistakes; one done the slow obvious way, in a type that cannot overflow, does not. The 777 ms loop the tests use divides neither a power of two nor a day, so a narrowing that is off by some whole number of 2³² ms cannot land back in phase by accident.

**Know whether the order of questions matters.** A `const` query has no order: day 59 asked after day 60 gets the same answer as before it, because nothing changed in between. Code that moves its origin while answering is different — a question about an earlier instant gets an answer measured from the *new* origin — and a test of such code must keep its simulated clock moving forward, writing its schedule in elapsed times where later is larger, because once a 32-bit clock has wrapped, readings are no longer ordered by size.

## 9. The same pattern elsewhere

Nothing here is specific to LED strips; every system with a free-running counter meets it.

- **Zephyr**, against v4.4.1. `k_uptime_get()` (`include/zephyr/kernel.h:2147`) is the 64-bit uptime in milliseconds, exactly the clock this library takes, and the one the sample's frame loop samples once per frame. `k_uptime_get_32()` (`include/zephyr/kernel.h:2171`) is its low 32 bits, and its own documentation advises against it: it computes the full 64-bit uptime anyway and truncates it, so there is "no benefit to using this over k_uptime_get() unless you know the application will never run long enough for the system clock to approach 2^32 ticks". This library refuses it at compile time (§6). `k_uptime_delta()` (`include/zephyr/kernel.h:2200`) works in `int64_t` throughout, the same choice §4 makes. The PTP sample measures its run time with the §2 idiom on the 32-bit clock, `(k_uptime_get_32() - uptime) / 1000` (`samples/net/ptp/src/main.c:83`), which is sound for a span of seconds. And `k_cycle_get_32()` (`include/zephyr/kernel.h:2219`) is the same problem on a much shorter fuse: a 32-bit counter of hardware cycles at a few hundred MHz wraps in seconds, so §2's subtraction is mandatory and §3's bound on elapsed time is measured in seconds too.
- **Arduino.** `millis()` is a 32-bit millisecond counter, and the idiom every sketch is told to use is `if (millis() - start >= interval)` — subtract, then compare — rather than `if (millis() >= start + interval)`, which breaks at the wrap. It is §2 exactly.
- **Linux.** `jiffies` is a wrapping tick counter, and `time_after(a, b)` in `include/linux/jiffies.h` compares two readings as `(long)((b) - (a)) < 0`: subtract in unsigned, then convert to signed, valid while they are under half the range apart. That is §2 and §3 in one macro. The kernel also keeps `jiffies_64`, and reaching for it is §4's other answer.
- **TCP and DNS.** Sequence numbers and zone serials are modular counters, and RFC 1982, "Serial Number Arithmetic", defines comparing them the same way, including the half-range rule. Those counters cannot be widened, because their width is on the wire, which is why the rule exists at all.

The common thread: **never compare readings, compare differences, and know the largest difference you will ever take.** Then decide whether to keep it small or make room for it.

## 10. The checklist

- **Derive positions from an origin**; store a position only while it is not following the clock.
- **On a wrapping counter, subtract readings in the counter's own unsigned type, then convert** the difference. Never widen a reading first; never convert first.
- **Name the largest elapsed time** the difference can hold, and check it is under half the counter's range.
- **If nothing bounds it, choose:** move the origin — by whole periods for anything periodic, to a constant cap for anything that ends, at every entry into following the clock — or widen the type until the bound is the device's lifetime, and extend the narrow counter once, at the edge.
- **Narrow the answer, not what is stored**, and only once the value being narrowed cannot have overflowed on the way.
- **Refuse the narrow type at compile time** once the wide one is chosen; an implicit widening is silent, and it is §2's wrong order.
- **Each transition does nothing in the state it produces**, because it reads the field the other state keeps current.
- **Take the clock as a parameter**, and test at the edges with a reference that cannot share the bug.

---

## 11. Exercises

Five. The first two are small standalone programs; Exercises 3 and 4 break the library on purpose and watch its tests catch it; Exercise 5 compiles against it.

```sh
mkdir -p /tmp/stripseq-lab && cd /tmp/stripseq-lab
REPO=~/path/to/StripSequencer                 # wherever you cloned it
```

### Exercise 1 — A counted playhead drifts

*Demonstrates §1.*

```cpp
// drift.cpp
#include <cstdint>
#include <cstdio>

int main() {
    const uint32_t origin = 1000;
    uint32_t now = origin;
    int32_t counted = 0;                       // advanced by one frame period per frame
    for (int frame = 1; frame <= 216000; ++frame) {
        now += (frame % 3 == 0) ? 16 : 17;     // a 60 Hz loop: 16.67 ms on average
        if (frame % 5000 == 0)
            now += 16;                         // and now and then, a dropped frame
        counted += 16;
        if (frame == 60 || frame == 3600 || frame == 216000)
            std::printf("frame %6d  counted %8d  derived %8d  behind by %5d ms\n", frame,
                        counted, static_cast<int32_t>(now - origin),
                        static_cast<int32_t>(now - origin) - counted);
    }
}
```

```sh
c++ -std=c++20 drift.cpp -o drift && ./drift
```

```
frame     60  counted      960  derived     1000  behind by    40 ms
frame   3600  counted    57600  derived    60000  behind by  2400 ms
frame 216000  counted  3456000  derived  3600688  behind by 144688 ms
```

Change `counted += 16` to add the measured frame time instead, and the steady drift goes — but now the counter is a second copy of what `now - origin` already says, and it is right only for as long as every path that renders a frame remembers to update it.

**Proves:** a counted position integrates every frame's error, and after an hour at 60 Hz it is more than two minutes out. The derived one has no error to integrate, because it is computed from the clock and nothing else: one hour plus the 43 dropped frames, to the millisecond.

### Exercise 2 — Subtract first, widen first, convert first

*Demonstrates §2.*

```cpp
// wrap.cpp
#include <cstdint>
#include <cstdio>
#include <initializer_list>

int main() {
    struct Reading { uint32_t origin, now; };
    for (Reading r : {Reading{1000, 1496},                  // nothing special
                      Reading{4294967000u, 200},            // across the 2^32 wrap
                      Reading{2147483400u, 2147483896u}}) { // across 2^31
        const int32_t subtract_first = static_cast<int32_t>(r.now - r.origin);
        const int64_t widen_first = int64_t{r.now} - int64_t{r.origin};
        const int32_t cast_first = static_cast<int32_t>(r.now) - static_cast<int32_t>(r.origin);
        std::printf("origin %10u  now %10u  subtract first %4d  widen first %11lld  cast first %d\n",
                    r.origin, r.now, subtract_first, static_cast<long long>(widen_first), cast_first);
    }
}
```

```sh
c++ -std=c++20 -fsanitize=undefined wrap.cpp -o wrap && ./wrap
```

```
origin       1000  now       1496  subtract first  496  widen first         496  cast first 496
origin 4294967000  now        200  subtract first  496  widen first -4294966800  cast first 496
wrap.cpp:13:64: runtime error: signed integer overflow: -2147483400 - 2147483400 cannot be represented in type 'int32_t' (aka 'int')
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior wrap.cpp:13:64
origin 2147483400  now 2147483896  subtract first  496  widen first         496  cast first 496
```

Every reading is 496 ms after its origin. Rebuild without `-fsanitize=undefined` and the third line prints `cast first 496` with no complaint at all.

**Proves:** the wrap is harmless only to modular arithmetic. Widening first preserves the clock's jump instead of cancelling it, and converting first has a wrap of its own at 2³¹ where the subtraction is undefined — and gives the right answer anyway on this machine, so only the sanitizer can tell it apart from the correct order.

### Exercise 3 — Narrow the difference instead of the answer

*Demonstrates §3–§5.*

In `Sequencer::position()` in `src/sequencer.cpp`, squeeze the difference through 32 bits before the narrowing ever sees it:

```cpp
    return playing_ ? static_cast<int32_t>(now_ms - origin_ms_) : frozen_;
```

```sh
cd "$REPO" && cmake --build build --target tests_default && ./build/tests_default -tc='a finite sequence left playing*'
```

```
TEST CASE:  a finite sequence left playing for 60 days stays on its landing color

tests/sequencer.cpp:373: ERROR: CHECK( wipe.at(p).color == colors::green ) is NOT correct!
  values: CHECK( rgb(0, 0, 0) == rgb(0, 65535, 0) )
  logged: day := 25

tests/sequencer.cpp:374: ERROR: CHECK( wipe.at(p).hold_ms == hold_forever ) is NOT correct!
  values: CHECK( 1073741824 == 4294967295 )
  logged: day := 25

tests/sequencer.cpp:373: ERROR: CHECK( wipe.at(p).color == colors::green ) is NOT correct!
  values: CHECK( rgb(0, 0, 0) == rgb(0, 65535, 0) )
  logged: day := 49

tests/sequencer.cpp:374: ERROR: CHECK( wipe.at(p).hold_ms == hold_forever ) is NOT correct!
  values: CHECK( 61367096 == 4294967295 )
  logged: day := 49

tests/sequencer.cpp:376: ERROR: CHECK( seq.playhead(777777 + 60 * ms_per_day) == limit_ms ) is NOT correct!
  values: CHECK( 889032704 == 1073741824 )
```

Day 24 is still green and day 25 is black, its start color, holding for 2³⁰ ms: the clamp's floor, since the position underneath is now a large negative number. Day 49 is black too, 61 million ms from starting again, and day 50 is green: §3's "wipe again at 49.7", to the day. Run the whole suite and five other cases fail with it, among them the table of §8. **Revert it.**

**Proves:** a clamp on the answer protects nothing once the value it tests has already wrapped. The same clamp is exact when the difference underneath is computed in a type that cannot overflow.

### Exercise 4 — A redundant `play()` that restarts

*Demonstrates §7.*

In `Sequencer::play()`, change `if (playing_)` to `if (false)`.

```sh
cmake --build build --target tests_default && ./build/tests_default -tc='play while playing*'
```

```
TEST CASE:  play while playing and pause while paused change nothing

tests/sequencer.cpp:254: ERROR: CHECK( seq.playhead(1700) == 700 ) is NOT correct!
  values: CHECK( 0 == 700 )

tests/sequencer.cpp:255: ERROR: CHECK( seq.playhead(2000) == 1000 ) is NOT correct!
  values: CHECK( 300 == 1000 )

tests/sequencer.cpp:258: FATAL ERROR: REQUIRE( seq.playhead(2500) == 1500 ) is NOT correct!
  values: REQUIRE( 800 == 1500 )
```

**Revert it.** The second `play()` rebuilt the origin from `frozen_`, still 0 from the `load()` before the first one, so the playhead jumped back to the start.

**Proves:** a transition that reads the other state's field must do nothing in its own state. Unguarded, a call that looks idempotent is a restart.

### Exercise 5 — Hand the library a 32-bit tick

*Demonstrates §2 and §6.*

Back in the lab directory, a program that plays the Wipe recipe's sequence from a 32-bit tick taken 100 ms before it wraps:

```cpp
// tick.cpp
#include <cstdint>
#include <cstdio>
#include <initializer_list>

#include <stripseq/sequence.h>
#include <stripseq/sequencer.h>

using namespace stripseq;

constexpr Sequence wipe = SequenceBuilder{}
                              .start_color(colors::black)
                              .fade_to(colors::green, 200)
                              .landing_color(colors::green)
                              .repeat(1)
                              .build_const();

/* The cookbook's extender: call at least once per 49.7 days. */
struct Clock64 {
    uint32_t last = 0;
    int64_t  total = 0;
    int64_t operator()(uint32_t now) {
        total += static_cast<uint32_t>(now - last);   /* the wrap cancels here */
        last = now;
        return total;
    }
} clock64;

int64_t reading(uint32_t tick) {
#ifdef EXTEND
    return clock64(tick);
#else
    return int64_t{tick};               // widened by hand, which compiles
#endif
}

int main() {
    Sequencer seq;
    seq.load(wipe);

    const uint32_t start = UINT32_MAX - 100;   // a 32-bit tick, 100 ms before it wraps
#ifdef PASS_THE_TICK
    seq.play(start);
#else
    seq.play(reading(start));
#endif
    for (uint32_t later : {50u, 100u, 101u, 400u}) {
        const int32_t p = seq.playhead(reading(start + later));
        std::printf("%3u ms later  playhead %11d  %s\n", later, p,
                    p < 0 ? "black, not started" : p < 200 ? "fading in" : "green, landed");
    }
}
```

First, pass the tick straight in:

```sh
cd /tmp/stripseq-lab
c++ -std=c++20 -I"$REPO/include" -DPASS_THE_TICK -fsyntax-only tick.cpp
```

```
tick.cpp:43:9: error: call to deleted member function 'play'
   43 |     seq.play(start);
      |     ~~~~^~~~
.../include/stripseq/sequencer.h:57:37: note: candidate function [with T = unsigned int] has been explicitly deleted
   57 |     template <wrapping_tick T> void play(T) = delete;
      |                                     ^
```

Then widen it by hand, which the library cannot stop, and then extend it:

```sh
c++ -std=c++20 -I"$REPO/include" -I"$REPO/src" tick.cpp "$REPO"/src/*.cpp -o tick && ./tick
c++ -std=c++20 -I"$REPO/include" -I"$REPO/src" -DEXTEND tick.cpp "$REPO"/src/*.cpp -o tick && ./tick
```

```
 50 ms later  playhead          50  fading in
100 ms later  playhead         100  fading in
101 ms later  playhead -1073741824  black, not started
400 ms later  playhead -1073741824  black, not started
```

```
 50 ms later  playhead          50  fading in
100 ms later  playhead         100  fading in
101 ms later  playhead         101  fading in
400 ms later  playhead         400  green, landed
```

At 101 ms the widened tick has wrapped to 0, the position is 2³² ms in the past, and the clamp shows its floor. Through `Clock64` the same ticks are one continuous count.

**Proves:** widening a wrapping reading keeps its wrap, and nothing in the language warns about it, so the guard has to be in the API. What it catches is the implicit conversion; an explicit one is a decision, and the extender is the only correct place to make it.

---

## 12. The model in one paragraph

A position that follows a clock should be derived from it, not counted: store the reading at which the position was zero, and answer `now - origin` on every call, so there is no accumulated error, no lost frame and no update to forget. On a wrapping unsigned counter, that subtraction is done in the counter's own modular arithmetic and only then converted to signed, and the counter's wrap cancels out of it entirely; widening a reading first preserves the jump, and converting first introduces a second wrap at half the range where the subtraction is undefined. What remains is a limit on the *difference*: under half the counter's range, 24.8 days for 32-bit milliseconds. There are two ways past it. Moving the origin keeps the difference small — by whole periods for anything periodic, to a constant cap for anything that ends — but changes state while answering and must be redone at every entry into following the clock. Widening the type to 64 bits makes the limit the device's lifetime, at the cost of 64-bit arithmetic and of extending the narrow counter once at the edge, where §2's subtraction lives on. With a wide position, the code that consumes it still gets a narrow answer, and clamping or reducing that answer is exact precisely because the value underneath could not overflow on its way; and since an implicit widening of a 32-bit reading is silent, a deleted overload refuses it at compile time. Holding a position still needs a second representation, so the state is one bit choosing between two fields, and each transition must do nothing in the state it produces, because it reads the field the other state keeps current. All of it is testable in milliseconds because the clock is a parameter: ask about the edges, compare against a reference that cannot share the bug, and know whether the order of questions matters.

## 13. Where to go next

- **`src/sequencer.cpp`** — §4–§7 as written: the wide position, the narrowing, and the one guard.
- **`include/stripseq/sequencer.h`** — `wrapping_tick` and the deleted overloads of §6.
- **`tests/sequencer.cpp`** — §8 as written: the table of positions, the continuity runs across 2³¹ and 2³², the `static_assert`s of §6, and a case for each section of this guide.
- **[`integer-arithmetic-guide.md`](integer-arithmetic-guide.md)** — the modular and signed arithmetic underneath §2, and how `Sequence::at()` folds a negative playhead onto the ring.
- **[`testing-guide.md`](testing-guide.md) §8** — what the test suite for this code could and could not see: a bug that fails on one machine for the wrong reason, crashes on another, and faults the target.
- **RFC 1982, "Serial Number Arithmetic"** — the half-range rule of §3, stated for sequence numbers, in eight pages.
