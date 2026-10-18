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
 * This file is the Zephyr half: the device, the button, and when things
 * happen. What each animation draws is in animations.cpp, which reaches the
 * strip only through the Canvas this file hands it, and the two step 2s with
 * the most arithmetic in them, the twinkle and the flame, have files of their
 * own that need nothing but the library.
 *
 * The frame loop and the module's led_strip adapter are strip_demo's, plus one
 * thing it never needs: a tick, for the animations that change on their own
 * schedule rather than the sequencer's -- the wipe changing direction, the
 * progress value moving, the alert firing. Everything runs on the system
 * workqueue, which serialises the work items, so a tick or a press can never
 * land halfway through a render and nothing here locks.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <stripseq/stripseq.h>

#include <stripseq/zephyr/led_strip.h>

#include "animation.h"

using namespace stripseq;

#define STRIP_NODE DT_ALIAS(led_strip)

static uint8_t   buffer[kPixels * Strip::bytes_per_pixel];
static Sequencer seq;
static Sequencer seq_b;
constinit static Strip strip(buffer);

/* The device, and on RGBW the channel order its color-mapping names. */
static constexpr zephyr::LedStrip strip_out = STRIPSEQ_LED_STRIP_DT_GET(STRIP_NODE);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

/* A hold of 0 means "come back at the frame rate", and this is the frame rate. */
static constexpr uint32_t kFramePeriodMs = 16;

constinit static Canvas canvas{strip, seq, seq_b};

static size_t current;

/* ---- The frame loop ----------------------------------------------------- */

static void frame(struct k_work* work);
static K_WORK_DELAYABLE_DEFINE(frame_work, frame);

static void frame(struct k_work*) {
    const int64_t now = k_uptime_get();   /* sampled once for the whole frame */
    if (kAnimations[current].drops_finished)
        strip.drop_finished(now);
    const uint32_t hold = strip.render(now);

    const int err = zephyr::update(strip_out, buffer);
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
    a.tick(canvas, k_uptime_get());
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
    a.show(canvas, k_uptime_get());
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

    printk("showcase: %u pixels, %s. The user button advances.\n",
           static_cast<unsigned>(kPixels), STRIPSEQ_WHITE_CHANNEL ? "RGBW" : "RGB");
    enter(0);
    return 0;
}
