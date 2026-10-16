# A position from a clock that wraps

*A from-first-principles guide to keeping time on a 32-bit millisecond counter: deriving a position instead of counting it, why the counter's wrap is harmless, what the real limit is, and how to stay inside it forever.*

This is a teaching document, not project documentation. It explains how to turn a free-running millisecond counter into a position that never drifts, survives the counter rolling over, and stays correct however long the device runs, plus the one way to get each of those wrong that looks right. The running example is this repository's `Sequencer`: about forty lines that keep a playhead for an LED animation, fed by a clock that wraps every 49.7 days and runs on a board nobody plans to reboot. It assumes [`integer-arithmetic-guide.md`](integer-arithmetic-guide.md) for two facts it uses without re-deriving: unsigned arithmetic is modular and defined, and signed overflow is undefined (§1 and §3 there). Beyond that, only C++ itself.

**The shape of this document:**

- **§1** — the choice everything else follows from: count a position, or derive it from a clock.
- **§2–§3** — the arithmetic: why subtracting before converting makes the wrap disappear, and what the limit really is once it has.
- **§4–§5** — staying inside the limit forever by moving the origin, and the moment the move has to happen.
- **§6** — play and pause: two ways to hold a position, and why each call must do nothing in the state it produces.
- **§7** — testing sixty days in milliseconds.
- **§8** — the same pattern in Zephyr, Arduino, Linux and TCP.
- **§9** — the synthesis, as a checklist.
- **§10** — five exercises.
- **§11–§12** — the model in a paragraph, and where to go next.

---

## 1. Counting a position, or deriving it

An animation needs to know where it is: how many milliseconds into the sequence, which this library calls the *playhead*. There are two ways to keep one.

**Counting.** Store the playhead and advance it every frame: `playhead += 16` for a 60 Hz loop, or `playhead += now - last_now` if the loop measures its own frames. This is the obvious design, the one a tape deck uses, and it has three problems that all come from the same place. Each frame adds its own error, and the errors accumulate: a 60 Hz loop that adds 16 ms when 16.67 ms has passed falls a full second behind every 25 seconds (§10, Exercise 1). A dropped frame is lost time for good. And the playhead is state that some code path must update every frame, so a path that forgets — a paused render, an early return — moves it out of step.

**Deriving.** Store the clock reading at which the playhead was 0, the *origin*, and work the playhead out on every call:

```cpp
playhead = now - origin;
```

Nothing accumulates, because nothing is added up: the answer depends only on the clock reading in hand. A late frame shows the right position late; a dropped frame is simply never asked about. Two calls in the same millisecond agree. And there is no update for any path to forget, because there is nothing to update.

`Sequencer` derives. Its whole state is a copy of the sequence and three fields:

```cpp
uint32_t origin_ms_ = 0;    /* playing: the instant playhead 0 sits at */
int32_t  frozen_ = 0;       /* not playing: the playhead itself */
bool     playing_ = false;
```

While playing, `playhead(now)` derives the answer from `origin_ms_`. While paused, there is nothing to derive from — the clock moves and the playhead must not — so it stores the playhead itself in `frozen_`. §6 is about moving between the two. Everything before it is about the playing case, which is where the arithmetic lives.

## 2. Subtract, then convert

The clock is `uint32_t` milliseconds since boot, and so it wraps: after 2³² − 1 = 4,294,967,295 ms, about 49.7 days, it reads 0 again. An origin taken shortly before the wrap is a large number, and a clock reading shortly after it is a small one. Take an origin of 4,294,967,000 and a reading of 200, 496 ms later:

```text
origin  4294967000   (296 ms before the wrap)
now            200   (200 ms after it)
```

The fix for this is no fix at all: subtract in `uint32_t`, and let the arithmetic be modular. `200 − 4294967000` modulo 2³² is 496, exactly the elapsed time, because modular subtraction does not care where zero is. The wrap is a point where the *representation* jumps, and a difference taken in the same modular arithmetic never sees it. The same holds for any two readings: as long as the true elapsed time is less than 2³² ms, the unsigned difference *is* the elapsed time.

