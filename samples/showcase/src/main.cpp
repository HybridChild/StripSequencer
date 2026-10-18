/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The showcase: what the library can make a strip do, one animation per press
 * of the user button.
 *
 * Unlike strip_demo, nothing here is a check. Each animation is chosen to look
 * like something, and to show one idea from the library: a stagger, a replaced
 * step 1, a playhead driven from outside, segments painted over each other, a
 * whole fire drawn by a step 2.
 *
 * The frame loop and ../common/strip_adapter.h are strip_demo's, plus one
 * thing it never needs: a tick, for the animations that change on their own
 * schedule rather than the sequencer's -- the wipe changing direction, the
 * progress value moving, the alert firing. Everything runs on the system workqueue, which serialises the work
 * items, so a tick or a press can never land halfway through a render and
 * nothing here locks.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <stripseq/stripseq.h>

#include "strip_adapter.h"

using namespace stripseq;

#define STRIP_NODE DT_ALIAS(led_strip)

static constexpr uint16_t kPixels = DT_PROP(STRIP_NODE, chain_length);

/* The halves of the split animations. On an odd strip the right one takes the
 * extra pixel. */
static constexpr uint16_t kLeft = kPixels / 2;
static constexpr uint16_t kRight = kPixels - kLeft;

static uint8_t   buffer[kPixels * Strip::bytes_per_pixel];
static Sequencer seq;
static Sequencer seq_b;
constinit static Strip strip(buffer);

static const struct device* const strip_dev = DEVICE_DT_GET(STRIP_NODE);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

/* A hold of 0 means "come back at the frame rate", and this is the frame rate. */
static constexpr uint32_t kFramePeriodMs = 16;

/* Eleven RGBW pixels at full white draw more than a USB-powered bench should
 * be asked for. Applied in the library's linear space, before gamma, so 74 is
 * about 5% of full light and leaves a channel 12 output levels from dark to
 * full: enough for a fade to read as smooth, where 64 leaves eight and a slow
 * fade climbs them as a visible staircase. The flame lives in dull reds,
 * which round to black this dim, so it asks for 120; its average pixel is
 * dim enough that it still draws little. */
static constexpr uint8_t kBrightness = 74;
static constexpr uint8_t kFlameBrightness = 120;

/* ---- The sequences ------------------------------------------------------ */

static constexpr Sequence kBlink =
    SequenceBuilder{}.hold(colors::red, 500).hold(colors::black, 500).build_const();

static constexpr Sequence kBreathe = SequenceBuilder{}
    .fade_to(colors::rgbwhite, 1000, Transition::EaseInOut)
    .fade_to(colors::black,    1000, Transition::EaseInOut)
    .build_const();

static constexpr Sequence kComet = SequenceBuilder{}
    .hold(colors::rgbwhite, 50)
    .fade_to(colors::black, 450)
    .build_const();

static constexpr Sequence kRainbow = SequenceBuilder{}
    .fade_to(Color::hsv(0x2AAA, 255, 255), 500)   // red   -> yellow
    .fade_to(Color::hsv(0x5555, 255, 255), 500)   //       -> green
    .fade_to(Color::hsv(0x8000, 255, 255), 500)   //       -> cyan
    .fade_to(Color::hsv(0xAAAA, 255, 255), 500)   //       -> blue
    .fade_to(Color::hsv(0xD555, 255, 255), 500)   //       -> magenta
    .fade_to(Color::hsv(0x0000, 255, 255), 500)   //       -> red
    .build_const();

/* Green holds on each pixel for exactly the stagger across the strip, so the
 * first pixel starts out the moment the last has landed: the wipe on runs
 * straight into the wipe off, with the strip full for an instant between. */
static constexpr uint32_t kWipeFadeMs = 150;
static constexpr int32_t kWipeDelayMs = 20;
static constexpr uint32_t kWipeAcrossMs =
    static_cast<uint32_t>((kPixels - 1) * kWipeDelayMs);

static constexpr Sequence kWipe = SequenceBuilder{}
    .start_color(colors::black)
    .fade_to(colors::green, kWipeFadeMs)
    .hold(colors::green, kWipeAcrossMs)
    .fade_to(colors::black, kWipeFadeMs)
    .repeat(1)
    .build_const();

