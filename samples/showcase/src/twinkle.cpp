/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "twinkle.h"

using namespace stripseq;

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
Sample twinkle(int32_t playhead_ms, uint16_t index, uint16_t /*count*/) {
    const uint32_t p = static_cast<uint32_t>(playhead_ms);   /* timebase, unstaggered: never negative */
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
