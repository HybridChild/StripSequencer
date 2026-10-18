# Features

## Sequences, sequencers, segments and strips

A `Sequence` describes an animation: a list of steps, each a color, a duration and a way of getting there, and whether the list repeats. Given a playhead in milliseconds, it says what color is there and how long that color holds. It holds no state, touches no hardware, and a sequence known at compile time sits whole in flash.

A `Sequencer` owns a playhead and the one bit of state around it: whether the playhead follows the clock or stands still. It refers to the sequence it has loaded rather than copying it.

A `Segment` is a run of pixels driven by one sequencer, with a `pixel_delay` that makes each pixel look a fixed distance behind its neighbour on the same timeline. It is the only part of the library that knows pixels exist.

A `Strip` holds a list of segments and renders them into a pixel buffer you own, applying brightness and gamma on the way, and returns how long until anything on it changes.

[DESIGN.md](DESIGN.md) explains why the pieces are shaped this way, and [COOKBOOK.md](COOKBOOK.md) puts them together into complete recipes. The examples below assume the cookbook's setup — a `buffer`, a `Sequencer seq` and a `Strip strip` at file scope, `kPixels` pixels long — and a `now()` that returns the platform's clock as `int64_t` milliseconds.

## Table of Contents

- [Building a sequence](#building-a-sequence)
- [Choosing the step capacity](#choosing-the-step-capacity)
- [Colors](#colors)
- [Driving a sequencer](#driving-a-sequencer)
- [The clock](#the-clock)
- [Segments and `pixel_delay`](#segments-and-pixel_delay)
- [Replacing a step of the render loop](#replacing-a-step-of-the-render-loop)
- [The strip and its segment list](#the-strip-and-its-segment-list)
- [Rendering and the hold](#rendering-and-the-hold)
- [Brightness and gamma](#brightness-and-gamma)
- [The pixel buffer](#the-pixel-buffer)
- [Asking a sequence directly](#asking-a-sequence-directly)

## Building a sequence

`SequenceBuilder` is the only way to put steps into a sequence. Every call returns the builder, so a sequence reads as one expression.

```cpp
static constexpr Sequence kTraffic = SequenceBuilder{}
    .hold(colors::green, 4000)
    .fade_to(Color::rgb8(255, 160, 0), 500)
    .hold(colors::red, 4000)
    .build_const();
```

### Steps and transitions

`fade_to(color, ms, transition)` adds a step that arrives at `color` by the end of `ms`, starting from wherever the previous step ended. The transition defaults to `Transition::Linear`. `hold(color, ms)` is shorthand for `fade_to(color, ms, Transition::Step)`.

| Transition | Over the step |
| --- | --- |
| `Step` | Jumps to the color at the start of the step and holds it. |
| `Linear` | Blends evenly from the previous color. |
| `EaseIn` | Starts slowly and accelerates: *t*². |
| `EaseOut` | Starts quickly and decelerates: 1 − (1 − *t*)². |
| `EaseInOut` | Slow at both ends: `EaseIn` for the first half and `EaseOut` for the second, each at double speed. |

All five are integer arithmetic on a 16-bit fraction, exact at both ends and monotonic, so a slow breathe never ticks at a step boundary. The blend covers every channel, white included.

### Instant steps

A `hold` may last 0 ms. It occupies no time, but its color is what the next step fades from, which makes it a waypoint:

```cpp
static constexpr Sequence kFlash = SequenceBuilder{}
    .hold(colors::rgbwhite, 0)          // jump to white...
    .fade_to(colors::black, 1000)       // ...and fade from it, every loop
    .build_const();
```

A fade of 0 ms has nothing to fade over, and the builder rejects it.

### Endless or repeated

A sequence is endless unless told otherwise, and an endless sequence is a ring: its timeline has no start and no end, and any playhead, negative ones included, wraps into the loop. `repeat(n)` makes it finite, `n` passes long. `endless()` says the default out loud.

```cpp
static constexpr Sequence kThreeFlashes = SequenceBuilder{}
    .hold(colors::red, 120)
    .hold(colors::black, 120)
    .repeat(3)
    .build_const();
```

### Start and landing colors

A finite sequence is a line, and both ends have a color. `start_color` is what a playhead before 0 shows: the animation has not reached it yet. `landing_color` is what a playhead past the end shows: the animation has passed it by. Left unset, the start color is black and the landing color is the last step's color, so a finite sequence holds where it finished rather than blanking.

```cpp
static constexpr Sequence kWipe = SequenceBuilder{}
    .start_color(colors::black)
    .fade_to(colors::green, 200)
    .landing_color(colors::green)
    .repeat(1)
    .build_const();
```

The first pass fades from the start color, and every later pass from the last step's color. Staggered across a segment, the two ends are what make a [wipe](COOKBOOK.md#the-recipes) and a [progress bar](COOKBOOK.md#the-recipes): pixels ahead of the wavefront are before the start, pixels behind it are past the end.

An endless sequence has neither color, and the builder rejects both: it never lands, and its negative playheads wrap rather than falling off the start.

### Compile time or run time

`build_const()` checks the sequence while compiling. It is `consteval`, so a sequence that breaks a rule does not compile, and the error names the rule. From gcc:

```text
error: call to non-'constexpr' function 'void stripseq::invalid_sequence::sequence_has_a_zero_duration_fade()'
```

`build(out)` checks the same rules at run time and returns an `Error`, for sequences assembled from user input. It writes `out` only on success, so a rejected rebuild leaves the previous sequence intact.

```cpp
static Sequence user_pulse;   // outlives every sequencer that loads it

bool set_pulse(Color c, uint32_t ms) {
    return SequenceBuilder{}
        .fade_to(c, ms)
        .fade_to(colors::black, ms)
        .build(user_pulse) == Error::None;
}
```

Which to call depends on where the values come from: `build_const()` compiles wherever the whole expression is constant, inside an ordinary function included, and stops compiling the moment a color or duration comes from a variable.

| `Error` | Rule |
| --- | --- |
| `NoSteps` | A sequence needs at least one step. |
| `TooManySteps` | No more steps than `STRIPSEQ_MAX_STEPS`. |
| `ZeroDurationFade` | Only a `Step` transition may last 0 ms. |
| `ZeroRepeatCount` | `repeat(0)` is not a way to say forever; `endless()` is. |
| `LandingColorOnEndless` | An endless sequence never lands. |
| `StartColorOnEndless` | An endless sequence's negative playheads wrap, so nothing is before it. |
| `TooLong` | At most 2^29 ms, about 6.2 days, in total, or per loop if endless. |
| `ZeroLengthLoop` | An endless loop needs some length to wrap by. |

The error is sticky: once a call has failed, the rest do nothing and `build()` reports the first failure, so a step past the capacity neither overwrites the last one nor vanishes silently.

## Choosing the step capacity

Every sequence is allocated at `STRIPSEQ_MAX_STEPS` steps, eight by default, whether it uses them or not. A step is 12 bytes, or 16 with the white channel, so a sequence is 116 bytes at the default, or 152 with white.

- **Most animations** fit in eight: blink, breathe, comet and wipe use one or two steps, and the rainbow uses six.
- **A sequence from `build_const()`** costs flash rather than RAM, so each step of capacity added costs one step's size of flash per constant sequence.
- **A sequence built at run time** costs RAM, the whole capacity of it.
- **A sequencer** only refers to its sequence, so its 24 bytes do not change with the capacity.

The capacity is global rather than per sequence, so one sequence that needs sixteen steps sizes every sequence to sixteen. Set it, like the other three options, as described in the [README](../README.md#configuration).

## Colors

A `Color` is red, green and blue at sixteen bits each, plus white when the build has [`STRIPSEQ_WHITE_CHANNEL`](../README.md#configuration). Sixteen bits are for blending: a long fade between two dim colors keeps its smoothness, and the precision is spent only at the conversion to the 8-bit buffer.

```cpp
constexpr Color amber = Color::rgb8(255, 160, 0);           // 8-bit; 255 maps to exactly 65535
constexpr Color deep  = Color::rgb(0, 0, 20000);            // 16-bit
constexpr Color teal  = Color::hsv(0x7000, 255, 200);       // hue, saturation, value
constexpr Color blend = Color::lerp(amber, deep, 32768);    // halfway; 0 = amber, 65535 = deep
```

Every factory is `constexpr`, so a palette is built at compile time and lives in flash. A color is always made through one of them, never as `Color{…}`: the brace form compiles in both builds and means something different in each.

### HSV

`Color::hsv(hue, sat, val)` takes a hue across the whole `uint16_t` range, so stepping around the wheel is plain addition and wraps for free, and saturation and value as `0..255`. It is integer arithmetic throughout, and leaves white at 0.

### The palette

| Constant | Value |
| --- | --- |
| `colors::black` | all channels 0 |
| `colors::red` | red at full scale |
| `colors::green` | green at full scale |
| `colors::blue` | blue at full scale |
| `colors::rgbwhite` | red, green and blue at full scale, white at 0 |

### The white channel

An RGBW build adds `Color::rgbw(r, g, b, w)`, and white then behaves exactly like the other three channels: a step sets it, and every transition blends it. The library never derives white from red, green and blue, because the white emitter's color depends on the strip, so any extraction is a policy to tune against real hardware rather than a calculation. Nothing in the palette names the white emitter; write `Color::rgbw(0, 0, 0, 65535)` where it is used. An RGB strip in an RGBW build simply leaves white at 0.

## Driving a sequencer

A sequencer has four control calls, each of which stores a number or two and returns:

```cpp
seq.load(kBlink);        // refer to kBlink; playhead 0, not playing
seq.play(now());         // the playhead follows the clock from where it stands
seq.pause(now());        // it stops, and keeps its value
seq.seek(250);           // it is set directly, and stands still until play()
```

There is no state machine to get wrong, and so no call that can fail: playing or not playing is the only state, and every call is valid in both. `play()` while playing and `pause()` while paused change nothing. The usual verbs are one call or two:

| To | Call |
| --- | --- |
| Start from the top | `seek(0)`, then `play(now())` |
| Stop | `seek(0)` |
| Resume | `play(now())` |
| Restart a different sequence | `load(other)`, then `play(now())` |
| Unload | `load(no_sequence)` |

`load()` refers to the sequence rather than copying it, so the sequence must outlive the sequencer's use of it, and a temporary will not compile. A sequence rebuilt with `build()` while it is loaded shows on the next render, with the playhead where it was.

### Pause and resume

Pausing stores the exact position, and playing again puts the clock origin wherever makes the position carry on from it, so a pause of any length costs nothing and the animation picks up exactly where it stood. The [cookbook](COOKBOOK.md#play-pause-and-seek) works an example through call by call.

### Seeking as an input

A sequencer that is never played turns its playhead into a value the application sets. Seeked to 0 under a rainbow's stagger, it is a [static gradient](COOKBOOK.md#the-recipes); seeked to a value under a finite fade, it is a [progress bar](COOKBOOK.md#the-recipes), with a part-lit pixel at the edge for free.

### Querying

`playing()` says whether the playhead follows the clock, `sequence()` returns the loaded sequence, and `playhead(now)` returns where it stands. For an endless sequence that is the position within the loop; for a finite one it is the position itself, clamped to 2^30 ms either side of 0, far past both ends, so a seeked value reads back exactly.

### Sharing

Any number of segments may point at one sequencer, across one strip or several, and they then run in lockstep. A sequencer does not know which segments read it.

## The clock

Every call that takes a time takes `int64_t` milliseconds, which does not wrap in any span a device runs for. Under Zephyr that is `k_uptime_get()`. A bare-metal platform with a 32-bit tick extends it once, in application code; the [cookbook](COOKBOOK.md#the-clock) has a ten-line `Clock64`.

Passing a 32-bit reading straight in is a compile error rather than a bug seven weeks later:

```cpp
seq.play(k_uptime_get_32());   // does not compile: the overload is deleted
```

Widening a `uint32_t` to `int64_t` loses nothing, so it converts silently and no warning fires, but it keeps the 49.7-day wrap as a jump of 2^32 ms backwards. Every function taking `now_ms` has a deleted overload for unsigned types of 32 bits or fewer, and an `int` literal such as `play(1000)` still compiles.

Read the clock once per frame and pass the same reading to every call in it. Nothing in the library reads a clock of its own.

## Segments and `pixel_delay`

A segment covers `count` pixels from `first`, driven by a sequencer:

```cpp
Segment s(0, kPixels, seq);
s.set_pixel_delay(16);       // each pixel 16 ms behind the one before it
strip.add(s);
```

Pixel *i* of the segment looks at the sequencer's playhead minus *i* × `pixel_delay`. That one expression, with different numbers, is most of the library's vocabulary:

| `pixel_delay` | Sequencer | What you see |
| --- | --- | --- |
| `0`, the default | playing | The whole segment as one LED: blink, fade, breathe. |
| tens of ms | playing | A wave rolling down the strip: chase, comet, travelling pulse. |
| loop ÷ pixel count | playing | The whole animation spread across the segment, rotating: a rainbow cycle. |
| loop ÷ pixel count | not playing | The same spread, frozen: a static gradient. |
| negative | either | The same effects, running the other way. |

### Speed or fit

`set_pixel_delay(ms)` sets a speed: a comet at 16 ms per pixel travels at the same rate on any length of strip. To fit an animation to the segment instead, whatever its length, divide the span by the pixel count with `spread()`:

```cpp
s.set_pixel_delay_q8(spread(3000, kPixels));   // the 3000 ms loop across the whole segment
```

The delay is stored in 1/256 ms, so a division that does not come out even leaves no visible seam where a ring joins up. `spread()` returns that unit, which is why it goes through `set_pixel_delay_q8()`.

Both setters return `false`, and change nothing, for a delay that would put the last pixel more than about 2.3 hours behind the first.

### Finishing

`finished(now)` is true when the segment's sequencer is playing a finite sequence and no pixel of the segment will change again. That comes a full stagger after the sequencer's own playhead reaches the end, since the last pixel is (count − 1) × `pixel_delay` behind it. A segment that is paused, never played, or driven by an endless sequence is never finished, however still it holds.

## Replacing a step of the render loop

Rendering a pixel is two steps: where on the timeline does it look, and what color is there. Either can be replaced on a segment with a plain function pointer, and the rest of the machinery — segments, overlap, brightness, gamma and the hold — keeps working. `nullptr` restores the default.

### Where a pixel looks: `OffsetFn`

```cpp
using OffsetFn = int32_t (*)(uint16_t index, uint16_t count, int32_t delay_q8);
```

It returns how many milliseconds behind the sequencer's playhead pixel `index` looks, and the default is `(index * delay_q8) >> 8`. A mirror flips the index:

```cpp
static int32_t mirrored(uint16_t index, uint16_t count, int32_t delay_q8) {
    return ((count - 1 - index) * delay_q8) >> 8;
}

s.set_offset_fn(mirrored);
```

`index` and `count` are within the segment, so a segment behaves the same wherever it sits. The function's one parameter is the segment's `pixel_delay`, which it may read however it documents: [Twinkle](COOKBOOK.md#the-recipes) reads it as the span its pseudo-random offsets scatter across.

### What color is there: `ColorFn`

```cpp
using ColorFn = Sample (*)(int32_t playhead_ms, uint16_t index, uint16_t count);
```

It receives the pixel's own playhead, after the offset, and returns a `Sample`: the color, and how long it holds. A meter's color scale, green at one end and red at the other, needs no playhead at all:

```cpp
static Sample meter_scale(int32_t /*playhead_ms*/, uint16_t index, uint16_t count) {
    const auto t = static_cast<uint16_t>(index * 65535u / (count > 1 ? count - 1u : 1u));
    return {Color::lerp(colors::green, colors::red, t), hold_forever};
}

s.set_color_fn(meter_scale);
```

The hold is a contract rather than a hint. The strip will not render again before the smallest hold expires, so a function that changes every frame returns 0, one that holds a color for a while says how long, and one that never changes again returns `hold_forever`. Report it honestly and a segment driven by your function sleeps exactly as long as one driven by a step list.

A playhead can be negative, or past the end of whatever the function has in mind, since each pixel's playhead is the sequencer's less its offset.

A function that draws from time alone, and never reads a step, still needs its segment's sequencer to be playing something. Load `timebase`: an endless sequence of one black step, as long as the builder allows, so the playhead the function receives counts milliseconds since `play()` and wraps back to 0 only every 2^29 ms, about 6.2 days.

```cpp
seq.load(timebase);
seq.play(now());
Segment s(0, kPixels, seq);
s.set_color_fn(flame);   // a function of index and time, nothing stored
strip.add(s);
```

### Helpers for replacements

Three pieces of the library's own arithmetic are public because a replacement keeps needing them, and the obvious version of each is subtly wrong:

```cpp
uint32_t h = hash(index);                              // pseudo-random, from the pixel alone
uint16_t level = lerp(dim, bright, t);                 // Color::lerp for one 16-bit value
uint16_t eased = ease(Transition::EaseInOut, t);       // the curve a fade follows
```

`hash` is the only randomness a replacement has, since neither step may keep state: hash the index for a fixed scatter, or the index and a slot of time for something that changes. Its bits are all equally mixed, so `hash(i) % span` scatters evenly — which a multiplicative hash such as `i * 2654435761` reduced the same way does not, and a twinkle built on one fires as a chase. `lerp` and `ease` are exact at both ends, monotonic, and free of the 32-bit division a Cortex-M0 makes a library call of; `ease` is exactly what `at()` applies, so a value eased by hand moves like a fade.

Both functions are pure: the only state they receive is their arguments. A replacement configured beyond `pixel_delay` — a hash seeded per device, say — is not expressible; see [deferred](DESIGN.md#deferred).

## The strip and its segment list

A strip is built over your buffer, and works out the pixel count from it:

```cpp
static uint8_t buffer[kPixels * Strip::bytes_per_pixel];
constinit static Strip strip(buffer);
```

`constinit` at file scope means no constructor runs before `main()`. A buffer whose size is not a whole number of pixels, usually an RGB-sized buffer in an RGBW build, does not compile.

### Adding and removing

`add(segment)` copies the segment onto the end of the list, and returns `false` if the list is full or the segment runs past the end of the strip. `remove(i)` takes out entry `i` and shifts the later ones down, and `clear()` takes out every entry, which is how an application switching animations starts the next one from an empty list; the brightness stays as it was. Up to `STRIPSEQ_MAX_SEGMENTS` segments, four by default, fit on one strip.

The list is paint order. Later segments paint over earlier ones wherever they overlap, opaquely, so the usual arrangement is a background across the whole strip first and smaller or temporary segments after it. The [alert recipe](COOKBOOK.md#the-recipes) paints three red flashes over a running breathe, and when the flashes are removed the breathe is still there, exactly where it would have been.

### Finding a segment again

An index is a position, not a handle: `remove()` shifts everything after it. To find a segment again, ask which sequencer it is driven by, sweeping backwards so a removal never skips an entry:

```cpp
for (uint8_t i = strip.segment_count(); i > 0; --i)
    if (strip.segment(i - 1).drives(alert_seq))
        strip.remove(i - 1);
```

### Dropping finished segments

`drop_finished(now)` removes every segment that reports [finished](#finishing), and returns how many went. It is a call in the frame loop rather than something the strip does on its own, because a finished segment still paints its landing color, and a wipe depends on that.

### Resizing a segment

`strip.segment(i)` returns a reference that can be assigned:

```cpp
strip.segment(0) = Segment(0, kPixels / 2, seq);
```

A segment holds no animation state, so replacing one costs nothing and the animation underneath does not flinch. An assigned segment that runs past the end of the strip is clipped when painted, since a reference cannot refuse it the way `add()` does.

## Rendering and the hold

`render(now)` clears the buffer, paints every segment at `now`, and returns when to come back:

| Return | Meaning | The frame loop |
| --- | --- | --- |
| `0` | Something is mid-fade. | Render again at your frame rate. |
| *n* | Nothing changes for exactly *n* ms. | Sleep *n* ms. |
| `hold_forever` | Nothing on this strip will change again. | Don't reschedule until a control call changes something. |

The answer is the smallest hold across every pixel of every segment, worked out from the same lookups that painted them, so it is exact rather than estimated. A blink reports the time to its next edge, a wipe the time until its next pixel starts moving, a paused gradient `hold_forever`. A device holding a steady status color wakes when the color is due to change, not sixty times a second.

Rendering is a pure function of the segments and `now`: nothing carries over between frames, and two calls with the same `now` produce the same buffer. Control calls change what the next render shows without the clock moving, so after one, render again rather than waiting out the previous hold.

## Brightness and gamma

`strip.set_brightness(level)` sets a level from 0 to 255 for everything on the strip, applied during the conversion to 8 bits; 255, the default, leaves colors as they are. It is one knob per strip, so a device with two strips has two independent levels, and there is one place to look when a pixel comes out dimmer than expected.

Brightness scales each channel while it is still 16-bit, and only then does gamma map it to the emitted level, so 128 looks half as bright. The price is at the dim end: with gamma on, channels round to 0 at different levels, so a mixed color shifts hue as it dims, and below 22 even full white is black.

Gamma, on by default, maps the 16-bit channel to the 8-bit output through a 256-byte table with an exponent of 2.5, so a linear fade to black looks linear rather than snapping off near the bottom. `STRIPSEQ_GAMMA=0` converts linearly and links no table. Blends happen before gamma, so with gamma on a fade's midpoint is halfway to the eye.

## The pixel buffer

The buffer is packed red, green, blue, and white in an RGBW build, one byte per channel with no padding: `Strip::bytes_per_pixel` is 3 or 4. That is a neutral order, and putting the bytes into the order a chip wants, WS2812's green-red-blue for one, is the platform's job.

Under Zephyr, the RGB path needs no reordering: `led_strip_update_rgb()` takes exactly this layout and applies the devicetree's `color-mapping` itself. The RGBW path goes through `led_strip_update_channels()`, which takes bytes already in the chip's order, so it needs a within-pixel shuffle, done in place. The [cookbook](COOKBOOK.md#on-zephyr) shows both, and [`samples/strip_demo/`](../samples/strip_demo/) is a complete application.

## Asking a sequence directly

`Sequence::at(playhead)` is public, so a sequence can drive something that is not a strip, such as a single PWM status LED, with the same descriptions and the same sequencer:

```cpp
const Sample s = seq.sequence().at(seq.playhead(now()));
set_pwm(s.color.r, s.color.g, s.color.b);   // 16-bit channels, before brightness and gamma
```

The `Sample` carries the hold as well, so the sleep logic is the same as a strip's, with one addition: a strip treats a sequencer that is not playing as holding forever, and `at()` cannot know about the sequencer, so check `playing()` first.