The playhead is signed — a pixel delayed past the start has a negative one — so the difference has to become an `int32_t` at some point. The order of the two steps is the whole lesson:

```cpp
const int32_t p = static_cast<int32_t>(now_ms - origin_ms_);   // subtract, then convert
```

There are two other orders, and both are wrong in instructive ways (§10, Exercise 2):

- **Widen first:** `int64_t{now} - int64_t{origin}`. It looks safer, since nothing can overflow 64 bits, and that is exactly the problem. In 64-bit arithmetic there is no modular wrap to cancel the clock's, so across the wrap the answer is −4,294,966,800: the clock's 2³² jump, faithfully preserved.
- **Convert first:** `int32_t(now) - int32_t(origin)`. This one usually gives the right answer, which makes it worse. Converting a reading past 2³¹ to `int32_t` makes it negative, so the conversion has its own wrap, at 2³¹ rather than 2³². Across *that* point the subtraction of the two signed values overflows `int32_t`: undefined behaviour. On today's compilers and hardware it happens to produce 496 anyway, so no test sees it, and only UBSan does.

Subtracting first leaves no wrap to cross and nothing undefined: one modular subtraction, then one conversion, which C++20 defines as modular too.

## 3. The real limit is elapsed time, not the clock

With §2 in place, the clock's wrap at 49.7 days is a non-event. But a limit has not gone away; it has moved.

The unsigned difference is correct for any elapsed time under 2³² ms. The conversion to `int32_t` keeps it correct only under 2³¹ ms, about **24.8 days**. One millisecond past that, a playhead that should read 2,147,483,648 converts to −2,147,483,648 — and to an animation, a large negative playhead means "not started yet". On a finite sequence that is the color it shows before it starts. On this library's Wipe recipe, a strip that swept to green and is meant to stay green would go black after 24.8 days, and the difference would come back round to small positive values and wipe again at 49.7.

So the question to ask of any code like this is not "does it survive the clock wrapping?" — subtraction handles that — but **"what is the largest elapsed time this difference will ever hold?"** If it is bounded by something shorter than 24.8 days, by design rather than by hope, there is nothing more to do. A sequencer that plays for as long as the device is powered has no such bound unless it makes one, which is what §4 does.

RFC 1982 states the same rule for sequence numbers, the general form of a clock reading: two values on a modular counter can be compared correctly only when they are less than half the counter's range apart. Half the range of a 32-bit counter is 2³¹, which is why the limit is 24.8 days and not 49.7.

## 4. Moving the origin

The elapsed time grows only because the origin stays put. Move the origin forward, and the elapsed time can be held as small as you like — provided the move changes nothing anyone downstream can observe. There are two such moves, one for each kind of sequence, and `playhead()` makes one of them on each call where it applies.

**An endless sequence: re-base by whole loops.** An endless sequence repeats every `loop` milliseconds, and `Sequence::at()` already treats its timeline as a ring, so a playhead of 1730 on a 500 ms loop is the same moment as 230. Whenever the derived playhead reaches the loop length, `playhead()` moves the origin forward by as many whole loops as fit and returns the remainder:

```cpp
const uint32_t into_loop = static_cast<uint32_t>(p) % loop;
origin_ms_ += static_cast<uint32_t>(p) - into_loop;
return static_cast<int32_t>(into_loop);
```

The origin and the answer move by the same whole number of loops, so every pixel — each of which looks up `at()` on the playhead plus its own stagger — sees exactly what it would have seen. What it buys is that the origin never trails the clock by more than one loop plus the gap since the last call. The builder refuses any loop over 2²⁹ ms, so as long as something asks more often than about every 18 days, the elapsed time never gets near 2³¹. A strip rendering at any frame rate asks millions of times a day.

Two cases are skipped. A negative playhead — a sequencer played after a `seek` to before the start — is already one `at()` wraps correctly, and it is counting *up* towards zero, away from the limit. And a sequence with no steps has a loop of zero, where the remainder would be a division by zero. The division itself runs only on calls that cross a loop boundary, not on every call.