/* A pass is the wipe on and off once, ending a full stagger after the sequence
 * does, when the last pixel goes dark. A cycle is a pass from pixel 0, a pass
 * from the far end, and two seconds dark. The tick counts the cycle out in
 * kWipeTickMs steps, so a pass must be a whole number of them. */
static constexpr uint32_t kWipePassMs = 2 * kWipeFadeMs + 2 * kWipeAcrossMs;
static constexpr uint32_t kWipeTickMs = 100;
static constexpr uint32_t kWipePassTicks = kWipePassMs / kWipeTickMs;
static constexpr uint32_t kWipeCycleTicks = (2 * kWipePassMs + 2000) / kWipeTickMs;
static_assert(kWipePassMs % kWipeTickMs == 0, "a pass must end on a tick");

/* One fade per pixel, each kProgressStepMs long and staggered by the same, so
 * a playhead of n steps lights n pixels and the one beyond partway. */
static constexpr int32_t kProgressStepMs = 10;
static constexpr Sequence kProgress = SequenceBuilder{}
    .start_color(colors::black)
    .fade_to(colors::red, kProgressStepMs)
    .landing_color(colors::red)
    .repeat(1)
    .build_const();

/* Only a clock for the twinkle and the flame, whose ColorFns ignore the steps.
 * The sequencer hands an endless sequence's playhead over modulo its loop, so
 * the loop is the longest the builder allows, 2^29 ms or about six days, and
 * neither repeats sooner than that. */
static constexpr Sequence kClock =
    SequenceBuilder{}.hold(colors::black, uint32_t{1} << 29).build_const();

/* The breathe bottoms out lit rather than dark. Gamma acts after the sequence,
 * so the floor is high in linear terms: at gamma 2.5, 144 leaves the chip at
 * about a quarter of full blue. At kBrightness the fade crosses 9 output
 * levels, one every ten frames or so. */
static constexpr Color kDimBlue = Color::rgb8(0, 0, 144);
static constexpr Sequence kBackground = SequenceBuilder{}
    .fade_to(colors::blue, 1500, Transition::EaseInOut)
    .fade_to(kDimBlue,     1500, Transition::EaseInOut)
    .build_const();

static constexpr Sequence kAlert = SequenceBuilder{}
    .hold(colors::red,   200)
    .hold(colors::black, 200)
    .repeat(5)
    .build_const();

/* The alert covers the 3rd to the 6th pixel and leaves the background
 * breathing on either side. */
static constexpr uint16_t kAlertFirst = 2;
static constexpr uint16_t kAlertCount = 4;
static_assert(kAlertFirst + kAlertCount <= kPixels, "the alert must fit on the strip");

/* ---- The replaced step 1s ----------------------------------------------- */

/* The default stagger counted from the far end, so a half wearing it is the
 * reflection of a half wearing the default. */
static int32_t mirror(uint16_t index, uint16_t count, int32_t delay_q8) {
    return ((count - 1 - index) * delay_q8) >> 8;
}

/* ---- The twinkle's step 2 ----------------------------------------------- */

/* A replaced step 1 cannot make a twinkle random: whatever offset it gives a
 * pixel, the pixel replays the same loop from it, and the whole strip repeats
 * once a loop. A ColorFn sees each pixel's playhead, so it can choose afresh
 * every slot. Time is cut into kSlotMs slots; in each, a pixel sparks once at
 * a moment hashed from the pixel and the slot, or a quarter of the time not
 * at all. Nothing is stored, so the same instant always draws the same picture. */
static constexpr uint32_t kSlotMs = 400;
static constexpr uint32_t kSparkMs = 30;
static constexpr uint32_t kSparkFadeMs = 330;

/* The spark that pixel index fires in slot, t ms after the slot began, as a
 * 16-bit level. t may run past the slot, which is how the previous slot's
 * spark fades on into this one. */
static uint16_t spark(uint16_t index, uint32_t slot, uint32_t t) {
    const uint32_t h = hash(slot * 2654435761u ^ hash(index + 1u));
    if ((h & 3) == 0)
        return 0;
    /* A multiply-high, so the start reads the top bits and the skip the bottom
     * two, independently. */
    const uint32_t start = static_cast<uint32_t>((uint64_t{h} * kSlotMs) >> 32);
    if (t < start)
        return 0;
    const uint32_t age = t - start;
    if (age < kSparkMs)
        return 65535;
    if (age >= kSparkMs + kSparkFadeMs)
        return 0;
    return static_cast<uint16_t>(65535 - (age - kSparkMs) * 65535 / kSparkFadeMs);
}

