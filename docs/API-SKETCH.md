# RgbSequencer — API sketch

The calling surface for the mechanism described in [DESIGN.md](DESIGN.md). Signatures only, no bodies.

Its purpose is to check that the design survives contact with real code, and the test is the [cookbook](#the-cookbook-as-code): the ten recipes from DESIGN.md's cookbook plus the overlay example from its render section, written out as they would actually be called. If Blink is not three lines, something is wrong upstream.

How it gets built is in [IMPLEMENTATION-PLAN.md](IMPLEMENTATION-PLAN.md). Decisions are recorded where the thing they decide is declared; [open questions](#open-questions) at the end lists what is still genuinely undecided.

## Contents

- [Language requirements](#language-requirements)
- [Build-time configuration](#build-time-configuration)
- [Color](#color)
- [Sequence](#sequence)
- [Sequencer](#sequencer)
- [Segment](#segment)
- [Strip](#strip)
- [The cookbook as code](#the-cookbook-as-code)
- [The Zephyr adapter](#the-zephyr-adapter)
- [Open questions](#open-questions)

## Language requirements

C++20, freestanding. Only `<cstdint>`, `<cstddef>` and `<type_traits>` are included; no exceptions, no RTTI, no heap, and no dynamic initialisation at file scope.

C++20 is a real requirement rather than a preference, and two things need it. `consteval` gives compile-time sequences the validation DESIGN.md promises them — an invalid sequence known at compile time becomes a compile error, with no runtime cost and no runtime trap. `constinit` lets a strip be declared at file scope without a constructor running before `main`. Under C++17 both degrade: validation falls back to the runtime path plus a `static_assert` the caller writes, and file-scope objects acquire dynamic initialisers.

## Build-time configuration

Four knobs, all overridable from the build system.

```cpp
// rgbseq/config.h

#ifndef RGBSEQ_MAX_STEPS
#define RGBSEQ_MAX_STEPS 8        // steps a sequence can hold; sets sequencer size
#endif

#ifndef RGBSEQ_MAX_SEGMENTS
#define RGBSEQ_MAX_SEGMENTS 4     // segments a strip can hold; sets strip size
#endif

#ifndef RGBSEQ_WHITE_CHANNEL
#define RGBSEQ_WHITE_CHANNEL 0    // 1 adds the fourth channel everywhere
#endif

#ifndef RGBSEQ_GAMMA
#define RGBSEQ_GAMMA 1            // 1 links the correction table (~256 bytes)
#endif
```

They are macros rather than template parameters. For the white channel that is forced: it changes a struct's layout, which a template parameter cannot do without splitting every downstream type as well. For the two capacities it is a choice, and the reasoning differs between them.

### Why step capacity is a macro

A step count is not always known at compile time. Sequences are built at run time from values that may come from user input — the whole reason the builder reports errors rather than trapping — so `N` cannot be a template parameter for those. Templates could serve only compile-time sequences, which means shipping a template family *and* a fixed-capacity fallback, with every downstream type obliged to cope with both.

Three further objections, none decisive alone. The fluent builder would go viral: each `.fade_to()` would have to return a `SequenceBuilder<N+1>` by value rather than a reference, since its type changes with every call — workable, and a known pattern, but it degrades error messages badly. `Segment` could no longer hold a plain `Sequencer*`, because `Sequencer<2>` and `Sequencer<6>` are unrelated types; the fixes are a virtual base, costing a vtable pointer per sequencer and an indirect call in the per-pixel path, or templating `Segment` and `Strip` too, which would stop one strip driving segments whose sequencers differ in size. And every distinct `N` instantiates its own copy of `at()`, the duration arithmetic and the builder, where a macro emits one — on an MCU flash is usually the tighter budget.

What this costs is a global ceiling: one animation needing sixteen steps forces every sequencer in the project to sixteen. At the default capacity a `Sequence` is about 116 bytes and a `Sequencer` about 128, whether it holds a six-step rainbow or a two-step blink, so a right-sized `Sequencer<2>` would have come in near 44. Across four undersized sequencers the waste is roughly one pixel buffer's worth on a hundred-pixel strip, against Zephyr-class hardware with tens to hundreds of kilobytes.

If that ceiling ever becomes the real problem, the cheaper escape is caller-supplied storage — `Sequencer(Step* buf, uint8_t cap)` — which gives per-sequencer sizing with no templates and one copy of the code. It is not free: DESIGN.md cites sequencers owning their steps outright as the design's model for avoiding caller-owned lifetimes, so it trades away a stated property. It can be done later without disturbing the mechanism, which is the reason not to pre-empt it.

### Why segment capacity is a macro

None of the objections above applies here. A segment count is known where the strip is declared, no builder is involved, and nothing in the library holds a `Strip*`, so `Strip<N>` would have infected nothing.

It is a macro for two smaller reasons. The ceiling is cheaper than the one already accepted for steps, because segment capacity applies per strip and a system has one or two strips rather than many sequencers — an unused slot costs 20 bytes against a pixel buffer of several hundred for the same strip. And two mechanisms for one job would need explaining every time either was read.

What this costs is per-strip sizing: an indicator strip needing six segments forces the underglow to six as well.

## Color

Sixteen bits per channel, with white present only when the build asks for it.

```cpp
// rgbseq/color.h
#include <cstdint>

namespace rgbseq {

struct Color {
    uint16_t r, g, b;
#if RGBSEQ_WHITE_CHANNEL
    uint16_t w;
#endif

    // Named constructors only, never aggregate initialisation: `Color{255,0,0}`
    // compiles in an RGB build and is wrong-but-legal in an RGBW one.
    static constexpr Color rgb(uint16_t r, uint16_t g, uint16_t b);
    static constexpr Color rgb8(uint8_t r, uint8_t g, uint8_t b);      // 8-bit convenience, scaled up
    static constexpr Color hsv(uint16_t hue, uint8_t sat, uint8_t val); // hue 0..65535; leaves w at 0
#if RGBSEQ_WHITE_CHANNEL
    static constexpr Color rgbw(uint16_t r, uint16_t g, uint16_t b, uint16_t w);
#endif

    // The blend every transition already performs, exposed because a custom
    // ColorFn is the one place outside the library that needs it. Covers all
    // channels, w included. t: 0 = a, 65535 = b. Works in the same linear
    // 16-bit space the sequence does -- gamma is applied later, at the 8-bit
    // conversion, so blending here is unaffected by it.
    static constexpr Color lerp(Color a, Color b, uint16_t t);
};

namespace colors {
inline constexpr Color black    = Color::rgb(0, 0, 0);
inline constexpr Color red      = Color::rgb8(255, 0, 0);
inline constexpr Color green    = Color::rgb8(0, 255, 0);
inline constexpr Color blue     = Color::rgb8(0, 0, 255);
inline constexpr Color rgbwhite = Color::rgb8(255, 255, 255);  // all three channels; any build
// No constant for the W emitter: write Color::rgbw(0, 0, 0, 65535) at the point
// of use, where the channels are visible.
}

}  // namespace rgbseq
```

`inline constexpr` keeps these constant-initialised, so they live in flash and run no code before `main`.

**Nothing is named after the W emitter** — no `Color::white()` factory and no `colors::white`. The word is unambiguous in a *channel* context: Zephyr's `LED_COLOR_ID_WHITE` sits beside `LED_COLOR_ID_RED` in `color-mapping`, DMX fixtures label the channel the same way, and nobody misreads either. Put it in a namespace beside `colors::red` and `colors::blue` and the reader's frame shifts from channels to colors, where "white" means "the strip looks white" rather than "drive the fourth die". Those differ visibly — the phosphor emitter is usually warmer and dimmer than the RGB mix, by an amount the library cannot know, which is the same reason DESIGN.md refuses to derive white at all. `colors::rgbwhite` keeps its awkward name because it is the portable one, and because the awkwardness stops it being read as authoritative.

This matches the field: Adafruit NeoPixel and NeoPixelBus expose the channel and name no constant, while FastLED and WLED name the *derivation policy* instead.

## Sequence

Pure description. A sequence holds its steps inline, so a sequencer can own one by value and get snapshot semantics — rebuilding a sequence cannot disturb one already running.

```cpp
// rgbseq/sequence.h

namespace rgbseq {

enum class Transition : uint8_t { Step, Linear, EaseIn, EaseOut, EaseInOut };

struct Step {
    Color      color;        // the color arrived at by the end of this step
    Transition transition;   // how it is approached from the previous color
    uint32_t   duration_ms;
};

// Field order above is load-bearing, not stylistic. `duration_ms` follows the
// one-byte `transition` so that it lands in the padding trailing `color`.
// Putting the duration first -- the order that reads more naturally -- costs
// four bytes per step in an RGB build, multiplied by RGBSEQ_MAX_STEPS in every
// sequencer. An RGBW build is 16 either way, since the eight-byte color leaves
// the duration already aligned; only RGB is sensitive, and RGB is the default.
static_assert(sizeof(Step) == (RGBSEQ_WHITE_CHANNEL ? 16 : 12),
              "Step has picked up padding -- check field order.");

// What every lookup returns, built-in or replacement alike.
//
// hold_ms is how long this color stays put, measured forward from the playhead
// it was asked about. It is not advice or an estimate: the strip folds it into
// the "when to come back" answer and will not render again before it expires,
// so understating it wastes frames and overstating it drops them.
//
//   0             changing continuously -- mid-fade. Asks for frame rate.
//   n             this exact color holds for n ms, then something changes.
//   hold_forever  it never changes again: a paused sequencer, or a finite
//                 sequence resting on its start or landing color.
//
// It costs the built-in lookup nothing, because finding the step containing the
// playhead already yields that step's end.
struct Sample {
    Color    color;
    uint32_t hold_ms;
};

inline constexpr uint32_t hold_forever = UINT32_MAX;

enum class Error : uint8_t {
    None,
    NoSteps,
    TooManySteps,
    ZeroDurationFade,        // a Step transition may be instant; a fade may not
    ZeroRepeatCount,
    LandingColorOnEndless,   // an endless sequence never lands
    StartColorOnEndless,     // negative playheads wrap, so nothing is ever "before"
};

class Sequence {
public:
    constexpr Sequence() = default;          // empty; valid to hold, not to render

    // The one question a sequence answers. The playhead is signed and unbounded:
    // negative means before the timeline, large means past the end. Instants and
    // durations elsewhere are uint32_t; only playheads are signed, and they must
    // be -- a pixel pointing before the start of a timeline is the mechanism,
    // not an edge case.
    Sample at(int32_t playhead_ms) const;

    // These two are here for the library's own use rather than the caller's:
    // Sequencer needs the loop length to re-base an endless origin, and
    // Segment::finished() needs the total. Endless reads as hold_forever.
    constexpr uint32_t loop_duration_ms() const;   // one pass through the steps
    constexpr uint32_t total_duration_ms() const;  // loop x repeats

private:
    Step     steps_[RGBSEQ_MAX_STEPS] = {};   // capacity: always allocated in full
    uint8_t  count_ = 0;                      // of those, how many are live
    uint16_t repeats_ = 0;                    // 0 = endless
    Color    start_ = {};                     // finite only; build() resolves the default
    Color    landing_ = {};                   // finite only; build() resolves the default
};

class SequenceBuilder {
public:
    constexpr SequenceBuilder() = default;

    constexpr SequenceBuilder& fade_to(Color c, uint32_t ms, Transition t = Transition::Linear);
    constexpr SequenceBuilder& hold(Color c, uint32_t ms);         // sugar for Transition::Step
    constexpr SequenceBuilder& endless();                          // the default
    constexpr SequenceBuilder& repeat(uint16_t times);
    constexpr SequenceBuilder& start_color(Color c);               // finite only
    constexpr SequenceBuilder& landing_color(Color c);             // finite only

    // Runtime path: reports rather than traps, because a sequence assembled from
    // user input can legitimately be invalid.
    [[nodiscard]] constexpr Error build(Sequence& out) const;

    // Compile-time path: the same rules, enforced by constant evaluation.
    // consteval, so an invalid sequence is a compile error rather than a
    // runtime check that cannot fail. Named as build()'s sibling, for when the
    // rules are checked.
    consteval Sequence build_const() const;

private:
    // Whether start_color() and landing_color() were called. Color has no spare
    // value to mean "unset" -- black is a legitimate start color -- and the
    // builder needs the distinction twice: to reject either call on an endless
    // sequence even when the color is black, and to know when build() should
    // supply the default instead. Sequence keeps neither bit, so at() never
    // branches on one. Not std::optional: <optional> is outside the three
    // headers the core may include.
    bool start_set_ = false;
    bool landing_set_ = false;
};

}  // namespace rgbseq
```

**Which of the two you call is decided by where the values come from, not by where the call sits.** `build_const()` is `consteval`, so it is legal anywhere the whole expression is a constant expression — including inside an ordinary function, which is why the cookbook uses it in runtime code. The moment a color or duration comes from a variable, the expression is no longer constant and `build_const()` stops compiling; that is when `build()` takes over:

```cpp
Sequence s;
if (builder.build(s) != Error::None)
    return;                          // reject the input, not the program
seq.load(s);
```

## Sequencer

The playhead and the state around it. It owns a copy of its sequence.

```cpp
// rgbseq/sequencer.h

namespace rgbseq {

class Sequencer {
public:
    void load(const Sequence& s);     // copies; playhead 0, not playing
    void play(uint32_t now_ms);       // advance from wherever the playhead is
    void pause(uint32_t now_ms);      // stop advancing; the playhead stays put
    void seek(int32_t playhead_ms);   // place the playhead; stops advancing

    bool playing() const;
    const Sequence& sequence() const;

    // Non-const on purpose. For an endless sequence this re-bases the origin by
    // whole loops so the 49.7-day wrap is never reached, and that mutation has
    // to happen somewhere the sequencer is being asked for its position anyway.
    int32_t playhead(uint32_t now_ms);

private:
    Sequence sequence_;
    uint32_t origin_ms_ = 0;    // playing: the instant playhead 0 sits at
    int32_t  frozen_ = 0;       // not playing: the playhead itself
    bool     playing_ = false;
};

}  // namespace rgbseq
```

Nothing stores a playhead as such. `origin_ms_` and `frozen_` are the two representations, `playing_` says which is authoritative, and the value is derived on every call — a stored playhead that got advanced each frame would be the tape model DESIGN.md rejects.

**There is one bit of state here, not a state machine.** A sequencer is either advancing or it is not. The obvious extra states are all derivable and none of them earns storage: *complete* is `playhead > total_duration_ms()` on a finite sequence, and is in any case not the signal anyone wants, since removing a segment keys off `Segment::finished()`. *Idle* is a sequence with no steps, which `Sequence` already represents. *Paused* and *loaded-but-never-played* are the same condition arrived at from different directions, and nothing downstream can tell them apart or needs to.

Collapsing them removes the verbs that existed only to move between them. `start(now)` was `seek(0)` then `play(now)`; `stop()` was `seek(0)`; `resume(now)` was `play(now)`; `unload()` was `load({})`. Re-triggering an alert that has already run is `load()` again, since loading resets the playhead — which is what the overlay recipe does anyway.

**`seek` stops the playhead advancing**, and that is the only coherent reading. Placing a playhead by hand on a moving target would mean re-deriving the origin, which needs a `now_ms` the call does not take. The recipes that seek — static gradient, progress bar — never play at all, so it costs them nothing, and scrubbing a running animation is `seek` then `play`.

Every operation is total. There are no state-dependent refusals to document, which leaves the builder as the only place in the library that reports an error.

## Segment

The only part of the design that knows pixels exist.

```cpp
// rgbseq/segment.h

namespace rgbseq {

// Step 1 of the render loop: where does this pixel look?
//
//   playhead_ms  the sequencer's own playhead, unstaggered -- the single number
//                the sequencer owns, identical for every pixel in the segment.
//   index        the pixel's position *within the segment*, 0-based -- not its
//                position on the strip. A segment therefore behaves identically
//                wherever it sits, and hash(index) stays stable if it moves.
//   count        how many pixels the segment covers. The only way a replacement
//                can normalise position: mirroring is (count - 1 - index).
//   returns      this pixel's playhead, which is what step 2 gets asked about.
//
// Note what is absent: pixel_delay. The offset it produces is precisely the
// difference between the returned value and playhead_ms -- the default is
// playhead_ms - index * pixel_delay. A replacement computes that difference
// for itself, so the delay is applied neither before the call nor after it.
// Installing a replacement therefore clears the segment's pixel_delay, and the
// setters refuse while one is in place, so no dormant value can linger. Twinkle relies on this -- hash(index) has to *be* the offset,
// not be added to a linear ramp already in place. A replacement that wants a
// configurable stagger must bake the constant in until context pointers
// arrive (see DESIGN.md, Deferred).
//
// Both plug-in types take (playhead_ms, index, count) in that order, so the
// pipeline reads playhead in -> playhead out -> color out with the spatial
// context trailing in both.
using PlayheadFn = int32_t (*)(int32_t playhead_ms, uint16_t index, uint16_t count);

// Step 2: what color is at that playhead?
//
//   playhead_ms  *this pixel's* playhead -- what step 1 returned, already
//                staggered. Deliberately not the same number PlayheadFn was
//                handed, despite the shared parameter name.
//   index        as above: position within the segment, 0-based.
//   count        as above: pixels in the segment. Present so that a color may
//                depend on position directly, not only through time.
//   returns      a Sample. See Sample for the contract; reporting the hold
//                honestly is what lets a segment driven by your function sleep
//                exactly as long as one driven by a step list. Returning 0
//                always is safe and costs only power; overstating is a bug.
using ColorFn = Sample (*)(int32_t playhead_ms, uint16_t index, uint16_t count);

// Fit intent: the delay that spreads total_ms evenly across count pixels.
// A free function rather than a Segment method, because the result is just a
// delay -- once computed there is nothing about it left to remember.
constexpr int32_t spread(uint32_t total_ms, uint16_t count);

class Segment {
public:
    Segment(uint16_t first, uint16_t count, Sequencer& seq);

    // Strip holds an array of these, so they must be default-constructible.
    // A default-constructed segment covers no pixels and paints nothing.
    constexpr Segment() = default;

    // Both setters feed the *default* step 1 only, and each returns false if a
    // PlayheadFn is installed -- the value is rejected rather than stored
    // dormant. See the note on PlayheadFn above.
    bool set_pixel_delay(int32_t ms);            // milliseconds of lag per pixel
    bool set_pixel_delay_q8(int32_t ms_q8);      // 1/256 ms, which is how it is stored

    // Installing a PlayheadFn clears any pixel_delay, so a segment has either
    // one or the other and never a dormant leftover. nullptr restores the
    // default, with a delay of 0 until one is set again.
    void set_playhead_fn(PlayheadFn fn);
    void set_color_fn(ColorFn fn);                // nullptr restores the default

    // True once every pixel here has passed the end of a finite sequence --
    // a full stagger later than the sequencer's own playhead reaching the end.
    // Narrower than "nothing here will change again", too: a sequencer that is
    // not playing holds forever and is never finished.
    bool finished(uint32_t now_ms) const;

private:
    uint16_t   first_ = 0;
    uint16_t   count_ = 0;
    Sequencer* sequencer_ = nullptr;
    int32_t    delay_q8_ = 0;
    PlayheadFn where_ = nullptr;
    ColorFn    what_  = nullptr;
};                                 // 20 bytes on a 32-bit target

}  // namespace rgbseq
```

## Strip

The segment list and the frame loop. The strip owns its segments; the pixel buffer is yours.

```cpp
// rgbseq/strip.h

namespace rgbseq {

class Strip {
public:
    // Neutral packed layout: R,G,B[,W], one byte per channel, no padding.
    // Channel order is the platform's business, not the library's.
    static constexpr size_t bytes_per_pixel = RGBSEQ_WHITE_CHANNEL ? 4 : 3;

    // The buffer's size is deduced rather than passed, and the pixel count
    // follows from it, so neither can drift from the array it describes.
    // constexpr so the object can be constinit at file scope.
    template <size_t B>
    constexpr explicit Strip(uint8_t (&buffer)[B]);

    // Position in the list is paint order: index 0 is painted first and later
    // entries overwrite it wherever they overlap. Segments are only appended,
    // so a background goes in first and overlays after it.
    bool add(const Segment& s);   // on top of everything already present
    bool remove(uint8_t at);      // false if at >= count

    // remove shifts every entry after `at`, so an index is a position and never
    // a handle: one held across a removal may now name a different segment, and
    // a Segment& returned earlier may now refer to one. Re-find the segment you
    // want rather than caching where it was. With a handful of segments that is
    // cheaper than the bookkeeping stable handles would need.

    uint8_t segment_count() const;

    // The non-const reference is assignable, which is how a segment is resized:
    // `strip.segment(i) = Segment(0, n, seq);`. A segment holds no animation
    // state -- the playhead lives in the sequencer -- so replacing one costs
    // nothing and the animation underneath does not flinch.
    Segment& segment(uint8_t at);
    const Segment& segment(uint8_t at) const;

    // Removes every segment reporting finished(), and returns how many went.
    // Not automatic: a finished segment keeps painting its landing color, which
    // is exactly what the wipe recipe depends on. This is opt-in cleanup for
    // overlays, and it exists mainly so the reverse iteration is written once.
    uint8_t drop_finished(uint32_t now_ms);

    void set_brightness(uint8_t level);   // 0..255, applied during 8-bit conversion
    uint8_t brightness() const;

    // Paints every segment into the buffer and returns when to come back: the
    // smallest hold across every pixel of every segment. 0 = call again at your
    // frame rate, n = nothing changes for n ms, hold_forever = nothing on this
    // strip will ever change again.
    uint32_t render(uint32_t now_ms);

private:
    // Owned outright, which is the point: there is no array to hand in, so two
    // strips cannot be pointed at the same segments.
    Segment  segments_[RGBSEQ_MAX_SEGMENTS];
    uint8_t  count_ = 0;
    uint8_t* buffer_ = nullptr;
    uint16_t pixel_count_ = 0;
    uint8_t  brightness_ = 255;
};

}  // namespace rgbseq
```

## The cookbook as code

Setup is shown once and applies to every recipe below.

```cpp
using namespace rgbseq;

static constexpr uint16_t kPixels = 30;

static uint8_t   buffer[kPixels * Strip::bytes_per_pixel];
static Sequencer seq;

constinit static Strip strip(buffer);   // pixel count deduced from the buffer;
                                        // segment capacity from RGBSEQ_MAX_SEGMENTS
```

**Blink** — the whole strip flashing together.

```cpp
seq.load(SequenceBuilder{}.hold(colors::red, 500).hold(colors::black, 500).build_const());
seq.play(now());
strip.add(Segment(0, kPixels, seq));      // pixel_delay defaults to 0
```

Three lines, and the third is only there because a segment has to exist. That is the test the design had to pass.

**Breathe** — same shape, eased fades instead of holds.

```cpp
seq.load(SequenceBuilder{}
    .fade_to(colors::rgbwhite, 1000, Transition::EaseInOut)
    .fade_to(colors::black,    1000, Transition::EaseInOut)
    .build_const());
seq.play(now());
strip.add(Segment(0, kPixels, seq));
```

**Comet** — a bright head with a fading tail, running down the strip.

```cpp
seq.load(SequenceBuilder{}
    .hold(colors::rgbwhite, 50)
    .fade_to(colors::black, 450)
    .build_const());
seq.play(now());

Segment s(0, kPixels, seq);
s.set_pixel_delay(16);        // speed intent: 16 ms per pixel whatever the length
strip.add(s);
```

**Rainbow cycle** — six steps around the hue wheel, spread across the strip and rotating.

```cpp
inline constexpr Sequence kRainbow = SequenceBuilder{}
    .fade_to(Color::hsv(0x2AAA, 255, 255), 500)   // red   -> yellow
    .fade_to(Color::hsv(0x5555, 255, 255), 500)   //       -> green
    .fade_to(Color::hsv(0x8000, 255, 255), 500)   //       -> cyan
    .fade_to(Color::hsv(0xAAAA, 255, 255), 500)   //       -> blue
    .fade_to(Color::hsv(0xD555, 255, 255), 500)   //       -> magenta
    .fade_to(Color::hsv(0x0000, 255, 255), 500)   //       -> red
    .build_const();

seq.load(kRainbow);
seq.play(now());

Segment s(0, kPixels, seq);
s.set_pixel_delay_q8(spread(3000, kPixels));   // the whole wheel across the segment
strip.add(s);
```

**Static gradient** — the identical setup, never started.

```cpp
seq.load(kRainbow);
seq.seek(0);                  // seeked, never played: the playhead does not move

Segment s(0, kPixels, seq);
s.set_pixel_delay_q8(spread(3000, kPixels));
strip.add(s);
```

The only difference between a rotating rainbow and a frozen one is whether `play()` or `seek()` was called.

The rainbow is a named constant because two recipes share it. `build_const()` makes the definition a constant expression, so `kRainbow` is constant-initialised into flash like the `colors::` palette, nothing runs before `main`, and a malformed sequence fails to compile at the definition rather than at either use. Each `load()` still copies it into the sequencer, since a sequencer owns its sequence; the constant adds a full-capacity `Sequence` in flash rather than saving the copy in RAM, which is the price of writing the steps once.

**Wipe** — green sweeping down a dark strip and staying.

```cpp
seq.load(SequenceBuilder{}
    .start_color(colors::black)
    .fade_to(colors::green, 200)
    .landing_color(colors::green)
    .repeat(1)
    .build_const());
seq.play(now());

Segment s(0, kPixels, seq);
s.set_pixel_delay(30);
strip.add(s);
```

The segment is deliberately left in place after it finishes: `landing_color` is what keeps the strip green. Calling `drop_finished()` here would blank it.

**Progress bar** — a sequencer that never runs, whose playhead is the value.

```cpp
seq.load(SequenceBuilder{}
    .start_color(colors::black)
    .fade_to(colors::red, 10)
    .landing_color(colors::red)
    .repeat(1)
    .build_const());

Segment s(0, kPixels, seq);
s.set_pixel_delay(10);
strip.add(s);

// Called whenever the value changes.
void set_progress(uint8_t percent) {
    seq.seek(static_cast<int32_t>(percent) * kPixels * 10 / 100);
}
```

No meter code exists anywhere in the library, and the pixel straddling the boundary lands inside the 10 ms fade and comes out part-lit.

**Two halves, different animations** — two segments, two sequencers.

```cpp
static Sequencer seq_a, seq_b;   // loaded and started as above

strip.add(Segment(0,  15, seq_a));
strip.add(Segment(15, 15, seq_b));
```

**Two halves, mirrored** — two segments, one sequencer, opposite delays.

```cpp
Segment left(0, 15, seq);
Segment right(15, 15, seq);
left.set_pixel_delay(20);
right.set_pixel_delay(-20);
strip.add(left);
strip.add(right);
```

**Twinkle** — the only recipe that replaces a default.

```cpp
static int32_t scatter(int32_t playhead_ms, uint16_t index, uint16_t /*count*/) {
    uint32_t h = index * 2654435761u;      // Knuth's multiplicative hash
    return playhead_ms - static_cast<int32_t>(h % 1980);
}

seq.load(SequenceBuilder{}
    .fade_to(colors::rgbwhite, 80)
    .fade_to(colors::black, 1900)
    .build_const());
seq.play(now());

Segment s(0, kPixels, seq);
s.set_playhead_fn(scatter);
strip.add(s);
```

**Alert over a background** — the one recipe that exercises the segment list rather than the animation mechanism. This is DESIGN.md's overlap example: a slow blue breathe running forever, with three red flashes painted over it when something goes wrong.

```cpp
static Sequencer breathe_seq;
static Sequencer alert_seq;

void start_background() {
    breathe_seq.load(SequenceBuilder{}
        .fade_to(colors::blue,  1500, Transition::EaseInOut)
        .fade_to(colors::black, 1500, Transition::EaseInOut)
        .build_const());
    breathe_seq.play(now());
    strip.add(Segment(0, kPixels, breathe_seq));    // first in the list, painted first
}

void raise_alert() {
    alert_seq.load(SequenceBuilder{}
        .hold(colors::red,   120)
        .hold(colors::black, 120)
        .repeat(3)
        .build_const());
    alert_seq.play(now());
    strip.add(Segment(0, kPixels, alert_seq));      // added last, so painted on top
}
```

The breathe is never touched. It keeps running underneath for as long as the alert covers it, and reappears exactly where it would have been.

Removal is the interesting half, and all the caller writes is one line in the frame loop:

```cpp
strip.drop_finished(now());
```

It is a library call rather than caller code because the obvious hand-written version is subtly wrong. This is what it does inside:

```cpp
// Strip::drop_finished, not application code.
for (uint8_t i = segment_count(); i > 0; --i)
    if (segment(i - 1).finished(now_ms))
        remove(i - 1);
```

The loop runs backwards. Removing an entry shifts every later one down, so a forward loop skips the segment after each removal — and it skips it silently, leaving a finished overlay painting over the background until something else happens to shift the list. Running downwards means the only entries that move are ones already visited. The endless background never reports finished, so it needs no special case.

Note what this recipe did *not* want: a `pop()`. The natural shape is a sweep of the whole list rather than "remove the top" — and once the loop exists, a sweep costs nothing extra, copes with two overlays finishing out of order, and spares the caller from remembering which index it added.

## The Zephyr adapter

Not part of the library. This is what the platform side looks like, and it is where the neutral buffer layout pays off.

```cpp
const struct device* dev = DEVICE_DT_GET(DT_ALIAS(led_strip));

void frame_work(struct k_work* work) {
    const uint32_t now = k_uptime_get_32();   // sampled once for the whole frame
    strip.drop_finished(now);
    const uint32_t hold = strip.render(now);

#if RGBSEQ_WHITE_CHANNEL
    permute_in_place(buffer, kPixels);           // into the chip's channel order
    led_strip_update_channels(dev, buffer, sizeof(buffer));
#else
    led_strip_update_rgb(dev, (struct led_rgb*)buffer, kPixels);
#endif

    if (hold != hold_forever)
        k_work_reschedule(&frame_work_item, K_MSEC(hold ? hold : 16));
}
```

The RGB path casts the buffer straight through, because neutral order is exactly what `led_strip_update_rgb()` wants and the driver applies `color-mapping` itself. That cast is only valid while `CONFIG_LED_STRIP_RGB_SCRATCH` is off, which is worth a `static_assert` in the real adapter. The RGBW path has no such luck: `led_strip_update_channels()` takes a raw array already in the chip's own order and maps nothing, so `permute_in_place` is the adapter's own — a within-pixel byte shuffle needing no second buffer.

## Open questions

None outstanding. Decisions are recorded beside the declarations they govern.

One note on what `render` deliberately does *not* return: a "nothing changed, skip the transfer" flag. It looks free — the strip knows when its next change falls due — but the deadline is only half the answer, because starting a sequencer or adding a segment between renders changes the picture without moving the clock past anything. Reporting it correctly would need a change counter on every sequencer and a dirty flag on the strip. That machinery buys nothing the caller cannot work out for itself: everything that moves a pixel without the clock moving is a call the caller just made, and the clock half is a comparison against the hold it was already handed. A caller that wants to skip transfers should sleep on the hold instead, which saves the frame as well as the transfer.
