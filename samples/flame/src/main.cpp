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
 * the loop simply runs at the frame rate. The user button steps to the next
 * fire's colors.
 *
 * Both work items run on the system workqueue, which serialises them: a color
 * change can never land halfway through a render, so nothing here locks.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
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
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

/* A hold of 0 means "come back at the frame rate", and this is the frame rate. */
static constexpr uint32_t kFramePeriodMs = 16;

static void frame(struct k_work* work);
static K_WORK_DELAYABLE_DEFINE(frame_work, frame);

static void frame(struct k_work*) {
    const uint32_t hold = strip.render(k_uptime_get());

    const int err = zephyr::update(strip_out, buffer);
    if (err)
        printk("strip update failed: %d\n", err);

    if (hold != hold_forever)
        k_work_reschedule(&frame_work, K_MSEC(hold ? hold : kFramePeriodMs));
}

/* The fire keeps burning through a change: only the segment's ColorFn and the
 * strip's brightness are swapped, and the sequencer's playhead runs on, so
 * the flame's shape carries straight over into the new colors. After the
 * fires come the swatches, which hold forever, so a change renders at once
 * rather than waiting on a frame that is not coming. */
static constexpr size_t kShowCount = kFlameCount + kSwatchCount;
static size_t current;

static const Flame& entry(size_t which) {
    return which < kFlameCount ? flames[which] : swatches[which - kFlameCount];
}

static void show(size_t which) {
    current = which;
    strip.segment(0).set_color_fn(entry(current).draw);
    strip.set_brightness(entry(current).brightness);
    printk("%s %u/%u: %s\n", current < kFlameCount ? "fire" : "swatch",
           static_cast<unsigned>(current + 1), static_cast<unsigned>(kShowCount),
           entry(current).name);
    k_work_reschedule(&frame_work, K_NO_WAIT);
}

static void advance(struct k_work*) {
    show((current + 1) % kShowCount);
}
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
    if (!device_is_ready(strip_out.dev)) {
        printk("LED strip %s is not ready\n", strip_out.dev->name);
        return 0;
    }
    if (!gpio_is_ready_dt(&button) || gpio_pin_configure_dt(&button, GPIO_INPUT) ||
        gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE)) {
        printk("user button is not available\n");
        return 0;
    }
    gpio_init_callback(&button_cb, on_button, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb);

    printk("flame: %u pixels, %s. Pixel 0 is the base. The user button changes "
           "the colors.\n",
           static_cast<unsigned>(kPixels), STRIPSEQ_WHITE_CHANNEL ? "RGBW" : "RGB");

    /* The fire is drawn from time alone, so its sequencer plays the timebase:
     * the playhead it hands flame() counts milliseconds since play(). */
    seq.load(timebase);
    seq.play(k_uptime_get());
    strip.add(Segment(0, kPixels, seq));
    show(0);
    return 0;
}
