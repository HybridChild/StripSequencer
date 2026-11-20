/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The flame: a whole fire on the strip, drawn by a replaced step 2 with no
 * heat array.
 *
 * This file is the Zephyr half: the device and the frame loop, which are
 * strip_demo's. The fire itself is in flame.cpp, which needs nothing but the
 * library. Every pixel of a fire is mid-change, so the hold is always 0 and
 * the loop simply runs at the frame rate.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <stripseq/stripseq.h>

#include <stripseq/zephyr/led_strip.h>

#include "flame.h"

using namespace stripseq;

#define STRIP_NODE DT_ALIAS(led_strip)

static constexpr uint16_t kPixels = DT_PROP(STRIP_NODE, chain_length);

static uint8_t   buffer[kPixels * Strip::bytes_per_pixel];
static Sequencer seq;
constinit static Strip strip(buffer);

/* The device, and on RGBW the channel order its color-mapping names. */
static constexpr zephyr::LedStrip strip_out = STRIPSEQ_LED_STRIP_DT_GET(STRIP_NODE);

/* A hold of 0 means "come back at the frame rate", and this is the frame rate. */
static constexpr uint32_t kFramePeriodMs = 16;

/* The fire lives in dull reds, which round to black at the 74 the other
 * samples run at, so it asks for more. Its average pixel is dim enough that
 * eleven RGBW pixels still draw little from a USB-powered bench. Applied
 * before gamma, so 120 is about 15% of full light. */
static constexpr uint8_t kBrightness = 120;

static void frame(struct k_work* work);
static K_WORK_DELAYABLE_DEFINE(frame_work, frame);

static void frame(struct k_work*) {
    const uint32_t hold = strip.render(k_uptime_get());

    const int err = zephyr::update(strip_out, buffer);
    if (err)
        printk("strip update failed: %d\n", err);

    k_work_reschedule(&frame_work, K_MSEC(hold ? hold : kFramePeriodMs));
}

int main() {
    if (!device_is_ready(strip_out.dev)) {
        printk("LED strip %s is not ready\n", strip_out.dev->name);
        return 0;
    }

    printk("flame: %u pixels, %s. Pixel 0 is the base.\n",
           static_cast<unsigned>(kPixels), STRIPSEQ_WHITE_CHANNEL ? "RGBW" : "RGB");

    /* The fire is drawn from time alone, so its sequencer plays the timebase:
     * the playhead it hands flame() counts milliseconds since play(). */
    seq.load(timebase);
    seq.play(k_uptime_get());
    Segment s(0, kPixels, seq);
    s.set_color_fn(flame);
    strip.add(s);
    strip.set_brightness(kBrightness);

    k_work_reschedule(&frame_work, K_NO_WAIT);
    return 0;
}
