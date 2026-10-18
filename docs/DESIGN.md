# StripSequencer — Design

A library for animating addressable LED strips. It targets short strips (tens of pixels, up to a hundred or so), simple animations, and any platform that can give you a millisecond counter and a pixel buffer.

This document explains the animation mechanism first and in detail, because that mechanism is the whole design — almost everything else is a consequence of it. Language and implementation specifics are deliberately pushed to the back.

How it is called — how the pieces relate, what one frame does, and every cookbook recipe below written as real code — is in [COOKBOOK.md](COOKBOOK.md). The declarations, each with the reasoning behind it, are in [`include/stripseq/`](../include/stripseq/).

## Contents

- [What this is](#what-this-is)
- [The core idea](#the-core-idea)
  - [A sequence is a lookup table, not a tape](#a-sequence-is-a-lookup-table-not-a-tape)
  - [The playhead](#the-playhead)
  - [Adding space: one number per segment](#adding-space-one-number-per-segment)
  - [What happens off the ends](#what-happens-off-the-ends)
  - [Two places to plug in](#two-places-to-plug-in)
- [The four building blocks](#the-four-building-blocks)
- [How a frame gets rendered](#how-a-frame-gets-rendered)
- [Cookbook](#cookbook)
- [Supporting decisions](#supporting-decisions)
- [Non-goals](#non-goals)
- [Deferred](#deferred)

## What this is

You describe an animation once, as a list of colors and durations. The library works out what every pixel should be at any given moment, writes those colors into a buffer you own, and tells you when to come back. Pushing that buffer to the hardware is your driver's job — on Zephyr, one of the `led_strip_update_*` calls.

The library never touches hardware, never reads a clock, and never allocates. It has no knowledge of Zephyr, or of any operating system. That is not an aspiration to be tidy; it is what makes the same code testable on your laptop and runnable on bare metal.

Nothing here scales with strip length except the pixel buffer you were going to allocate anyway. There is no per-pixel state inside the library, ever. Strip length is therefore a number the library barely notices, and several decisions below exist to keep it that way — the hundred-pixel target is about what a small microcontroller can render and transmit in a frame, not about anything the design itself runs out of.

## The core idea

### A sequence is a lookup table, not a tape

The instinct with animation is to think of a tape playing: the sequence advances, step by step, and pushes colors out as it goes. That model is easy to picture and it is the wrong one, because it makes the sequence *stateful* — the color depends on how it got here — and once the color depends on history you can no longer ask questions like "what would this animation look like two seconds ago?"

Flip it around. A **sequence** is a description of color against a timeline, and you can point at any place on that timeline and ask what color is there. Nothing advances. Nothing is consumed. It is a lookup table with a continuous index.

Here is a sequence of three steps, each 500 ms, each fading smoothly into the next:

```text
        0ms         500ms        1000ms       1500ms
        |------------|------------|------------|
        [ red→green ][ green→blue ][ blue→red  ]

                  ^
                  ask here (300 ms) → 60% of the way from red to green
```

Ask at 300 ms and you get a color 60% of the way from red toward green. Ask at 300 ms again a week later and you get exactly the same color. The sequence holds no state and no time; it is pure description, and it can live in flash.

Everything the library does is built on this one property, so it is worth being clear about why it matters. Because lookup is stateless and repeatable:

- **Animation cannot drift.** If your loop is late by 8 ms, you ask at the real current time and get the correct color for *now*, rather than the color you would have shown 8 ms ago. Lateness costs you a slightly jerky frame, never a slowly desynchronising animation.
- **Independent animations stay in sync.** Two things started at the same moment stay in step forever, because neither is counting — both are looking up.
- **Pausing and picking up again are trivial.** Pausing means "stop moving the place I am pointing at." Starting again means "shift the origin forward by however long I stood still." No state to reconcile.
- **And the one this whole library rests on: different pixels can point at different places on the same timeline.**

That last point is the entire spatial mechanism, and the rest of this section unpacks it.

### The playhead

If the sequence is a timeline you can point at, something has to decide *where* to point. Call that place the **playhead** — a single number, in milliseconds, measured from the start of the sequence.

The **sequencer** is the small piece of state that owns a playhead. Normally it derives the playhead from the clock: playhead = now − the moment it started playing. That is all "playing" means. Pausing stops deriving and keeps the number where it was; playing again shifts the origin forward so the playhead carries on from there rather than jumping. You can also set the playhead directly (`seek`) if you want to place it by hand — which turns out to matter later.

So far this is a single-LED library: one sequence, one playhead, one color, one LED.

```text
sequence  ─────────────────────────────────────────
                          ^
                      playhead (moves with the clock)
                          |
                          v
                       one color  →  the LED
```

### Adding space: one number per segment

Now the strip. Say you have thirty pixels and you want a comet running along them.

The obvious approach is one sequencer per pixel, each started at a staggered time. It works, and it is a dead end: thirty sequencers means thirty copies of the state, the RAM cost grows with the strip, and nothing structurally ties them together — you have thirty independent animations that merely happen to look related.

The alternative comes straight out of "a sequence is a lookup table." All thirty pixels share **one** sequence and **one** playhead. Each pixel simply looks at a different place on the timeline: pixel 0 looks at the playhead, pixel 1 looks slightly earlier, pixel 2 slightly earlier still.

One number controls this. Call it **`pixel_delay`**: how many milliseconds *behind* each pixel is relative to the one before it.

```text
    playhead = 300ms,  pixel_delay = 40ms

    pixel:        0     1     2     3     4     5     6
    looks at:   300   260   220   180   140   100    60   (ms on the timeline)
                 |     |     |     |     |     |     |
                 v     v     v     v     v     v     v
              [ the same sequence, sampled at seven places ]
```

Every pixel is showing the same animation; they are just showing different moments of it. As the playhead advances, each pixel walks forward along the timeline, and whatever the animation does appears to travel down the strip.

It is worth being exact about what exists here, because the arithmetic invites you to imagine more state than there is. There is still one playhead, and the sequencer owns it. The seven numbers in the diagram are owned by nobody. Each is worked out from that one playhead and the pixel's own index at the moment it is needed, used for a single lookup, and thrown away; nothing is written back, and the next frame recomputes all of them from scratch. That is the no-per-pixel-state rule in its most literal form — thirty pixels have thirty positions on the timeline, and the library remembers none of them.

Now look at what falls out of that single number, with no additional machinery:

| `pixel_delay` | Sequencer | What you see |
| ------------- | --------- | ------------ |
| `0` | playing | Every pixel reads the same point — the whole strip acts as one big LED. Blink, fade, breathe. |
| small (tens of ms) | playing | A wave rolls down the strip. Chase, comet, travelling pulse. |
| `loop_duration ÷ pixel_count` | playing | The whole animation is spread across the strip at once, and rotates. Rainbow cycle. |
| `loop_duration ÷ pixel_count` | **not playing** | The same spread, frozen. A static gradient. |
| any, negative | either | The same effects, running the other way. |
| any | **seek by hand** | The playhead becomes an input rather than a clock reading. Progress bars and meters. |

These are not six features. They are one expression — *pixel `i` looks at `playhead − i × pixel_delay`* — with different constants. The uniform case, which is what most status indicators need, costs nothing extra because `pixel_delay` is simply zero.

This is the central bargain of the design: generality comes from one small mechanism rather than from a catalogue of named effects. There is no `EFFECT_CHASE` and no `EFFECT_RAINBOW` to implement, maintain, and multiply.

#### Two ways to think about the number

`pixel_delay` is milliseconds per pixel, and that is the right unit when you care about **speed**. A comet with a 16 ms delay travels at the same rate whether the strip is twenty pixels or eighty; a longer strip simply takes longer end to end. That is usually what you want from anything that reads as motion.

But a gradient or a rainbow cares about **fit** instead. You want the whole animation spread across the segment, however many pixels it happens to have, so the same setup looks right on a twelve-pixel ring and a sixty-pixel strip. That is a different intent, not a different unit, so the library offers a helper that does the arithmetic — `spread(duration, count)` — rather than a second thing to store.

Both are the same number in the end, and the library stores only that number. Fit is a division — total span ÷ pixel count — done once, where the segment is set up. It is worth naming the two intents anyway, because they answer differently when a strip changes length: a speed is still the same speed, while a span has to be divided again. Nothing in the design resizes a segment in place, so this never becomes machinery. A segment holds no animation state — the playhead lives in the sequencer — so changing one means assigning a fresh segment over it, and whichever intent you had, you express it again in the line that builds the replacement.

That division rarely comes out even, and the leftover is more visible than its size suggests. Spreading a 1000 ms loop across 30 pixels wants 33.33 ms per pixel; as a whole number it becomes 33. Pixel 29 then sits 957 ms behind pixel 0 instead of 967, and on a ring — where pixel 29 and pixel 0 are neighbours — the gap between those two works out at 43 ms against 33 everywhere else. One junction out of thirty jumps a third further than the rest, and on an otherwise even gradient that reads as a seam. Note that the error only bites when the strip wraps. On a straight run the same truncation just means the gradient spans 990 ms rather than 1000, and a one percent difference with nothing beside it to compare against is invisible.

So `pixel_delay` is stored in units of 1/256 ms. The same spread becomes 8533 instead of 33, the closing gap lands at 33.37 ms against 33.33 elsewhere, and the seam goes away. Worst case across a hundred pixels the accumulated error is under half a millisecond, which is about a fortieth of a single frame at 60 fps.

The fraction only matters for numbers the library works out for itself. A comet whose delay you set directly at 16 ms is stored exactly and never drifts. It is `spread` that produces awkward values, because it has to divide by whatever pixel count the segment happens to have.

Two alternatives were considered. Rounding instead of truncating doesn't help, since 33.33 rounds down to 33 anyway. Computing each pixel's offset exactly as `i × total_span ÷ count` removes the error entirely, but it needs a division per pixel — expensive on cores without a divide instruction — and it means storing a span and a count rather than the one number every pixel actually uses. A power-of-two fraction keeps the multiply the render loop was doing regardless and adds a single shift.

### What happens off the ends

Look at the diagram again with a longer strip, and a problem appears. With `pixel_delay = 40 ms`, pixel 29 looks `29 × 40 = 1160 ms` behind the playhead. The diagram had the playhead at 300 ms, so pixel 29 is pointing at `300 − 1160 =` **−860 ms** — before the sequence begins. It stays negative until the playhead passes 1160 ms, so this is not a brief startup flicker; and a short finite sequence has the opposite problem, with most pixels pointing *past its end*.

So the sequence cannot only answer for the range it explicitly covers. It has to answer for **every** playhead value, including negative ones and ones far past the end. Once you accept that, the answer depends on a distinction that turns out to be genuinely useful:

**An endless sequence is a circle.** If the sequence repeats forever, its timeline has no beginning and no end — it is a ring. A playhead of −860 ms on a 500 ms loop simply wraps to 140 ms. This is exactly what you want: a rainbow that rotates around a ring of LEDs joins up seamlessly, and a chase wraps from the end of the strip back to the beginning with no visible seam. There are simply no ends to fall off.

```text
    endless sequence — the timeline is a ring

              ┌──────────────┐
              │              │        playhead −860ms
        red   │              │  blue     ≡  140ms
              │              │
              └──────────────┘
                    green
```

**A finite sequence is a line.** If the sequence runs a fixed number of times and then stops, it genuinely has a before and an after, and both need a color. Repeating three times simply makes the line three loops long: the wrapping still happens inside it, and the two ends are still ends.

- **`start_color`** — what a pixel shows when its playhead is *before* zero. The animation has not reached it yet.
- **`landing_color`** — what a pixel shows when its playhead is *past the end*. The animation has already passed it by.

```text
    finite sequence — the timeline is a line

    start_color  |======= the sequence =======|  landing_color
    ─────────────|                            |──────────────────
       "not yet"                                  "already done"
```

And now a wipe — light sweeping down a dark strip and leaving it lit — is not a special effect either. It is a finite sequence with `start_color` black and `landing_color` on, plus a `pixel_delay`. Pixels ahead of the wavefront haven't started, so they show black. Pixels behind it have finished, so they show the final color. The pixel at the wavefront is mid-transition. The mechanism produces the effect; nobody implements it.

This also tidies up two rules that would otherwise look arbitrary. An endless sequence has neither color and the builder rejects both — `landing_color` because a sequence that never finishes never lands, and `start_color` because negative playheads wrap rather than falling off, so no pixel is ever before the timeline. A finite sequence may have either or both. The symmetry is not decoration; it is the shape of the thing.

One thing this does *not* settle, though the name invites it, is fading into an animation from whatever color happened to be showing beforehand. That is a different question: it asks what should happen when the *sequencer* starts, not what lies before the *timeline* begins, and it applies to endless and finite sequences alike. Nothing above rules it out — it simply isn't designed yet. See [deferred](#deferred).

### Two places to plug in

Everything so far has been built from a single sentence: *pixel `i` looks at `playhead − i × pixel_delay`, and the sequence says what color is there.* Read it again and notice it is really two separate steps, one after the other.

```text
    pixel i  ──►  where does it look?  ──►  what color is there?  ──►  color

                  playhead − i×delay          the list of steps
                       (step 1)                    (step 2)
```

Both steps have a sensible default, and everything in the cookbook below uses those defaults. But each is just a small calculation, and either can be replaced without disturbing the other. That is where all the library's extensibility lives — not in a plugin system, but in the fact that the render loop is two composable functions.

**Replacing step 1 — where a pixel looks.** The default spreads pixels evenly along the timeline. Anything that computes an offset from a pixel index will do: mirror the strip by flipping the index, bunch pixels toward one end by easing the offset, or — the interesting one — give each pixel a *pseudo-random* offset.

That last case is worth spelling out, because it solves something the mechanism looked unable to reach. Twinkle needs pixels flashing at unrelated moments, which sounds like it needs per-pixel state. It doesn't. Take an endless sequence that flashes bright and fades over, say, two seconds, and give pixel `i` an offset of `hash(i)` milliseconds into that loop. Every pixel now runs the same animation at a different, fixed point in it, so they twinkle independently. Nothing is stored.

**Replacing step 2 — what color is at a playhead.** The default walks the list of steps. A replacement is any function from a playhead to a color, which is how you get curves a step list can't describe — a true sine breathe rather than an eased approximation, or a color computed from a formula.

The two compose. A custom color function combined with a non-zero `pixel_delay` is a procedural wave travelling down the strip, and neither half had to know about the other.

A replacement for step 1 returns how far behind the segment's playhead a pixel looks, not the pixel's playhead itself, so the segment does the subtraction on every path and can bound it whatever is installed. The price is that step 1 cannot warp time: reversing or rescaling a pixel's playhead is beyond it, and is left to a replacement for step 2, which receives that playhead. Step 1 is a pure function of the pixel index, the pixel count and the segment's `pixel_delay`, which it may read however it documents — twinkle reads it as the span its offsets scatter across. Step 2 is a pure function of the pixel's playhead, index and count. Neither gets any other state, which is enough for twinkle and mirroring, delays included, but rules out anything parameterised beyond that one number — see [deferred](#deferred).

Both the built-in lookup and any replacement answer the same question in the same shape: given a playhead, what color is here, and *how long does it stay this color?* The second half feeds the "when should I come back" answer described [below](#how-a-frame-gets-rendered), and it is not an extra burden — the built-in lookup has to find the step containing the playhead anyway, so that step's end falls out of the same search. A function that genuinely changes every frame, like a sine breathe, reports zero and gets frame-rate treatment; one that holds a color for a while says how long. So replacing the default costs nothing in power: a segment driven by your own function can let the system sleep exactly as long as one driven by a step list.

All of which is why the library does **not** offer a "here is the buffer, draw whatever you like" escape hatch. Handing over the buffer means giving up everything the surrounding machinery does for you: segments, overlapping, brightness, gamma, and the sleep answer. Plugging into one of these two steps keeps all of it working.

## The four building blocks

With the mechanism established, the pieces are small and each has one job.

**Sequence** — the description. A list of steps, each a target color, a duration, and how to get there (jump, linear fade, or one of three eased fades). Plus whether it loops forever or a fixed number of times, and for finite sequences the start and landing colors. It holds no state, references no hardware, and can be built at compile time or at run time. Given a playhead, it produces a color and how long that color holds. That is its entire interface.

**Sequencer** — the playhead, and the one bit of state around it: whether the playhead follows the clock or stands still. It converts a clock reading into a playhead, or accepts one directly via `seek`. Several segments may share one sequencer, which is how you get two parts of a strip running the same animation in lockstep.

It is worth resisting the urge to give it more states than that. "Complete" is a comparison against the sequence's length, not a condition to store — and as the **Segment** entry below explains, it is not even the signal you want, since a segment finishes a full stagger later than its sequencer does. "Idle" is a sequence with no steps. "Paused" and "loaded but never played" are the same standing-still condition reached from different directions, and nothing downstream can tell them apart.

**Segment** — a run of pixels with its own animation.

Each segment holds three things: the run of pixels it covers, the sequencer driving them, and the `pixel_delay` that staggers them. Segments are entirely independent, and each may have its own sequencer or share one with its neighbours. There are two further fields, bookkeeping rather than concepts: the optional replacements for the [render loop's two steps](#two-places-to-plug-in).

Segments are also the only part of the design that knows pixels exist. A sequence describes color against time, and a sequencer tracks a position in that description, but neither has any idea how many LEDs there are or where they sit — which is why the pixel range and the stagger both live here and nowhere else.

That is also why *finishing* is a segment-level question rather than a sequencer-level one, and the distinction is easy to get wrong. A sequencer knows when its own playhead has passed the end of a finite sequence, but with a stagger the last pixel is still `(count − 1) × pixel_delay` behind it — so the sequencer is done while most of the run is still animating. A wipe whose segment was removed on the sequencer's signal would vanish before it had finished crossing. The signal to take a segment away is therefore the segment's own, and it needs no new machinery: a pixel that has passed the end of a finite sequence reports `hold_forever`, and that hold is [exact rather than estimated](#how-a-frame-gets-rendered), so the segment has finished when every one of its pixels says so. Asking the pixels rather than computing the end from `pixel_delay` is what keeps it right for a replaced step 1 or step 2, whose ends the segment cannot compute, and keeps painting and finishing from ever disagreeing.

Finished is a narrower condition than "nothing here will ever change again", and the two must not be confused. A finished segment does hold forever — every pixel sits on `landing_color` — and so does a paused one, and so does a static gradient that was never played, or one drawn by a `ColorFn` on an endless sequence. All of them report `hold_forever` to the frame loop, but only the first is done: the others are holding still on purpose, and removing them would blank the strip. So finished is "nothing here will ever change again" on a sequencer that is playing a finite sequence. Playing rules out the paused segment and the unplayed gradient; finite rules out the endless one, and makes a finite sequence the way a `ColorFn` declares that it is meant to end.

Segments need not tile the strip neatly, and they may overlap; a common arrangement is one segment covering the whole strip as a background with smaller ones painted over it.

**Strip** — the list of segments and the frame loop. It renders all segments into your pixel buffer and folds every segment's "come back in N milliseconds" into a single answer. It also carries the brightness level for everything it covers. A system with two physical strips has two of these, each with its own segments and its own brightness.

Note what is absent. No LED type, no driver reference, no clock object, no callbacks. The strip renders into a buffer you hand it, using a time you hand it. Everything platform-shaped lives outside.

## How a frame gets rendered

Each frame does exactly this:

1. Clear the buffer to black.
2. For each segment, in order: for each of its pixels, work out where it looks, look up the color there, write it. Both halves are the defaults from [two places to plug in](#two-places-to-plug-in) unless the segment replaced one.
3. Apply the strip's brightness and gamma while converting to the 8-bit output.
4. Return when to come back.

Three consequences worth understanding:

**Segments may overlap, and later ones win.** This is not a special feature — it is what happens when you paint in order and don't check. It is worth *allowing* because it makes the common case simpler: rather than tiling the strip exactly and accounting for every pixel, you put a background segment across the whole strip and paint smaller segments over it.

Take a concrete case. The strip is showing a slow blue breathe, something goes wrong, and you want to flash it red without losing the breathe underneath. Without overlap you would have to stop the breathe, remember where it had got to, run the flash, then put it back. With overlap you add a second segment covering the whole strip, driven by a finite sequence that flashes red three times, and place it after the breathing segment in the list. For as long as it is there, it paints over everything beneath it. When the segment reports that it has finished, you remove it from the list, and the breathe is simply there again — still running, still exactly where it would have been, because nothing ever interrupted it.

The limit is sharp and worth knowing up front: a segment paints its whole range opaquely. There is no transparency and no blending, so you cannot have one animation show through another. That is a deliberate boundary — see [non-goals](#non-goals).

**Rendering is a pure function of the segments and the current time.** Nothing accumulates between frames, so there is no stale state to invalidate, and the whole renderer can be tested by calling it with a time and comparing the buffer.

**Skipping work is the caller's job, and step 4's "come back in N ms" answer is what makes it possible.** A strip resting on a steady color reports a long hold, so a caller that sleeps on it wakes seconds apart instead of sixty times a second — skipping not just the hardware update, which re-sends the entire strip because the pixels are daisy-chained and cannot be updated one at a time, but the render as well.

Note what the strip deliberately does *not* offer: a "nothing changed since last time" flag. It looks free, since the deadline is already computed, but the deadline is only half the answer — starting a sequencer or adding a segment between renders changes the picture without moving the clock past anything, and a strip trusting its deadline alone would report no change and freeze. Reporting it properly means a change counter on every sequencer and a dirty flag on the strip, and that buys nothing the caller lacks: everything that moves a pixel without the clock moving is a call the caller itself just made. The clock half it can test with one comparison against the hold it was already handed. Comparing against the previous frame is not an option either — step 1 has overwritten it, and keeping a copy would be per-pixel state.

That answer deserves unpacking, since the consequences above lean on it. The "when to come back" answer has three forms: come back at your frame rate (something is mid-fade), come back after exactly N milliseconds (everything is holding a steady color until then), or don't come back (everything has finished). The second form is the one that pays: it is what a device holding a solid status color reports, and why it can sleep.

Working the answer out is nearly free, because rendering already asks every pixel what color it is, and the same lookup reports how long that color holds. Take the smallest hold across a segment's pixels and you have the moment that segment next changes; take the smallest across the segments and you have the strip's answer. It is the same fold applied twice, and it costs one comparison per pixel on work already being done.

Because it is computed rather than estimated, the answer is exact in every case, including the awkward ones. A paused sequencer's pixels never change at all, so a static gradient reports "don't come back" and costs nothing to hold indefinitely. A comet has pixels mid-fade whose hold is zero, so its segment correctly asks for frame rate. A wipe reports the time until its next pixel starts moving, because a pixel waiting on a negative playhead holds its start color until the playhead reaches zero — and that interval is known. And a chase built from discrete blocks that only step every 200 ms gets told 200 ms, rather than being lumped in with the smooth animations by a cruder rule keyed on whether the segment has spread at all.

## Cookbook

Every one of these uses the same mechanism, and all but the last differ only in their numbers. Values are illustrative.

**Blink** — 30 pixels, red 500 ms, off 500 ms, endless. `pixel_delay = 0`. All pixels read the same point on the timeline, so the whole strip flashes together.

**Breathe** — white ramping up over 1000 ms and down over 1000 ms with eased transitions, endless. `pixel_delay = 0`.

**Comet** — endless, 500 ms total: white for 50 ms, then fading to black over 450 ms. Thirty pixels with `pixel_delay = 16 ms` spreads roughly one loop across the strip, so a single bright head with a fading tail runs down it and wraps seamlessly at the end.

**Rainbow cycle** — endless, hue sweeping the full wheel over 3000 ms. Thirty pixels with `pixel_delay = 100 ms` (3000 ÷ 30) puts the entire spectrum on the strip at once and rotates it.

**Static gradient** — the same sequence and the same `pixel_delay` as the rainbow, but the sequencer seeked to 0 and never played. The spread is there; nothing moves.

**Wipe** — finite, `start_color` black, a 200 ms fade to green, `landing_color` green. With `pixel_delay = 30 ms`, green sweeps down a dark strip and stays, and the segment [finishes a full stagger after its sequencer does](#the-four-building-blocks).

**Progress bar** — this one is the least obvious and the best demonstration that the mechanism is doing real work. Take a finite sequence with `start_color` black, a single 10 ms linear fade to the bar color, and `landing_color` the bar color. Never play the sequencer, and set `pixel_delay = 10 ms`. Now `seek(value × pixel_count × 10 ms)`.

Pixels below the threshold have playheads past the end, so they show the bar color. Pixels above it have negative playheads, so they show black. The one pixel straddling the boundary lands inside the 10 ms fade and shows a partial brightness — a smooth sub-pixel edge, for free. Want a bar whose color changes along its length? Add more steps to the sequence. No meter code exists anywhere in the library.

**Two halves, different animations** — two segments over the same strip covering pixels 0–14 and 15–29, each with its own sequencer.

**Two halves, same animation, converging** — two segments sharing one sequencer, the second with a negative `pixel_delay`, so both halves flow toward the centre. The picture is not a reflection: each segment's index 0 is its own first pixel, so the two pixels k from the centre are `(count − 1) × pixel_delay` apart. A mirror flips the index in a step-1 replacement instead.

**Twinkle** — the only recipe here that replaces a default. An endless sequence that flashes to white over 80 ms and fades back to black over 1900 ms, with the pixel's playhead offset by `hash(i)`, taken across a 1980 ms span, instead of `i × pixel_delay`. The span is the segment's `pixel_delay`, which a replacement for step 1 receives as its parameter. Each pixel sits at its own fixed point in the same loop, so they flash at unrelated moments.

## Supporting decisions

These matter but are not the point. Recorded so they don't get relitigated.

**Instants are `int64_t` milliseconds, durations `uint32_t`, playheads `int32_t`.** Sub-millisecond resolution is not needed here, and concrete types keep sequences as plain data rather than infecting every type with a duration parameter. Platform time converts once, at the edge. The playhead is the one signed quantity in the design, and it has to be: a pixel pointing before the start of a timeline is not an edge case but the mechanism itself — it is the thing `start_color` answers, and the reason a wipe leaves unlit pixels ahead of its wavefront. A 32-bit millisecond counter wraps after 49.7 days, and a sequencer left playing reaches it whatever it plays — a finite sequence that has landed keeps playing for as long as its landing color is wanted. A 64-bit one does not wrap in any span a device runs for, so the sequencer takes `int64_t` and keeps its position exactly, and the extension from a narrower counter happens once, at the edge, with the rest of the platform conversion; a 32-bit reading is refused at compile time, because widening one silently would keep its wrap. Only the playhead handed to the pixels is narrowed, on every read: modulo the loop for an endless sequence, and to a fixed range well past either end for a finite one, which no pixel can tell from the exact value.

**Errors are handled at the granularity each case deserves.** Building an invalid sequence — empty, a zero-duration fade, a landing color on an endless sequence — is a real runtime condition when sequences are built from user input, so the builder reports it. Sequences known at compile time get the same rules checked by the compiler instead. Everything else is total: a sequencer has no operation that can be refused, because it has no states to be in the wrong one of, and rendering never fails. The builder is the only place in the library that reports an error.

**Brightness belongs to the strip.** Each strip carries its own level, applied during the conversion to 8-bit alongside gamma. A device with an underglow strip and an indicator strip gets two independent knobs, one per physical output, which is how people actually think about dimming — you dim *the underglow*, not some subset of an animation.

It deliberately does not go on the segment or the sequencer. Independent multipliers compound, and tracing why a pixel landed at 12% when you asked for 50% through two of them costs more than the flexibility is worth. There is exactly one place to look when something comes out dimmer than expected, and it is the same place for every pixel on that strip.

The limitation this accepts: two regions of the *same* physical strip cannot be dimmed independently at run time. An animation that should always be dim can say so in its own colors, but one whose level needs to move while a neighbour holds steady cannot be expressed. In practice regions wanting independent levels are usually separate hardware anyway, which is exactly the case this does cover.

**Gamma is optional and on by default for fades.** Interpolation happens at 16 bits per channel, but the output is 8, and a linear ramp to black looks like it snaps off near the bottom. A small lookup table at the conversion point fixes it. This costs a few hundred bytes and is the single largest visible quality difference on a strip. The output width is not a precision choice: 8 bits per channel is what common addressable chips take on the wire, so chips with wider channels are a deliberate boundary — see [non-goals](#non-goals).

**White is a fourth channel, and the library never invents it.** RGBW strips such as the SK6812 carry a dedicated white emitter, and a strip library that cannot address it is missing a common case. So a color is red, green, blue and white, sixteen bits each, and white behaves exactly like the other three: a step sets it, interpolation and easing treat it identically, and every mechanism in this document is indifferent to how many channels there are.

What the library deliberately does not do is derive white from the other three. Extraction looks like a one-liner — take the smallest of red, green and blue, move it to the white channel, subtract it from each — and it is a color-science trap. The white emitter has its own color temperature, usually warmer than the RGB mix it replaces, so extraction shifts hue by an amount that depends on the strip in front of you. Established libraries ship several competing algorithms for exactly this, which is the clearest sign available that it is a policy decision rather than a calculation. An application that wants extraction applies it to the colors it builds, where it can be tuned against real hardware. For the same reason HSV conversion leaves white at zero: mapping a hue onto a fourth emitter is the same policy question wearing a different hat.

Whether the fourth channel exists at all is a build-time choice, alongside the two capacities. Left off, nothing changes and nothing is paid for. Turned on, a step grows from twelve bytes to sixteen and color operations do four channels instead of three. An RGB strip in an RGBW build simply leaves white at zero, so mixing strip types in one project costs memory rather than capability.

On Zephyr the white channel reaches the hardware through `led_strip_update_channels()` rather than `led_strip_update_rgb()`, which is RGB-shaped by definition.

**The pixel buffer is written in neutral channel order.** Red, green, blue, and white if the build has it — packed, three or four bytes per pixel with no padding, since the buffer is the one thing in the design that scales with strip length. Chips do not agree on that order: WS2812 wants green first, and other parts differ again. Rearranging it is left entirely outside the core; on Zephyr the module's `led_strip` adapter does it.

Channel order is the last transform in the pipeline and the only one carrying no color meaning. Brightness and gamma are real color math and belong inside. Shuffling bytes within a pixel is not — it cannot be subtly wrong, and nothing downstream of it depends on anything but the bytes themselves. That makes it the natural seam: color above it, wiring below. Taking the order as a parameter instead would also put the first hardware-shaped concept inside a core that has so far kept all of them out.

It costs less than that separation appears to. On Zephyr `led_strip_update_rgb()` takes exactly this layout and the driver applies the devicetree `color-mapping` itself, so the RGB path reorders nothing at all — the only thing to watch is `CONFIG_LED_STRIP_RGB_SCRATCH`, which prefixes `struct led_rgb` with a byte and breaks the layout match. The RGBW path goes through `led_strip_update_channels()`, which takes a raw array already in the chip's own order and maps nothing, so there a permutation is genuinely needed — but it would be needed under any design, including one where the core knew the order, so the neutral layout is not what causes it. And because the permutation is within each pixel rather than across the buffer, it can be done in place. Neutral order does not imply a second copy of the only allocation that grows with the strip.

**Memory.** A sequencer is small: it refers to its sequence rather than holding one, so it is a pointer and its clock state, about two dozen bytes. The step storage lives in the sequence, a fixed capacity chosen at build time (eight steps by default), and a sequence known at compile time sits in flash rather than RAM. A segment is about twenty bytes: its pixel range, the sequencer it points at, the delay, and the two optional plug-in functions. Having several costs nothing worth counting. A strip holds only its list of segments. Nothing scales with pixel count except the pixel buffer itself, and that belongs to you rather than to the library.

## Non-goals

Written down so scope creep has to argue its case.

- No layering, transparency, or blending — segments paint opaquely in order.
- No two-dimensional or matrix support.
- No palettes.
- No power or current limiting.
- No playlists, scene management, or sequences that chain into other sequences.
- No endless sequence that holds a start color until each pixel first reaches it. Negative playheads [wrap](#what-happens-off-the-ends) on an endless sequence, and the static gradient, Twinkle and `seek` all depend on that.
- No per-pixel state of any kind inside the library.
- No transport and no channel ordering in the core: getting pixels to the hardware, in whatever order the chip wants them, belongs to the platform. The Zephyr module is the platform side, and it carries one thin piece of it, the hand-off to a `led_strip` driver in `<stripseq/zephyr/led_strip.h>`, because every Zephyr application would otherwise derive the same permute from the same devicetree property. It stops at the hand-off: the frame loop, the workqueue and the frame rate stay the application's, and nothing in `include/` or `src/` knows the adapter exists.
- No color-space policy — white is carried, never derived.
- No output wider than 8 bits per channel: the 16-bit model exists for blending, and the buffer holds what common addressable chips take.
- No "here is the buffer, draw whatever you like" hook. Effects beyond the built-in vocabulary are reached by replacing one of the [two steps](#two-places-to-plug-in) in the render loop instead, which keeps every other part of the machinery working.

## Deferred

Features the design has room for but leaves out, each until an effect needs it, with what adding it would cost.

**Context pointers for the two plug-in points.** As things stand a replacement for step 1 has one number to configure it, the segment's `pixel_delay`, and a replacement for step 2 has none. That covers twinkle's span and a mirror's delay but nothing richer — a hash seeded per device, a curve whose shape comes from configuration, a wave whose sharpness is a runtime setting. Three things are already settled should it be added. The two points get separate contexts rather than sharing one, since they are independent in every other respect and sharing their data would quietly undo that. The pointer is `const`, because a context is configuration to be read rather than scratch space to be written, and keeping it read-only preserves the property that [rendering is a pure function](#how-a-frame-gets-rendered) of the segments and the current time. And it would add a caller-owned lifetime, since the context must outlive the segment pointing at it — which is the reason to wait. The buffer, a segment's sequencer and a sequencer's sequence are borrowed too, but each of those is something the application has anyway; a context would be an object kept alive for the library's sake alone, and it is worth taking on for a real effect that needs it rather than on speculation.

**A twinkle that rearranges itself every cycle.** Twinkle's pattern is fixed: each pixel keeps its offset into the loop forever. Folding the loop number into the hash would reshuffle it on every cycle, but step 1 never sees a loop number, nor any playhead: it computes an offset from the pixel's position and the segment's `pixel_delay`. The sequencer narrows an endless sequence's playhead into a single loop before handing it out, so the number is gone even from the playhead a replacement for step 2 receives. Supporting it would mean handing step 1 the loop number as a further parameter. The sequencer already has it — the quotient of the same division that narrows the playhead — so it is not per-pixel state, and narrowing it to fit a parameter does no harm to a hash. But it is a new parameter on step 1's type, and it is worth adding for an effect that needs it rather than one that would merely be nice.

**Smooth entry into an animation.** The library has no way to fade in from whatever color was showing when a sequence starts, and switching status colors with a hard jump does look cheap. The name `start_color` invites reusing it for this, but it means something narrower — what lies before a finite timeline — so the feature needs a mechanism of its own rather than inheriting one. The two candidates are a short lead-in sequence that hands over to the main one, which needs some form of chaining, or the sequencer capturing its current color at start and injecting a fade. Both add machinery nothing else in the design needs, so the choice between them belongs to the first application that wants the feature.