**A finite sequence: cap it.** A finite sequence cannot be re-based, because its position is absolute: 1730 ms into three 500 ms loops and a landing is not the same moment as 230. But once its playhead is far enough past the end, the exact value stops mattering — every pixel shows the landing color and holds it forever — so it only has to *stay* far enough past the end. When the derived playhead passes 2³⁰ ms, about 12.4 days, `playhead()` moves the origin to keep it there:

```cpp
if (p > finite_cap_ms) {
    origin_ms_ = now_ms - static_cast<uint32_t>(finite_cap_ms);
    return finite_cap_ms;
}
```

The tempting version clamps only the answer — `return p > cap ? cap : p` — and it is a trap, because it leaves the origin where it was. The unsigned difference underneath keeps growing, and at 24.8 days the conversion in §2 turns it negative before the clamp ever sees it. The clamp tests a value that has already gone wrong (§10, Exercise 3). Moving the origin is what keeps the difference itself small.

Why 2³⁰, and not the sequence's own total? Because a segment of pixels is *staggered*: its last pixel runs some fixed delay behind the sequencer, so it asks `at()` about the playhead minus that stagger, and a sequencer capped at the total would leave every staggered pixel short of the end forever. The sequencer does not know which segments read from it, so it caps at a constant chosen to clear every case at once: the builder refuses any total over 2²⁹ ms, the segment's overflow guard keeps every stagger under 2²³ ms — a stagger is a pixel index times a delay that must fit in an `int32_t` with 8 fractional bits — and 2³⁰ − 2²³ is still well past 2²⁹. Two limits enforced elsewhere are what make the constant safe, and the test called "a capped playhead minus any stagger is past any accepted total" writes that arithmetic down where a change to either limit would break it.

## 5. The moment to move it

`playhead()` moves the origin whenever it is asked. That is enough only if it is asked before the elapsed time can go wrong — and there is one moment where it is not.

`seek(p)` sets the playhead directly, to any `int32_t`, and stores it exactly: a progress bar that seeks to a value must read that value back. `play(now)` then picks the origin that puts the playhead at `frozen_` at this instant, so that it carries on from where it stood. Now seek to `INT32_MAX − 5` and play. The derived playhead starts at `INT32_MAX − 5` and is six milliseconds from overflowing. If the first `playhead()` call comes 6 ms after `play()` — one frame later, at almost any frame rate — the difference has already crossed 2³¹, converted to a large negative number, and §4's moves both skip negative playheads. The finite sequence shows its start color; the endless one is out of phase for good (§10, Exercise 5).

So `play()` makes the move once itself, at the instant the playhead starts following the clock:

```cpp
origin_ms_ = now_ms - static_cast<uint32_t>(frozen_);
playing_ = true;
(void)playhead(now_ms);   /* re-base or cap here, not on the next call */
```

At that instant the derived playhead is exactly `frozen_`, so the call re-bases or caps the value just restored, before any time has passed. From then on the steady state of §4 holds. The general rule: **an invariant maintained "on every call" has to be established at every point where the state enters the regime it protects** — here, the transition into playing — not merely restored on the next call afterwards.

## 6. Two representations, one bit

A paused playhead does not follow the clock, so it cannot be derived from one; `frozen_` holds it directly. That gives the sequencer two representations of one value and one bit, `playing_`, saying which is current:

| Call | Stores | Effect |
| --- | --- | --- |
| `play(now)` | `origin_ms_ = now - frozen_` | Picks the origin that puts the playhead where it stood. |
| `pause(now)` | `frozen_ = playhead(now)` | Records where the playhead is, and keeps it there. |
| `seek(p)` | `frozen_ = p` | Sets it; no clock involved. |

`play` and `pause` take `now` because each converts between a value and an origin; `seek` only ever sets a value. Worked through, with clock time on the left:

```text
t=1000  play(1000)    origin = 1000 - 0    = 1000   playhead(1300) = 300
t=1500  pause(1500)   frozen = 1500 - 1000 = 500    playhead(9999) = 500    stays put
t=4000  play(4000)    origin = 4000 - 500  = 3500   playhead(4200) = 700    carries on from 500
```

A 2.5-second pause costs nothing: the new origin sits 500 ms in the past, so the playhead resumes at exactly the value it was paused at.