/* A spark lasts less than a slot, so only this slot's and the last one's can
 * be lit. The hold is 0, the one promise always kept: with eleven pixels
 * sparking about twice a second, something is nearly always mid-fade, and a
 * longer one would buy no sleep. */
static Sample twinkle(int32_t playhead_ms, uint16_t index, uint16_t /*count*/) {
    const uint32_t p = static_cast<uint32_t>(playhead_ms);   /* an endless playhead is never negative */
    const uint32_t slot = p / kSlotMs;
    const uint32_t t = p % kSlotMs;
    uint16_t v = spark(index, slot, t);
    if (slot > 0) {
        const uint16_t last = spark(index, slot - 1, t + kSlotMs);
        v = last > v ? last : v;
    }
    return {Color::rgb(v, v, v), 0};
}
static_assert(kSparkMs + kSparkFadeMs <= kSlotMs, "a spark must be over within a slot of starting");

/* ---- The flame's step 2 ------------------------------------------------- */

/* A fire drawn the way a shader draws one: each pixel's color is a function of
 * its height and the time, and of nothing else. The usual LED fire keeps a
 * heat per pixel and diffuses it upward frame by frame. That array is exactly
 * the per-pixel state the library refuses, and this fire does without it.
 *
 * Pixel 0 is the base. Every height and speed is a fraction of the strip, so
 * the fire fills any length. Four layers, three of them made of one
 * ingredient, smoothed value noise:
 *   - the flame's height: four octaves of noise in time, from a slow draught
 *     to a fast flicker, so it breathes, leaps and gutters at once;
 *   - turbulence rising through it: noise over height and time whose features
 *     climb faster than the flame grows, so bright knots run up the body and
 *     tongues tear off the top and vanish. It bites deeper the higher it is,
 *     so the base stays a steady core and the tip is ragged;
 *   - embers: born at the flame's tip at hashed moments, each rising at its
 *     own speed and cooling as it goes, drawn between two pixels in
 *     proportion rather than jumping from one to the next;
 *   - a blackbody palette that turns heat into color, from dull red through
 *     orange to yellow, and on an RGBW strip into the white die at the top.
 *
 * Heights, levels and heats are all Q16: 65536 is the whole strip, or full. */

/* Value noise over a plane: a hashed level at every whole (x, y), blended
 * between on an ease-in-out, which flattens into and out of every lattice
 * point. A linear blend turns a corner there, and on a slow fade the eye
 * catches the corner. x and y are Q16, so a lattice cell is 65536 units wide,
 * and the plane repeats after 65536 cells with no seam, because the neighbour
 * of the last cell is the first. A coordinate that overflows is therefore
 * harmless, which is what lets time drive one without bound. */
static uint16_t lattice(uint32_t x, uint32_t y, uint32_t seed) {
    return static_cast<uint16_t>(hash(x * 2654435761u ^ hash(y ^ seed)) >> 16);
}

static uint16_t noise(uint32_t x, uint32_t y, uint32_t seed) {
    const uint32_t x0 = x >> 16, x1 = (x0 + 1) & 0xFFFF;
    const uint32_t y0 = y >> 16, y1 = (y0 + 1) & 0xFFFF;
    const uint16_t sx = ease(Transition::EaseInOut, static_cast<uint16_t>(x));
    const uint16_t sy = ease(Transition::EaseInOut, static_cast<uint16_t>(y));
    return lerp(lerp(lattice(x0, y0, seed), lattice(x1, y0, seed), sx),
                lerp(lattice(x0, y1, seed), lattice(x1, y1, seed), sx), sy);
}

/* Noise in time alone: a fresh level every period_ms, eased between. */
static uint16_t wobble(uint32_t t, uint32_t period_ms, uint32_t seed) {
    return noise(t * (65536u / period_ms), 0, seed);
}

/* The flame's mean height and how far its noise moves it either way. The
 * octaves are weighted toward the slow ones, so it mostly breathes and
 * sometimes leaps. */
