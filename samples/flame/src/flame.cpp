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
 *   - a palette that turns heat into color: a black body's, from dull red
 *     through orange to yellow, and on an RGBW strip into the white die at
 *     the top, or one of five others that mix two primaries the same way.
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
 * of it away again.
 *
 * The stops name roles rather than channels. The cold primary is the whole of
 * a dull glow and the first to rise; the hot one joins it as the heat climbs,
 * so it mixes in toward the base and is missing at the ragged top; the third
 * only tints the tip. The black body is red cold and green hot. Every other
 * fire is the same stops with the primaries dealt out differently, so all
 * six turn color at the same heats. White is a role of its own and is the
 * same in every fire; an RGB build ignores it. */
struct Stop {
    uint16_t heat;
    uint16_t cold, hot, tint, white;
};

static constexpr uint16_t c8(uint8_t level) {
    return static_cast<uint16_t>(level * 257);
}

static constexpr size_t kStops = 6;
static constexpr Stop kBlackBody[kStops] = {
    {0,     0,       0,       0,       0},
    {9800,  c8(80),  0,       0,       0},
    {24900, c8(200), c8(25),  0,       0},
    {39300, c8(255), c8(105), 0,       0},
    {52400, c8(255), c8(170), c8(10),  0},
#if STRIPSEQ_WHITE_CHANNEL
    {65535, 65535,   52000,   10000,   36000},
#else
    {65535, c8(255), c8(220), c8(90),  0},
#endif
};

enum Channel : uint8_t { kRed, kGreen, kBlue };

/* The primaries are not equally bright: green looks far brighter than red at
 * the same level, and blue dimmer. The table is the level of each primary
 * that looks as bright as red at full, in thousandths, before gamma. It is
 * set by eye on the bench strip, against the swatches at the end of this
 * file. A WS2812B's datasheet intensities, about 400, 700 and 190 mcd, put
 * blue far dimmer than it looks there, so they are no guide to it. */
static constexpr uint32_t kEqualLevel[3] = {1000, 800, 900};

static constexpr Channel third(Channel cold, Channel hot) {
    return static_cast<Channel>(3 - cold - hot);
}

/* Newton's method, rounding down; consteval, so it only ever runs while
 * compiling. */
static consteval uint64_t isqrt(uint64_t x) {
    if (x < 2)
        return x;
    uint64_t r = x, next = (x + 1) / 2;
    while (next < r) {
        r = next;
        next = (r + x / r) / 2;
    }
    return r;
}

/* How bright a level looks, Q16 in and out: 65536 is red at full. The output
 * raises a level to the 2.5 before it reaches the LED, and x^2.5 is
 * x * x * sqrt(x), so this is integers only, like the library's gamma table.
 * Exact at full: 65536^2 * 2^16 >> 32. */
static consteval uint64_t light(uint64_t level) {
    return level * level * isqrt(level << 16) >> 32;
}

/* A color's channels before it is fitted under full scale, so any may be past
 * 65535. */
struct Levels {
    uint32_t channel[3];
};

/* How bright a color looks once scaled by scale, Q16: each channel counted as
 * the level of red that looks as bright, and the light of the three added. */
static consteval uint64_t perceived(const Levels& c, uint32_t scale) {
    uint64_t sum = 0;
    for (size_t i = 0; i < 3; ++i)
        sum += light(uint64_t{c.channel[i]} * scale / 65536u * 1000u / kEqualLevel[i]);
    return sum;
}

/* One stop of one fire, before it is fitted under full scale.
 *
 * The hue: each role is first given the level that looks the way the black
 * body's channel in that role does -- the cold part as bright as its red,
 * the hot as its green, the tint as its blue -- and the hot part is then
 * scaled by the fire's mix, in thousandths. How strongly one primary reads
 * mixed into another depends on the pair, so green into blue shows far less
 * than green into red at the same balance, and the mix is set by eye.
 *
 * The brightness: the whole color is then scaled until it looks exactly as
 * bright as the black body's at the same heat. So the mix moves the hue and
 * never the brightness, and a fire matches the black body at every stop
 * whatever its mix. */