Each conversion reads the field the *other* state keeps current, and that is the trap. While playing, `frozen_` is stale — it still holds whatever the last pause or seek left. So `play()` called while already playing would rebuild the origin from that stale value, and the playhead would jump back to it. A redundant `play()` is not exotic: any code that calls it "to make sure" does it, on every frame. So `play()` while playing must do nothing (§10, Exercise 4):

```cpp
if (playing_)
    return;
```

`pause()` while paused has the mirror-image problem in principle — `origin_ms_` is stale while paused — but here it needs no guard, because `pause()` records `playhead(now)` rather than computing `now - origin_ms_` itself, and while paused `playhead()` answers `frozen_` without reading the origin at all. Pausing again stores `frozen_` into `frozen_`. Routing every conversion from a clock reading to a playhead through the one function that knows how — including its moves from §4 — is what made the guard unnecessary, and is the better habit of the two.

There is one bit here, not a state machine. "Stopped" is `seek(0)`; "resume" is `play`; "finished" is a property of the playhead and the sequence, worked out when someone asks. The obvious extra states are all derivable, and a derivable state stored separately is one more thing that can disagree with what it was derived from.

## 7. Testing sixty days in milliseconds

None of this can be tested by waiting. It can be tested at all because the clock is an *argument*: `playhead(now_ms)` reads no hardware, so a test can hand it any reading it likes, in any order it likes — including 60 days' worth in a fraction of a second. Taking the time as a parameter rather than reading it inside is what makes time-dependent code testable, and it is worth designing for from the start.

Three habits make such tests trustworthy.

**Compare against a reference that cannot have the bug.** The question "is re-basing invisible?" is answered by computing what the playhead *should* be in arithmetic with no re-basing and no wrap — the true elapsed time in `int64_t`, reduced modulo the loop — and comparing every answer against it. A reference that shares the implementation's arithmetic shares its mistakes; one done in a wider type, the slow obvious way, does not.

**Stride, but not in step with anything.** Ten days is 864 million milliseconds, too many to visit one by one five times over. The tests step the clock by 7919 ms, a prime: a stride that shares no factor with the 777 ms loop under test or with a day, so successive samples land at every offset into the loop rather than revisiting a few. Where a property is local — no discontinuity across the wrap — the test goes back to single milliseconds, but only over the few hundred around the event.

**Never let the simulated clock run backwards.** Real clocks only go forward, and code like this quietly relies on it: once `playhead()` has moved the origin, a question about an earlier instant gets an answer measured from the *new* origin. A 60-day test that asks about day 59 after its loop has asked about day 60 gets a playhead a day short of the cap and fails — on a correct answer to a question the sequencer is never asked. It is an easy test to write by accident, because a reading for day 59 is just another `uint32_t`, with nothing about it to say it comes before the last one asked about, and once the clock has wrapped, readings are no longer ordered by size. Hence `clock_after(start, elapsed)` in `tests/sequencer.cpp`, a helper that turns a 64-bit elapsed time into the wrapped 32-bit reading a real clock would show, so the test's order is written in elapsed times, where later is larger.

The 60-day run is the one that matters most, because it is the only test that crosses everything at once: the 2³⁰ cap at 12.4 days, the point where an unprotected difference would go negative at 24.8, and the clock's own wrap at 49.7.

## 8. The same pattern elsewhere

Nothing here is specific to LED strips; every system with a free-running counter meets it.