static constexpr int32_t kFlameMean = 38000;
static constexpr int32_t kFlameSwing = 40000;
static constexpr int32_t kFlameLowest = 9000;

static uint32_t flame_height(uint32_t t) {
    const int32_t f = (3 * wobble(t, 2600, 11) + 3 * wobble(t, 800, 12) +
                       2 * wobble(t, 300, 13) + wobble(t, 110, 14)) / 9;
    const int32_t h = kFlameMean + (f - 32768) * kFlameSwing / 32768;
    return static_cast<uint32_t>(h < kFlameLowest ? kFlameLowest : h);
}

/* One octave of turbulence. Given in human units -- feature size in
 * thousandths of the strip, rise in thousandths a second, and how long a
 * feature keeps its shape -- and stored as lattice steps, so a frame only
 * multiplies. */
struct Octave {
    uint32_t cell;    /* thousandths of the strip per lattice cell */
    uint32_t climb;   /* lattice units the field rises a millisecond */
    uint32_t churn;   /* lattice units it changes by a millisecond */
    uint32_t seed;
};

static constexpr Octave octave(uint32_t cell, uint32_t rise, uint32_t life_ms, uint32_t seed) {
    return {cell, 65536u * rise / (1000u * cell), 65536u / life_ms, seed};
}

static constexpr Octave kCoarse = octave(300, 1400, 240, 21);
static constexpr Octave kFine = octave(130, 1900, 100, 22);

/* Noise carried upward: the level at height y now is the level at y + rise
 * a moment later, changing as it goes. */
static int32_t rising(const Octave& o, uint32_t y, uint32_t t) {
    return noise(t * o.climb - y * 1000u / o.cell, t * o.churn, o.seed);
}

/* How far the turbulence moves the flame's edge either way, pixel by pixel. */
static constexpr int32_t kLick = 16000;

static uint16_t flame_heat(uint32_t y, uint32_t t) {
    const int32_t turbulence = (2 * rising(kCoarse, y, t) + rising(kFine, y, t)) / 3;
    const int32_t edge =
        static_cast<int32_t>(flame_height(t)) + (turbulence - 32768) * kLick / 32768;
    if (edge <= static_cast<int32_t>(y))
        return 0;
    /* Full at the base, falling to nothing at the edge... Measured from the
     * pixel rather than the edge, because a leaping flame's edge is above the
     * strip and edge * 65535 overflows, where the pixel's height never does. */
    const uint32_t body = 65535u - y * 65535u / static_cast<uint32_t>(edge);
    /* ...less what the turbulence takes, from a fifth at the base to four
     * fifths at the top of the strip. */
    const uint32_t bite = 13000u + y * 39000u / 65536u;
    const uint32_t keep = 65535u - bite * static_cast<uint32_t>(65535 - turbulence) / 65535u;
    return static_cast<uint16_t>(body * keep / 65535u);
}

/* Half the slots launch an ember, at a hashed moment within the slot, from
 * wherever the flame's tip was then. It rises at 0.6 to 1.2 strips a second
 * for half a second to a second, glowing amber at first and cooling to
 * nothing, with a fast flicker of its own. A life is shorter than
 * kEmberSlots slots, so no older slot can still be burning. */
static constexpr uint32_t kEmberSlotMs = 280;
static constexpr uint32_t kEmberSlots = 5;
static constexpr uint32_t kEmberLongestMs = 950;
static_assert(kEmberLongestMs < (kEmberSlots - 1) * kEmberSlotMs,
              "an ember must die before its slot leaves the window");

