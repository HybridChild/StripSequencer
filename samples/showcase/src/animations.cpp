/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The catalogue: every animation the showcase plays, each a sequence or two
 * and the segments that place them, and the table main.cpp steps through. */

#include "animation.h"
#include "flame.h"
#include "twinkle.h"

using namespace stripseq;

/* The halves of the split animations. On an odd strip the right one takes the
 * extra pixel. */
static constexpr uint16_t kLeft = kPixels / 2;
static constexpr uint16_t kRight = kPixels - kLeft;

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
    .fade_to(Color::hsv(0x2AAA, 255, 255), 500)   /* red   -> yellow  */
    .fade_to(Color::hsv(0x5555, 255, 255), 500)   /*       -> green   */
    .fade_to(Color::hsv(0x8000, 255, 255), 500)   /*       -> cyan    */
    .fade_to(Color::hsv(0xAAAA, 255, 255), 500)   /*       -> blue    */
    .fade_to(Color::hsv(0xD555, 255, 255), 500)   /*       -> magenta */
    .fade_to(Color::hsv(0x0000, 255, 255), 500)   /*       -> red     */
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

/* The breathe bottoms out lit rather than dark. Gamma acts after the sequence,
 * so a floor that is high as written is low in light: at gamma 2.5, 144 leaves
 * the chip at about a quarter of full blue. At kBrightness the fade crosses 9 output
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

/* A fresh alert every seven seconds: five flashes take two, so the background
 * breathes alone for most of the cycle. */
static constexpr uint32_t kAlertEveryMs = 7000;
static_assert(kAlertFirst + kAlertCount <= kPixels, "the alert must fit on the strip");

/* ---- The replaced step 1s ----------------------------------------------- */

/* The default stagger counted from the far end, so a half wearing it is the
 * reflection of a half wearing the default. */
static int32_t mirror(uint16_t index, uint16_t count, int32_t delay_q8) {
    return ((count - 1 - index) * delay_q8) >> 8;
}

/* ---- The animations ----------------------------------------------------- */

static void play_whole(Canvas& c, const Sequence& s, int64_t now) {
    c.seq.load(s);
    c.seq.play(now);
    c.strip.add(Segment(0, kPixels, c.seq));
}

static void show_blink(Canvas& c, int64_t now) { play_whole(c, kBlink, now); }

static void show_breathe(Canvas& c, int64_t now) { play_whole(c, kBreathe, now); }

static void show_comet(Canvas& c, int64_t now) {
    c.seq.load(kComet);
    c.seq.play(now);
    Segment s(0, kPixels, c.seq);
    s.set_pixel_delay(24);
    c.strip.add(s);
}

static void show_rainbow(Canvas& c, int64_t now) {
    c.seq.load(kRainbow);
    c.seq.play(now);
    Segment s(0, kPixels, c.seq);
    s.set_pixel_delay_q8(spread(3000, kPixels));
    c.strip.add(s);
}

static void show_gradient(Canvas& c, int64_t) {
    c.seq.load(kRainbow);
    c.seq.seek(0);
    Segment s(0, kPixels, c.seq);
    s.set_pixel_delay_q8(spread(3000, kPixels));
    c.strip.add(s);
}

/* Ticks into the current cycle. The reversed pass is the same sequence with
 * the stagger counted from the far end, so the direction is the segment's
 * step 1 and the sequence never knows. */
static uint32_t wipe_ticks;

static void start_pass(Canvas& c, int64_t now, OffsetFn where) {
    c.seq.load(kWipe);   /* re-trigger: back to playhead 0 */
    c.seq.play(now);
    c.strip.segment(0).set_offset_fn(where);
}

static void wipe(Canvas& c, int64_t now) {
    wipe_ticks = wipe_ticks + 1 < kWipeCycleTicks ? wipe_ticks + 1 : 0;
    if (wipe_ticks == 0)
        start_pass(c, now, nullptr);
    else if (wipe_ticks == kWipePassTicks)
        start_pass(c, now, mirror);
}

static void show_wipe(Canvas& c, int64_t now) {
    Segment s(0, kPixels, c.seq);
    s.set_pixel_delay(kWipeDelayMs);
    c.strip.add(s);
    wipe_ticks = 0;
    start_pass(c, now, nullptr);
}

/* The bar fills over three seconds, rests full for two, and starts again.
 * Values past 100 are the rest. */
static constexpr uint32_t kProgressTickMs = 30;
static constexpr uint8_t kProgressLast = 100 + 2000 / kProgressTickMs;
static uint8_t progress;

static void set_progress(Canvas& c, uint8_t percent) {
    c.seq.seek(static_cast<int32_t>(percent) * kPixels * kProgressStepMs / 100);
}