static consteval Levels hued(Channel cold, Channel hot, uint32_t mix, const Stop& s) {
    Levels c{};
    c.channel[cold] = s.cold * kEqualLevel[cold] / kEqualLevel[kRed];
    c.channel[hot] = s.hot * kEqualLevel[hot] / kEqualLevel[kGreen] * mix / 1000u;
    c.channel[third(cold, hot)] = s.tint * kEqualLevel[third(cold, hot)] / kEqualLevel[kBlue];
    return c;
}

static consteval Levels matched(Channel cold, Channel hot, uint32_t mix, const Stop& s) {
    const Levels have = hued(cold, hot, mix, s);
    const uint64_t target = perceived(hued(kRed, kGreen, 1000, s), 65536);
    /* Unscaled when it already matches, which the black body always does, so
     * its levels come through exactly; otherwise the least scale, up to four
     * times, that looks at least as bright. */
    uint32_t scale = 65536;
    if (perceived(have, scale) != target) {
        uint32_t lo = 0, hi = 4 * 65536;
        while (lo < hi) {
            const uint32_t mid = (lo + hi) / 2;
            if (perceived(have, mid) < target)
                lo = mid + 1;
            else
                hi = mid;
        }
        scale = lo;
    }
    Levels out{};
    for (size_t i = 0; i < 3; ++i)
        out.channel[i] = static_cast<uint32_t>(uint64_t{have.channel[i]} * scale / 65536u);
    return out;
}

/* The fire lives in dull reds, which round to black at the 74 the other
 * samples run at, so the black body asks for more. Its average pixel is dim
 * enough that eleven RGBW pixels still draw little from a USB-powered bench.
 * Applied before gamma, so 120 is about 15% of full light. */
static constexpr uint32_t kBrightness = 120;

/* A channel cannot go above full, so a fire whose dim primary wants more is
 * scaled down until its brightest level fits, and the strip's brightness is
 * raised by as much, which is the same multiply before gamma. This is that
 * brightest level, and never less than full. */
static consteval uint32_t peak(Channel cold, Channel hot, uint32_t mix) {
    uint32_t most = 65535;
    for (const Stop& s : kBlackBody) {
        const Levels c = matched(cold, hot, mix, s);
        for (uint32_t level : c.channel)
            most = level > most ? level : most;
    }
    return most;
}

static consteval uint16_t fit(uint32_t level, uint32_t peak) {
    return static_cast<uint16_t>(uint64_t{level} * 65535u / peak);
}

/* A fire's stop as a color, fitted under full scale. White is the same in
 * every fire, so it is scaled down with the rest and the raised brightness
 * puts it back where it was. */
struct Shade {
    uint16_t heat;
    Color    color;
};

static consteval Shade shade(Channel cold, Channel hot, uint32_t mix, uint32_t peak, const Stop& s) {
    const Levels c = matched(cold, hot, mix, s);
    const uint16_t r = fit(c.channel[kRed], peak);
    const uint16_t g = fit(c.channel[kGreen], peak);
    const uint16_t b = fit(c.channel[kBlue], peak);
#if STRIPSEQ_WHITE_CHANNEL
    return {s.heat, Color::rgbw(r, g, b, fit(s.white, peak))};
#else
    return {s.heat, Color::rgb(r, g, b)};
#endif
}

/* The brightness is kept wide so fire() can refuse one past 255 rather than
 * see it wrap. */
struct Palette {
    Shade    stop[kStops];
    uint32_t brightness;
};

static consteval Palette palette(Channel cold, Channel hot, uint32_t mix) {
    static_assert(kStops == 6, "palette() lists every stop");
    const uint32_t p = peak(cold, hot, mix);
    return {{shade(cold, hot, mix, p, kBlackBody[0]), shade(cold, hot, mix, p, kBlackBody[1]),
             shade(cold, hot, mix, p, kBlackBody[2]), shade(cold, hot, mix, p, kBlackBody[3]),
             shade(cold, hot, mix, p, kBlackBody[4]), shade(cold, hot, mix, p, kBlackBody[5])},
            (kBrightness * p + 32767u) / 65535u};   /* to the nearest step */
}