static uint16_t ember_heat(uint32_t y, uint32_t t, uint16_t count) {
    const uint32_t slot = t / kEmberSlotMs;
    const uint32_t pixel = 65536u / count;
    uint32_t hottest = 0;
    for (uint32_t k = 0; k < kEmberSlots && k <= slot; ++k) {
        const uint32_t s = slot - k;
        const uint32_t h = hash(s * 2654435761u ^ 0x5EEDu);
        if (h & 1)
            continue;
        const uint32_t birth =
            s * kEmberSlotMs + static_cast<uint32_t>((uint64_t{h} * kEmberSlotMs) >> 32);
        if (t < birth)
            continue;
        const uint32_t age = t - birth;
        const uint32_t life = 450u + (h >> 1) % (kEmberLongestMs - 450u);
        if (age >= life)
            continue;
        const uint32_t speed = 40u + (h >> 12) % 40u;   /* Q16 strips a millisecond */
        const uint32_t at = flame_height(birth) + speed * age;
        const uint32_t off = at > y ? at - y : y - at;
        if (off >= pixel)
            continue;
        const uint32_t cooling = 52000u * (life - age) / life;
        const uint32_t flicker = 40000u + wobble(t, 45, s) * 25535u / 65535u;
        const uint32_t share = (pixel - off) * 65535u / pixel;
        const uint32_t heat = cooling * flicker / 65535u * share / 65535u;
        hottest = heat > hottest ? heat : hottest;
    }
    return static_cast<uint16_t>(hottest);
}

/* Heat to color, through stops at a black body's colors: dull red, red,
 * orange, amber, yellow. The colors are in the 16-bit space a sequence's are,
 * before gamma, which is why orange carries so much green: gamma takes most
 * of it away again. */
struct Stop {
    uint16_t heat;
    Color color;
};

static constexpr Stop kPalette[] = {
    {0,     colors::black},
    {9800,  Color::rgb8(80, 0, 0)},
    {24900, Color::rgb8(200, 25, 0)},
    {39300, Color::rgb8(255, 105, 0)},
    {52400, Color::rgb8(255, 170, 10)},
#if STRIPSEQ_WHITE_CHANNEL
    {65535, Color::rgbw(65535, 52000, 10000, 36000)},
#else
    {65535, Color::rgb8(255, 220, 90)},
#endif
};

static Color heat_color(uint16_t heat) {
    size_t i = 1;
    while (heat > kPalette[i].heat)
        ++i;
    const Stop& lo = kPalette[i - 1];
    const Stop& hi = kPalette[i];
    return Color::lerp(lo.color, hi.color,
                       static_cast<uint16_t>(uint32_t(heat - lo.heat) * 65535u /
                                             uint32_t(hi.heat - lo.heat)));
}

/* Every pixel is mid-change in a fire, so the hold is always 0. */
static Sample flame(int32_t playhead_ms, uint16_t index, uint16_t count) {
    const uint32_t t = static_cast<uint32_t>(playhead_ms);   /* an endless playhead is never negative */
    const uint32_t y = (2u * index + 1u) * 32768u / count;   /* the pixel's centre */
    const uint16_t body = flame_heat(y, t);
    const uint16_t ember = ember_heat(y, t, count);
    return {heat_color(body > ember ? body : ember), 0};
}

/* ---- The animations ----------------------------------------------------- */

static void play_whole(const Sequence& s, int64_t now) {
    seq.load(s);
    seq.play(now);
    strip.add(Segment(0, kPixels, seq));
}

static void show_blink(int64_t now) { play_whole(kBlink, now); }

static void show_breathe(int64_t now) { play_whole(kBreathe, now); }

static void show_comet(int64_t now) {
    seq.load(kComet);
    seq.play(now);
    Segment s(0, kPixels, seq);
    s.set_pixel_delay(24);
    strip.add(s);
}

static void show_rainbow(int64_t now) {
    seq.load(kRainbow);
    seq.play(now);
    Segment s(0, kPixels, seq);
    s.set_pixel_delay_q8(spread(3000, kPixels));
    strip.add(s);
}

static void show_gradient(int64_t) {
    seq.load(kRainbow);
    seq.seek(0);
    Segment s(0, kPixels, seq);
    s.set_pixel_delay_q8(spread(3000, kPixels));
    strip.add(s);
}

/* Ticks into the current cycle. The reversed pass is the same sequence with
 * the stagger counted from the far end, so the direction is the segment's
 * step 1 and the sequence never knows. */
static uint32_t wipe_ticks;

static void start_pass(int64_t now, OffsetFn where) {
    seq.load(kWipe);   /* re-trigger: back to playhead 0 */
    seq.play(now);
    strip.segment(0).set_offset_fn(where);
}

static void wipe(int64_t now) {
    wipe_ticks = wipe_ticks + 1 < kWipeCycleTicks ? wipe_ticks + 1 : 0;
    if (wipe_ticks == 0)
        start_pass(now, nullptr);
    else if (wipe_ticks == kWipePassTicks)
        start_pass(now, mirror);
}