static void show_progress(Canvas& c, int64_t) {
    c.seq.load(kProgress);
    progress = 0;
    set_progress(c, 0);
    Segment s(0, kPixels, c.seq);
    s.set_pixel_delay(kProgressStepMs);
    c.strip.add(s);
}

static void advance_progress(Canvas& c, int64_t) {
    progress = progress < kProgressLast ? progress + 1 : 0;
    set_progress(c, progress < 100 ? progress : 100);
}

static void show_halves(Canvas& c, int64_t now) {
    c.seq.load(kBreathe);
    c.seq.play(now);
    c.seq_b.load(kRainbow);
    c.seq_b.play(now);
    c.strip.add(Segment(0, kLeft, c.seq));
    Segment right(kLeft, kRight, c.seq_b);
    right.set_pixel_delay_q8(spread(3000, kRight));
    c.strip.add(right);
}

/* Opposite delays: both halves flow toward the centre, but the two pixels k
 * from it are a whole half's stagger apart, so the picture is not a
 * reflection. Compare the next animation. */
static void show_converging(Canvas& c, int64_t now) {
    c.seq.load(kComet);
    c.seq.play(now);
    Segment left(0, kLeft, c.seq);
    Segment right(kLeft, kRight, c.seq);
    left.set_pixel_delay(40);
    right.set_pixel_delay(-40);
    c.strip.add(left);
    c.strip.add(right);
}

static void show_mirrored(Canvas& c, int64_t now) {
    c.seq.load(kComet);
    c.seq.play(now);
    Segment left(0, kLeft, c.seq);
    Segment right(kLeft, kRight, c.seq);
    left.set_pixel_delay(40);
    right.set_pixel_delay(40);
    right.set_offset_fn(mirror);
    c.strip.add(left);
    c.strip.add(right);
}

static void show_twinkle(Canvas& c, int64_t now) {
    c.seq.load(timebase);
    c.seq.play(now);
    Segment s(0, kPixels, c.seq);
    s.set_color_fn(twinkle);
    c.strip.add(s);
}

static void show_flame(Canvas& c, int64_t now) {
    c.seq.load(timebase);
    c.seq.play(now);
    Segment s(0, kPixels, c.seq);
    s.set_color_fn(flame);
    c.strip.add(s);
}

/* The background is seq, first in the list and so painted first; the alert is
 * seq_b, added last and painted on top. drop_finished() in the frame loop is
 * what takes the alert away again once its five flashes are done. */
static void show_alert(Canvas& c, int64_t now) {
    c.seq.load(kBackground);
    c.seq.play(now);
    c.strip.add(Segment(0, kPixels, c.seq));
}

static void raise_alert(Canvas& c, int64_t now) {
    c.seq_b.load(kAlert);
    c.seq_b.play(now);
    for (uint8_t i = 0; i < c.strip.segment_count(); ++i)
        if (c.strip.segment(i).drives(c.seq_b))
            return;   /* still flashing: the load() alone restarts it */
    c.strip.add(Segment(kAlertFirst, kAlertCount, c.seq_b));
}

/* The flame lives in dull reds, which round to black at kBrightness, so it
 * asks for more; its average pixel is dim enough that it still draws little. */
static constexpr uint8_t kFlameBrightness = 120;

/* animation.h declares the table extern, and that declaration is what gives
 * this definition external linkage: a const variable at namespace scope is
 * otherwise private to its file, and main.cpp would fail to link. It is still
 * constexpr, so it is built by the compiler and lives in flash. */
constexpr Animation kAnimations[] = {
    {.name = "blink", .show = show_blink},
    {.name = "breathe: eased fades", .show = show_breathe},
    {.name = "comet: a stagger of 24 ms a pixel", .show = show_comet},
    {.name = "rainbow: the hue wheel spread across the strip", .show = show_rainbow},
    {.name = "static gradient: the rainbow, seeked and never played", .show = show_gradient},
    {.name = "wipe: a finite sequence that stays where it lands", .show = show_wipe,
     .tick = wipe, .tick_ms = kWipeTickMs},
    {.name = "progress bar: a playhead set from outside", .show = show_progress,
     .tick = advance_progress, .tick_ms = kProgressTickMs},
    {.name = "two halves: two sequencers side by side", .show = show_halves},
    {.name = "converging: opposite delays", .show = show_converging},
    {.name = "mirrored: a replaced step 1 on one half", .show = show_mirrored},
    {.name = "twinkle: a replaced step 2 that never repeats", .show = show_twinkle},
    {.name = "flame: a fire with no heat array", .show = show_flame,
     .brightness = kFlameBrightness},
    {.name = "alert: red flashes painted over a breathing background", .show = show_alert,
     .tick = raise_alert, .tick_ms = kAlertEveryMs, .drops_finished = true},
};
constexpr size_t kAnimationCount = sizeof(kAnimations) / sizeof(kAnimations[0]);
