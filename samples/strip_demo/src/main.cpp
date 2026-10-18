/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The bring-up app: the Zephyr side of the library, driving the bench strip
 * through the stages described in this sample's README.md.
 *
 * The library never touches hardware or reads a clock, so everything platform-
 * shaped is here or in the Zephyr module's <stripseq/zephyr/led_strip.h>: the
 * frame loop on a k_work_delayable, k_uptime_get() sampled once per frame, the
 * hold turned into a reschedule, and, in the module, the channel permute and
 * the strip update. The user button advances to the next stage.
 *
 * Both work items run on the system workqueue, which serialises them: a stage
 * change can never land halfway through a render, so nothing here locks.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <stripseq/stripseq.h>

#include <stripseq/zephyr/led_strip.h>

using namespace stripseq;

#define STRIP_NODE DT_ALIAS(led_strip)

static constexpr uint16_t kPixels = DT_PROP(STRIP_NODE, chain_length);

static uint8_t   buffer[kPixels * Strip::bytes_per_pixel];
static Sequencer seq;
constinit static Strip strip(buffer);

/* The device, and on RGBW the channel order its color-mapping names. */
static constexpr zephyr::LedStrip strip_out = STRIPSEQ_LED_STRIP_DT_GET(STRIP_NODE);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

/* A frame with nothing animating still costs a full transfer, so a hold of 0
 * means "come back at the frame rate", and this is the frame rate. */
static constexpr uint32_t kFramePeriodMs = 16;

/* Bring-up proves wiring, timing and code paths, not light output. A quarter
 * keeps eleven pixels at full white well inside what a USB-powered bench
 * supplies, and is applied in the library's linear space, before gamma. */
static constexpr uint8_t kBrightness = 64;

/* ---- The stages --------------------------------------------------------- */

/* Each stage asks the hardware one question the one before it could not.
 * Solid colors and the gradient are seeked rather than played, so they report
 * hold_forever and the frame loop stops: the frame count printed when the
 * button leaves a static stage is 1, or the hold fold is wrong. */

static constexpr Sequence kRed = SequenceBuilder{}.hold(colors::red, 1000).build_const();

#if STRIPSEQ_WHITE_CHANNEL
/* The white die alone: the one color update_rgb() would silently zero. */
static constexpr Sequence kWhiteDie =
    SequenceBuilder{}.hold(Color::rgbw(0, 0, 0, 65535), 1000).build_const();
#endif

static constexpr Sequence kBlink =
    SequenceBuilder{}.hold(colors::red, 500).hold(colors::black, 500).build_const();

static constexpr Sequence kComet = SequenceBuilder{}
    .hold(colors::rgbwhite, 50)
    .fade_to(colors::black, 450)
    .build_const();

static constexpr Sequence kRainbow = SequenceBuilder{}
    .fade_to(Color::hsv(0x2AAA, 255, 255), 500)
    .fade_to(Color::hsv(0x5555, 255, 255), 500)
    .fade_to(Color::hsv(0x8000, 255, 255), 500)
    .fade_to(Color::hsv(0xAAAA, 255, 255), 500)
    .fade_to(Color::hsv(0xD555, 255, 255), 500)
    .fade_to(Color::hsv(0x0000, 255, 255), 500)
    .build_const();

static void show_static(const Sequence& s) {
    seq.load(s);
    seq.seek(0);
    strip.add(Segment(0, kPixels, seq));
}

static void show_red(int64_t) { show_static(kRed); }

#if STRIPSEQ_WHITE_CHANNEL
static void show_white_die(int64_t) { show_static(kWhiteDie); }
#endif

static void show_blink(int64_t now) {
    seq.load(kBlink);
    seq.play(now);
    strip.add(Segment(0, kPixels, seq));
}

/* The head runs from pixel 0 toward the far end: a positive delay makes each
 * pixel later than the one before it. */
static void show_comet(int64_t now) {
    seq.load(kComet);
    seq.play(now);
    Segment s(0, kPixels, seq);
    s.set_pixel_delay(16);
    strip.add(s);
}

/* The seam between the last pixel and the first should be as smooth as any
 * other neighbour pair, since the whole wheel is spread across the strip. */
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

struct Stage {
    const char* name;
    void (*show)(int64_t now);
};

static constexpr Stage kStages[] = {
    {"solid red: wiring, permute, power", show_red},
#if STRIPSEQ_WHITE_CHANNEL
    {"white die only: the update_channels path", show_white_die},
#endif
    {"blink: frame loop and hold", show_blink},
    {"comet: stagger and its sign", show_comet},
    {"rainbow: HSV and the ring seam", show_rainbow},
    {"static gradient: an idle workqueue", show_gradient},
};
static constexpr size_t kStageCount = sizeof(kStages) / sizeof(kStages[0]);

static size_t   stage;
static uint32_t frames;
static int64_t  stage_start_ms;

/* ---- The frame loop ----------------------------------------------------- */

static void frame(struct k_work* work);
static K_WORK_DELAYABLE_DEFINE(frame_work, frame);

static void frame(struct k_work*) {
    const int64_t now = k_uptime_get();   /* sampled once for the whole frame */
    strip.drop_finished(now);
    const uint32_t hold = strip.render(now);

    const int err = zephyr::update(strip_out, buffer);
    if (err)
        printk("strip update failed: %d\n", err);
    ++frames;

    /* hold_forever: nothing on the strip will change until a stage change
     * reschedules this, so the workqueue has nothing to do until then. */
    if (hold != hold_forever)
        k_work_reschedule(&frame_work, K_MSEC(hold ? hold : kFramePeriodMs));
}

/* ---- Stage changes ------------------------------------------------------ */

static void enter_stage(size_t next) {
    strip.clear();

    stage = next;
    frames = 0;
    stage_start_ms = k_uptime_get();
    printk("stage %u/%u: %s\n", static_cast<unsigned>(stage + 1),
           static_cast<unsigned>(kStageCount), kStages[stage].name);
    kStages[stage].show(stage_start_ms);

    /* A static stage stopped rescheduling, so the new one must restart it. */
    k_work_reschedule(&frame_work, K_NO_WAIT);
}

/* The time beside the count is what turns it into a rate: about two frames a
 * second for blink, one per 500 ms hold, and one per kFramePeriodMs for comet
 * and rainbow, whose holds are always 0. */
static void advance(struct k_work*) {
    printk("  %u frames in %u ms\n", static_cast<unsigned>(frames),
           static_cast<unsigned>(k_uptime_get() - stage_start_ms));
    enter_stage((stage + 1) % kStageCount);
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

    strip.set_brightness(kBrightness);
    printk("strip_demo: %u pixels, %s, gamma %s. The user button advances.\n",
           static_cast<unsigned>(kPixels), STRIPSEQ_WHITE_CHANNEL ? "RGBW" : "RGB",
           STRIPSEQ_GAMMA ? "on" : "off");
    enter_stage(0);
    return 0;
}