- **Zephyr**, against v4.4.1. `k_uptime_get_32()` (`include/zephyr/kernel.h:2171`) is the low 32 bits of the 64-bit uptime in milliseconds — exactly the clock this library takes, and the one the sketch's Zephyr adapter samples once per frame. Its documentation points out that it computes the full 64-bit uptime anyway and truncates it. Its sibling `k_uptime_delta()` (`include/zephyr/kernel.h:2200`) sidesteps the question by working in `int64_t` throughout, which costs 64-bit arithmetic on every call to avoid a wrap that §2 already makes harmless. The PTP sample measures its run time with the §2 idiom, `(k_uptime_get_32() - uptime) / 1000` (`samples/net/ptp/src/main.c:83`). And `k_cycle_get_32()` (`include/zephyr/kernel.h:2219`) is the same problem on a much shorter fuse: a 32-bit counter of hardware cycles at a few hundred MHz wraps in seconds, so the same subtraction is mandatory and the elapsed-time bound of §3 is measured in seconds too.
- **Arduino.** `millis()` is a 32-bit millisecond counter, and the idiom every sketch is told to use is `if (millis() - start >= interval)` — subtract, then compare — rather than `if (millis() >= start + interval)`, which breaks at the wrap. It is §2 exactly.
- **Linux.** `jiffies` is a wrapping tick counter, and `time_after(a, b)` in `include/linux/jiffies.h` compares two readings as `(long)((b) - (a)) < 0`: subtract in unsigned, then convert to signed, valid while they are under half the range apart. That is §2 and §3 in one macro.
- **TCP and DNS.** Sequence numbers and zone serials are modular counters, and RFC 1982, "Serial Number Arithmetic", defines comparing them the same way, including the half-range rule.

The common thread: **never compare readings, compare differences, and know the largest difference you will ever take.**

## 9. The checklist

- **Derive positions from an origin**; store a position only while it is not following the clock.
- **Subtract readings in the clock's own unsigned type, then convert** the difference. Never widen first; never convert first.
- **Name the largest elapsed time** the difference can hold, and check it is under half the counter's range.
- **If nothing bounds it, move the origin**: by whole periods for anything periodic, to a constant cap for anything that ends. Never clamp only the answer.
- **Establish the bound at every entry** into the following-the-clock state, not just on the next call.
- **Each transition does nothing in the state it produces**, because it reads the field the other state keeps current.
- **Take the clock as a parameter**, and test with a wider reference, a stride that shares no factor with the periods under test, and a clock that never runs backwards.

---

## 10. Exercises

Five. The first two are small standalone programs; the other three break the library on purpose and watch its tests catch it.

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

### Exercise 3 — Clamp the answer instead of moving the origin

*Demonstrates §3–§4.*

In `Sequencer::playhead()` in `src/sequencer.cpp`, delete the line in the finite branch that moves the origin, leaving the clamp:

```cpp
    if (p > finite_cap_ms) {
        origin_ms_ = now_ms - static_cast<uint32_t>(finite_cap_ms);   // delete this line
        return finite_cap_ms;
    }
```

```sh
cd "$REPO" && cmake --build build --target tests_default && ./build/tests_default -tc='a finite sequence left playing*'
```

```
TEST CASE:  a finite sequence left playing for 60 days stays on its landing color

tests/sequencer.cpp:338: ERROR: CHECK( p == expected ) is NOT correct!
  values: CHECK( -2147477038 == 1073741824 )
  logged: elapsed := 2147490258

tests/sequencer.cpp:339: ERROR: CHECK( landed ) is NOT correct!
  values: CHECK( false )
  logged: elapsed := 2147490258

tests/sequencer.cpp:346: ERROR: CHECK( at_61_days == cap_ms ) is NOT correct!
  values: CHECK( 975432704 == 1073741824 )

tests/sequencer.cpp:351: ERROR: CHECK( seq.playhead(0) == cap_ms ) is NOT correct!
  values: CHECK( 1061832704 == 1073741824 )
```

The same four errors follow for the second sequence the case runs. **Revert it.** Look at `elapsed` in the first failure: 2,147,490,258 ms is the first sample past 2³¹ = 2,147,483,648, at 24.86 days. Up to that sample the clamp did its job and every answer was right.

**Proves:** a clamp on the answer protects nothing once the value it tests has already wrapped. Moving the origin keeps the difference itself small; without it, the strip goes black at 24.8 days exactly as §3 predicts.

### Exercise 4 — A redundant `play()` that restarts

*Demonstrates §6.*

In `Sequencer::play()`, change `if (playing_)` to `if (false)`.

```sh
cmake --build build --target tests_default && ./build/tests_default -tc='play while playing*'
```