template <Channel cold, Channel hot, uint32_t mix>
static constexpr Palette kPalette = palette(cold, hot, mix);

static Color heat_color(const Palette& palette, uint16_t heat) {
    size_t i = 1;
    while (heat > palette.stop[i].heat)
        ++i;
    const Shade& lo = palette.stop[i - 1];
    const Shade& hi = palette.stop[i];
    const uint32_t into = static_cast<uint32_t>(heat - lo.heat);
    const uint32_t width = static_cast<uint32_t>(hi.heat - lo.heat);
    return Color::lerp(lo.color, hi.color, static_cast<uint16_t>(into * 65535u / width));
}

/* Every pixel is mid-change in a fire, so the hold is always 0. The palette is
 * a template argument rather than a variable because a ColorFn is a bare
 * function pointer: each fire is its own function, and which one draws is the
 * application's choice, made by installing it. */
template <const Palette& palette>
static Sample burn(int32_t playhead_ms, uint16_t index, uint16_t count) {
    const uint32_t t = static_cast<uint32_t>(playhead_ms);   /* timebase, unstaggered: never negative */
    const uint32_t y = (2u * index + 1u) * 32768u / count;   /* the pixel's centre */
    const uint16_t body = flame_heat(y, t);
    const uint16_t ember = ember_heat(y, t, count);
    return {heat_color(palette, body > ember ? body : ember), 0};
}

template <Channel cold, Channel hot, uint32_t mix = 1000>
static constexpr Flame fire(const char* name) {
    static_assert(cold != hot, "a fire mixes two different primaries");
    static_assert(kPalette<cold, hot, mix>.brightness <= 255,
                  "a fire too dim to match the black body even at full brightness");
    return {name, burn<kPalette<cold, hot, mix>>,
            static_cast<uint8_t>(kPalette<cold, hot, mix>.brightness)};
}

/* Every ordered pair of primaries, grouped by the cold one. Each is named
 * cold/hot: the color at the top of the flame, then the one that mixes in at
 * its base. */
const Flame flames[kFlameCount] = {
    fire<kRed, kGreen>("red/green"),
    fire<kRed, kBlue, 850>("red/blue"),
    fire<kGreen, kRed>("green/red"),
    fire<kGreen, kBlue, 850>("green/blue"),
    fire<kBlue, kRed>("blue/red"),
    fire<kBlue, kGreen, 1350>("blue/green"),
};

/* Steady blocks for setting kEqualLevel by eye, where a flickering fire of
 * two colors is no help: red at full on the base half, and the other primary
 * at its table level on the top half. The table is right when the halves look
 * equally bright. The halves are the same number of pixels, so on an odd
 * strip the last is dark rather than tipping the comparison. A level over
 * 1000 would need more than full scale, so both halves come down together.
 * Nothing moves, so the hold is forever. */
template <Channel primary>
static Sample swatch(int32_t, uint16_t index, uint16_t count) {
    constexpr uint32_t top = kEqualLevel[primary] > 1000 ? kEqualLevel[primary] : 1000;
    const uint16_t half = count / 2;
    uint32_t channel[3] = {0, 0, 0};
    if (index < half)
        channel[kRed] = 65535u * 1000u / top;
    else if (index < 2 * half)
        channel[primary] = 65535u * kEqualLevel[primary] / top;
    return {Color::rgb(static_cast<uint16_t>(channel[kRed]), static_cast<uint16_t>(channel[kGreen]),
                       static_cast<uint16_t>(channel[kBlue])),
            hold_forever};
}

const Flame swatches[kSwatchCount] = {
    {"red | green, for kEqualLevel", swatch<kGreen>, kBrightness},
    {"red | blue, for kEqualLevel",  swatch<kBlue>,  kBrightness},
};