static void show_wipe(int64_t now) {
    Segment s(0, kPixels, seq);
    s.set_pixel_delay(kWipeDelayMs);
    strip.add(s);
    wipe_ticks = 0;
    start_pass(now, nullptr);
}

/* The bar fills over three seconds, rests full for two, and starts again.
 * Values past 100 are the rest. */
static constexpr uint32_t kProgressTickMs = 30;
static uint8_t progress;

static void set_progress(uint8_t percent) {
    seq.seek(static_cast<int32_t>(percent) * kPixels * kProgressStepMs / 100);
}

static void show_progress(int64_t) {
    seq.load(kProgress);
    progress = 0;
    set_progress(0);
    Segment s(0, kPixels, seq);
    s.set_pixel_delay(kProgressStepMs);
    strip.add(s);
}

static void advance_progress(int64_t) {
    progress = progress < 166 ? progress + 1 : 0;
    set_progress(progress < 100 ? progress : 100);
}

static void show_halves(int64_t now) {
    seq.load(kBreathe);
    seq.play(now);
    seq_b.load(kRainbow);
    seq_b.play(now);
    strip.add(Segment(0, kLeft, seq));
    Segment right(kLeft, kRight, seq_b);
    right.set_pixel_delay_q8(spread(3000, kRight));
    strip.add(right);
}

/* Opposite delays: both halves flow toward the centre, but the two pixels k
 * from it are a whole half's stagger apart, so the picture is not a
 * reflection. Compare the next animation. */
static void show_converging(int64_t now) {
    seq.load(kComet);
    seq.play(now);
    Segment left(0, kLeft, seq);
    Segment right(kLeft, kRight, seq);
    left.set_pixel_delay(40);
    right.set_pixel_delay(-40);
    strip.add(left);
    strip.add(right);
}

static void show_mirrored(int64_t now) {
    seq.load(kComet);
    seq.play(now);
    Segment left(0, kLeft, seq);
    Segment right(kLeft, kRight, seq);
    left.set_pixel_delay(40);
    right.set_pixel_delay(40);
    right.set_offset_fn(mirror);
    strip.add(left);
    strip.add(right);
}

static void show_twinkle(int64_t now) {
    seq.load(kClock);
    seq.play(now);
    Segment s(0, kPixels, seq);
    s.set_color_fn(twinkle);
    strip.add(s);
}

static void show_flame(int64_t now) {
    seq.load(kClock);
    seq.play(now);
    Segment s(0, kPixels, seq);
    s.set_color_fn(flame);
    strip.add(s);
}

/* The background is seq, first in the list and so painted first; the alert is
 * seq_b, added last and painted on top. drop_finished() in the frame loop is
 * what takes the alert away again once its five flashes are done. */
static void show_alert(int64_t now) {
    seq.load(kBackground);
    seq.play(now);
    strip.add(Segment(0, kPixels, seq));
}

static void raise_alert(int64_t now) {
    seq_b.load(kAlert);
    seq_b.play(now);
    for (uint8_t i = 0; i < strip.segment_count(); ++i)
        if (strip.segment(i).drives(seq_b))
            return;   // still flashing: the load() alone restarts it
    strip.add(Segment(kAlertFirst, kAlertCount, seq_b));
}

struct Animation {
    const char* name;
    void (*show)(int64_t now);
    /* Run every tick_ms while the animation is up, or never if null. */
    void (*tick)(int64_t now);
    uint32_t tick_ms;
    uint8_t brightness = kBrightness;
    /* Whether the frame loop drops segments that have finished. Only the
     * alert wants it: a finished segment keeps painting its landing color,
     * and the wipe rests on nothing else, so dropping its segment would leave
     * the next wipe painting nothing. */
    bool drops_finished = false;
};

