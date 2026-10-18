/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The seam between the two halves of the showcase: animations.cpp says what
 * each animation draws, and main.cpp decides when, and owns everything that
 * touches the hardware. This header is all either knows of the other. */

#ifndef SHOWCASE_ANIMATION_H_
#define SHOWCASE_ANIMATION_H_

#include <cstddef>
#include <cstdint>

#include <zephyr/devicetree.h>

#include <stripseq/stripseq.h>

/* The strip's length, from the devicetree. A constant rather than something
 * asked of the Strip, because the sequences are built against it at compile
 * time: the wipe holds for exactly the stagger across the whole strip. */
inline constexpr uint16_t kPixels = DT_PROP(DT_ALIAS(led_strip), chain_length);

/* What an animation draws with. main.cpp owns all three and hands them in, so
 * an animation reaches the strip only through its argument and never the
 * device, the button or the frame loop behind it. Two sequencers, because the
 * split and layered animations play two sequences at once. */
struct Canvas {
    stripseq::Strip&     strip;
    stripseq::Sequencer& seq;
    stripseq::Sequencer& seq_b;
};

/* Eleven RGBW pixels at full white draw more than a USB-powered bench should
 * be asked for. Applied in the space colors are written in, before gamma, so
 * 74 is about 5% of full light and leaves a channel 12 output levels from dark
 * to full: enough for a fade to read as smooth, where 64 leaves eight and a
 * slow fade climbs them as a visible staircase. */
inline constexpr uint8_t kBrightness = 74;

struct Animation {
    const char* name;
    /* Called on an empty strip, at the brightness below. */
    void (*show)(Canvas& c, int64_t now);
    /* Run every tick_ms while the animation is up, or never if null. */
    void (*tick)(Canvas& c, int64_t now) = nullptr;
    uint32_t tick_ms = 0;
    uint8_t brightness = kBrightness;
    /* Whether the frame loop drops segments that have finished. Only the
     * alert wants it: a finished segment keeps painting its landing color,
     * and the wipe rests on nothing else, so dropping its segment would leave
     * the next wipe painting nothing. */
    bool drops_finished = false;
};

/* In the order the button steps through them. Sized only in animations.cpp,
 * so adding one there is the whole change. */
extern const Animation kAnimations[];
extern const size_t kAnimationCount;

#endif  /* SHOWCASE_ANIMATION_H_ */
