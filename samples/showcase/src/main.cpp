/* The showcase: what the library can make a strip do, one animation per press
 * of the user button.
 *
 * Unlike strip_demo, nothing here is a check. Each animation is chosen to look
 * like something, and to show one idea from the library: a stagger, a replaced
 * step 1, a playhead driven from outside, segments painted over each other.
 *
 * The frame loop and ../common/strip_adapter.h are strip_demo's, plus one
 * thing it never needs: a tick, for the animations that change on their own
 * schedule rather than the sequencer's -- the wipe turning around, the
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
 * be asked for. Applied in the library's linear space, before gamma, so 64 is
 * about 3% of full light, and it leaves a dim fade few output levels to step
 * through. The alert fades a single color slowly enough for that to show, so
 * it asks for more: 128 is about a fifth of full light. */
static constexpr uint8_t kBrightness = 64;
static constexpr uint8_t kAlertBrightness = 128;

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

/* A pair, so the wipe can turn around: each lands on the color the other
 * starts from, and the strip stays where the last one left it. */
static constexpr Sequence kWipeOn = SequenceBuilder{}
    .start_color(colors::black)
    .fade_to(colors::green, 200)
    .landing_color(colors::green)
    .repeat(1)
    .build_const();

static constexpr Sequence kWipeOff = SequenceBuilder{}
    .start_color(colors::green)
    .fade_to(colors::black, 200)
    .landing_color(colors::black)
    .repeat(1)
    .build_const();

/* One fade per pixel, each kProgressStepMs long and staggered by the same, so
 * a playhead of n steps lights n pixels and the one beyond partway. */
static constexpr int32_t kProgressStepMs = 10;
static constexpr Sequence kProgress = SequenceBuilder{}
    .start_color(colors::black)
    .fade_to(colors::red, kProgressStepMs)
    .landing_color(colors::red)
    .repeat(1)
    .build_const();

/* A quick spark and a dark rest, so each pixel is lit for a fraction of the
 * loop and the flashes stay distinct. The scatter spans the whole loop. */
static constexpr int32_t kTwinkleLoopMs = 1000;
static constexpr Sequence kTwinkle = SequenceBuilder{}
    .fade_to(colors::rgbwhite, 80)
    .fade_to(colors::black, 500)
    .hold(colors::black, kTwinkleLoopMs - 80 - 500)
    .build_const();

/* The breathe bottoms out lit rather than dark. Gamma acts after the sequence,
 * so the floor is high in linear terms: at gamma 2.5, 144 leaves the chip at
 * about a quarter of full blue. At kAlertBrightness the fade crosses 36 output
 * levels, one every two or three frames; at kBrightness it would cross 7, and
 * step visibly every 200 ms whatever the frame rate. */
static constexpr Color kDimBlue = Color::rgb8(0, 0, 144);
static constexpr Sequence kBackground = SequenceBuilder{}
    .fade_to(colors::blue, 1500, Transition::EaseInOut)
    .fade_to(kDimBlue,     1500, Transition::EaseInOut)
    .build_const();

static constexpr Sequence kAlert = SequenceBuilder{}
    .hold(colors::red,   120)
    .hold(colors::black, 120)
    .repeat(3)
    .build_const();

/* ---- The replaced step 1s ----------------------------------------------- */

/* Twinkle: each pixel at its own fixed point in the loop, scattered across the
 * span pixel_delay names rather than marched along it. */
static int32_t scatter(uint16_t index, uint16_t /*count*/, int32_t span_q8) {
    const int32_t span = span_q8 >> 8;
    if (span <= 0)
        return 0;
    const uint32_t h = index * 2654435761u;      // Knuth's multiplicative hash
    return static_cast<int32_t>(h % static_cast<uint32_t>(span));
}

/* The default stagger counted from the far end, so a half wearing it is the
 * reflection of a half wearing the default. */
static int32_t mirror(uint16_t index, uint16_t count, int32_t delay_q8) {
    return ((count - 1 - index) * delay_q8) >> 8;
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
    s.set_pixel_delay(16);
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

static bool wipe_lit;

static void wipe(int64_t now) {
    seq.load(wipe_lit ? kWipeOff : kWipeOn);
    seq.play(now);
    wipe_lit = !wipe_lit;
}

static void show_wipe(int64_t now) {
    wipe_lit = false;
    wipe(now);
    Segment s(0, kPixels, seq);
    s.set_pixel_delay(30);
    strip.add(s);
}

/* The bar fills over two seconds, rests full for one, and starts again. Values
 * past 100 are the rest. */
static constexpr uint32_t kProgressTickMs = 20;
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
    progress = progress < 150 ? progress + 1 : 0;
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
    seq.load(kTwinkle);
    seq.play(now);
    Segment s(0, kPixels, seq);
    s.set_offset_fn(scatter);
    s.set_pixel_delay(kTwinkleLoopMs);
    strip.add(s);
}

/* The background is seq, first in the list and so painted first; the alert is
 * seq_b, added last and painted on top. drop_finished() in the frame loop is
 * what takes the alert away again once its three flashes are done. */
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
    strip.add(Segment(0, kPixels, seq_b));
}

struct Animation {
    const char* name;
    void (*show)(int64_t now);
    /* Run every tick_ms while the animation is up, or never if null. */
    void (*tick)(int64_t now);
    uint32_t tick_ms;
    uint8_t brightness = kBrightness;
};

static constexpr Animation kAnimations[] = {
    {"blink", show_blink, nullptr, 0},
    {"breathe: eased fades", show_breathe, nullptr, 0},
    {"comet: a stagger of 16 ms a pixel", show_comet, nullptr, 0},
    {"rainbow: the hue wheel spread across the strip", show_rainbow, nullptr, 0},
    {"static gradient: the rainbow, seeked and never played", show_gradient, nullptr, 0},
    {"wipe: a finite sequence that stays where it lands", show_wipe, wipe, 3000},
    {"progress bar: a playhead set from outside", show_progress, advance_progress,
     kProgressTickMs},
    {"two halves: two sequencers side by side", show_halves, nullptr, 0},
    {"converging: opposite delays", show_converging, nullptr, 0},
    {"mirrored: a replaced step 1 on one half", show_mirrored, nullptr, 0},
    {"twinkle: a hashed offset per pixel", show_twinkle, nullptr, 0},
    {"alert: red flashes painted over a breathing background", show_alert, raise_alert,
     7000, kAlertBrightness},
};
static constexpr size_t kAnimationCount = sizeof(kAnimations) / sizeof(kAnimations[0]);

static size_t current;

/* ---- The frame loop ----------------------------------------------------- */

static void frame(struct k_work* work);
static K_WORK_DELAYABLE_DEFINE(frame_work, frame);

static void frame(struct k_work*) {
    const int64_t now = k_uptime_get();   /* sampled once for the whole frame */
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
    for (uint8_t i = strip.segment_count(); i > 0; --i)
        strip.remove(i - 1);

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