static constexpr Animation kAnimations[] = {
    {"blink", show_blink, nullptr, 0},
    {"breathe: eased fades", show_breathe, nullptr, 0},
    {"comet: a stagger of 24 ms a pixel", show_comet, nullptr, 0},
    {"rainbow: the hue wheel spread across the strip", show_rainbow, nullptr, 0},
    {"static gradient: the rainbow, seeked and never played", show_gradient, nullptr, 0},
    {"wipe: a finite sequence that stays where it lands", show_wipe, wipe, kWipeTickMs},
    {"progress bar: a playhead set from outside", show_progress, advance_progress,
     kProgressTickMs},
    {"two halves: two sequencers side by side", show_halves, nullptr, 0},
    {"converging: opposite delays", show_converging, nullptr, 0},
    {"mirrored: a replaced step 1 on one half", show_mirrored, nullptr, 0},
    {"twinkle: a replaced step 2 that never repeats", show_twinkle, nullptr, 0},
    {"flame: a fire with no heat array", show_flame, nullptr, 0, kFlameBrightness},
    {"alert: red flashes painted over a breathing background", show_alert, raise_alert,
     7000, kBrightness, true},
};
static constexpr size_t kAnimationCount = sizeof(kAnimations) / sizeof(kAnimations[0]);

static size_t current;

/* ---- The frame loop ----------------------------------------------------- */

static void frame(struct k_work* work);
static K_WORK_DELAYABLE_DEFINE(frame_work, frame);

static void frame(struct k_work*) {
    const int64_t now = k_uptime_get();   /* sampled once for the whole frame */
    if (kAnimations[current].drops_finished)
        strip.drop_finished(now);
    const uint32_t hold = strip.render(now);

    const int err = send_frame(strip_dev, buffer, kPixels);
    if (err)
        printk("strip update failed: %d\n", err);

    /* hold_forever: nothing on the strip will change until a tick or a press
     * reschedules this. */
    if (hold != hold_forever)
        k_work_reschedule(&frame_work, K_MSEC(hold ? hold : kFramePeriodMs));
}

/* ---- The tick ----------------------------------------------------------- */

static void tick(struct k_work* work);
static K_WORK_DELAYABLE_DEFINE(tick_work, tick);

/* A tick changes what the strip shows behind the hold the last frame reported,
 * which may have been hold_forever, so it renders now rather than waiting for
 * a frame that might never come. */
static void tick(struct k_work*) {
    const Animation& a = kAnimations[current];
    a.tick(k_uptime_get());
    k_work_reschedule(&frame_work, K_NO_WAIT);
    k_work_reschedule(&tick_work, K_MSEC(a.tick_ms));
}

/* ---- Changing animation ------------------------------------------------- */

static void enter(size_t next) {
    k_work_cancel_delayable(&tick_work);
    strip.clear();

    current = next;
    printk("%u/%u: %s\n", static_cast<unsigned>(current + 1),
           static_cast<unsigned>(kAnimationCount), kAnimations[current].name);
    const Animation& a = kAnimations[current];
    strip.set_brightness(a.brightness);
    a.show(k_uptime_get());
    if (a.tick)
        k_work_reschedule(&tick_work, K_MSEC(a.tick_ms));

    /* The last animation may have stopped rescheduling, so restart the loop. */
    k_work_reschedule(&frame_work, K_NO_WAIT);
}

static void advance(struct k_work*) { enter((current + 1) % kAnimationCount); }
static K_WORK_DEFINE(advance_work, advance);

/* The button bounces; a press inside this window of the last one is the same
 * press. */
static constexpr int64_t kDebounceMs = 200;
static int64_t last_press_ms;
static struct gpio_callback button_cb;

static void on_button(const struct device*, struct gpio_callback*, uint32_t) {
    const int64_t now = k_uptime_get();
    if (now - last_press_ms < kDebounceMs)
        return;
    last_press_ms = now;
    k_work_submit(&advance_work);
}

int main() {
    if (!device_is_ready(strip_dev)) {
        printk("LED strip %s is not ready\n", strip_dev->name);
        return 0;
    }
    if (!gpio_is_ready_dt(&button) || gpio_pin_configure_dt(&button, GPIO_INPUT) ||
        gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE)) {
        printk("user button is not available\n");
        return 0;
    }
    gpio_init_callback(&button_cb, on_button, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb);

    printk("showcase: %u pixels, %s. The user button advances.\n",
           static_cast<unsigned>(kPixels), STRIPSEQ_WHITE_CHANNEL ? "RGBW" : "RGB");
    enter(0);
    return 0;
}