```
TEST CASE:  play while playing and pause while paused change nothing

tests/sequencer.cpp:168: ERROR: CHECK( seq.playhead(1700) == 700 ) is NOT correct!
  values: CHECK( 0 == 700 )

tests/sequencer.cpp:169: ERROR: CHECK( seq.playhead(2000) == 1000 ) is NOT correct!
  values: CHECK( 300 == 1000 )

tests/sequencer.cpp:172: FATAL ERROR: REQUIRE( seq.playhead(2500) == 1500 ) is NOT correct!
  values: REQUIRE( 800 == 1500 )
```

**Revert it.** The second `play()` rebuilt the origin from `frozen_`, still 0 from the `load()` before the first one, so the playhead jumped back to the start.

**Proves:** a transition that reads the other state's field must do nothing in its own state. Unguarded, a call that looks idempotent is a restart.

### Exercise 5 — Seek to the edge, then play

*Demonstrates §5.*

In `Sequencer::play()`, delete the line `(void)playhead(now_ms);`.

```sh
cmake --build build --target tests_default && ./build/tests_default -tc='play after a seek*'
```

```
TEST CASE:  play after a seek near INT32_MAX does not overflow
  finite: held at the cap, on its landing color

tests/sequencer.cpp:373: ERROR: CHECK( p == cap_ms ) is NOT correct!
  values: CHECK( -2147482654 == 1073741824 )

tests/sequencer.cpp:374: ERROR: CHECK( wipe.at(p).color == colors::green ) is NOT correct!
  values: CHECK( rgb(0, 0, 0) == rgb(0, 65535, 0) )

TEST CASE:  play after a seek near INT32_MAX does not overflow
  endless: in phase with where it was sought

tests/sequencer.cpp:383: ERROR: CHECK( seq.playhead(2000) == mod_loop(int64_t{sought} + 1000, loop) ) is NOT correct!
  values: CHECK( -2147482654 == 387 )
```

and a third failure for the subcase that also crosses the clock's wrap. **Revert it.** The finite sequence shows black, its start color, instead of green; the endless one reports a negative playhead that `at()` will wrap, but onto the wrong point of the loop, because 2³² is not a multiple of 777.

**Proves:** `playhead()` keeping the difference small on every call is not enough when a transition can hand it a difference that is already at the edge. The move has to happen at the transition itself.

---

## 11. The model in one paragraph

A position that follows a clock should be derived from it, not counted: store the reading at which the position was zero, and answer `now - origin` on every call, so there is no accumulated error, no lost frame and no update to forget. On a wrapping unsigned counter, that subtraction is done in the counter's own modular arithmetic and only then converted to signed, and the counter's wrap cancels out of it entirely; widening first preserves the jump, and converting first introduces a second wrap at half the range where the subtraction is undefined. What remains is a limit on the *difference*: under half the counter's range, 24.8 days for 32-bit milliseconds. Code that runs indefinitely stays inside it by moving the origin rather than clamping the answer — forward by whole periods for anything periodic, to a constant cap for anything that ends — and by making that move at every point where the state starts following the clock, not just on the next call. Holding a position still needs a second representation, so the state is one bit choosing between two fields, and each transition must do nothing in the state it produces, because it reads the field the other state keeps current. All of it is testable in milliseconds because the clock is a parameter: compare against a wider reference that cannot share the bug, stride in steps that share no factor with the periods under test, and never let the simulated clock run backwards.

## 12. Where to go next

- **`src/sequencer.cpp`** — the whole of §2–§6, in under eighty lines with comments.
- **`tests/sequencer.cpp`** — §7 as written: the reference model, the strides, `clock_after()`, and a case for each section of this guide.
- **[`../docs/API-SKETCH.md`](../docs/API-SKETCH.md), "Sequencer"** — the decisions this guide teaches, recorded beside the declarations they govern, with the worked examples in full.
- **[`integer-arithmetic-guide.md`](integer-arithmetic-guide.md)** — the modular and signed arithmetic underneath §2, and how `Sequence::at()` folds a negative playhead onto the ring.
- **[`testing-guide.md`](testing-guide.md) §8** — what the test suite for this code could and could not see: a bug that every test passes on one machine and crashes on another.
- **RFC 1982, "Serial Number Arithmetic"** — the half-range rule of §3, stated for sequence numbers, in eight pages.
