/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "flame.h"

using namespace stripseq;

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
Sample flame(int32_t playhead_ms, uint16_t index, uint16_t count) {
    const uint32_t t = static_cast<uint32_t>(playhead_ms);   /* timebase, unstaggered: never negative */
    const uint32_t y = (2u * index + 1u) * 32768u / count;   /* the pixel's centre */
    const uint16_t body = flame_heat(y, t);
    const uint16_t ember = ember_heat(y, t, count);
    return {heat_color(body > ember ? body : ember), 0};
}
