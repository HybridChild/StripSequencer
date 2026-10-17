/* What one frame costs: Strip::render() over a 60-pixel strip, in CPU cycles,
 * for the scenarios described in this sample's README.md, printed once at
 * boot.
 *
 * Cycles come from the DWT cycle counter through Zephyr's timing API, so they
 * are the core's own clock and include every wait state and cache miss. Each
 * scenario renders the same instant many times: the first call runs with
 * whatever the caches held, the rest with the library's code and tables warm,
 * which is how a frame loop runs it. Interrupts are locked around each call,
 * so a tick never lands inside a measurement.
 *
 * No strip is driven. The buffer is written and never sent: the transfer is
 * the driver's cost, not the library's.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/timing/timing.h>

#include <stripseq/stripseq.h>

using namespace stripseq;

static constexpr uint16_t kPixels = 60;
static constexpr uint32_t kRuns = 100;

static uint8_t   buffer[kPixels * Strip::bytes_per_pixel];
static Sequencer below;
static Sequencer above;
constinit static Strip strip(buffer);

/* ---- The sequences ------------------------------------------------------ */

static constexpr Sequence kBlink =
    SequenceBuilder{}.hold(colors::red, 500).hold(colors::black, 500).build_const();

static constexpr Sequence kFade =
    SequenceBuilder{}.fade_to(colors::red, 1000).fade_to(colors::blue, 1000).build_const();

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

/* Every step the build allows, each an eased fade, so a pixel in the last one
 * walks past all the others before it can blend: the most at() can do.
 * consteval, because build_const() is: called from a merely constexpr
 * function, on a builder that is not itself a constant, it would not compile. */
static consteval Sequence make_longest() {
    SequenceBuilder b;
    for (uint8_t i = 0; i < STRIPSEQ_MAX_STEPS; ++i)
        b.fade_to(Color::hsv(static_cast<uint16_t>(i * 8191), 255, 255), 100,
                  Transition::EaseInOut);
    return b.build_const();
}
static constexpr Sequence kLongest = make_longest();

/* ---- The scenarios ------------------------------------------------------ */

/* Each loads its sequences, places its segments, and returns the instant to
 * render, relative to play(0). */
struct Scenario {
    const char* name;
    int64_t (*setup)();
};

static Segment whole(Sequencer& seq) { return Segment(0, kPixels, seq); }

/* Every pixel on a hold: one step test and no blend. The floor. */
static int64_t hold() {
    below.load(kBlink);
    below.play(0);
    strip.add(whole(below));
    return 250;
}

/* Every pixel mid-fade at the same point: one blend each. */
static int64_t fade() {
    below.load(kFade);
    below.play(0);
    strip.add(whole(below));
    return 500;
}

/* A head running down the strip: a few pixels fading, the rest on the
 * finished end of the loop. */
static int64_t comet() {
    below.load(kComet);
    below.play(0);
    Segment s = whole(below);
    s.set_pixel_delay(16);
    strip.add(s);
    return 300;
}

/* The whole wheel across the strip: every pixel mid-fade, at every step. */
static int64_t rainbow() {
    below.load(kRainbow);
    below.play(0);
    Segment s = whole(below);
    s.set_pixel_delay_q8(spread(3000, kPixels));
    strip.add(s);
    return 1234;
}

/* Every pixel in the last of STRIPSEQ_MAX_STEPS eased fades. */
static int64_t longest() {
    below.load(kLongest);
    below.play(0);
    strip.add(whole(below));
    return STRIPSEQ_MAX_STEPS * 100 - 50;
}

/* The rainbow with the comet painted over it: every pixel painted twice. */
static int64_t overlay() {
    const int64_t at = rainbow();
    above.load(kComet);
    above.play(0);
    Segment s = whole(above);
    s.set_pixel_delay(16);
    strip.add(s);
    return at;
}

static constexpr Scenario kScenarios[] = {
    {"hold", hold},
    {"fade", fade},
    {"comet", comet},
    {"rainbow", rainbow},
    {"longest", longest},
    {"overlay", overlay},
};

/* ---- Measuring ---------------------------------------------------------- */

static uint32_t render_cycles(int64_t at) {
    const unsigned int key = irq_lock();
    timing_t start = timing_counter_get();
    strip.render(at);
    timing_t end = timing_counter_get();
    irq_unlock(key);
    return static_cast<uint32_t>(timing_cycles_get(&start, &end));
}

/* The cost of reading the counter twice, measured the same way and taken off
 * every figure, so what remains is render() alone. */
static uint32_t overhead_cycles() {
    uint32_t least = UINT32_MAX;
    for (uint32_t i = 0; i < kRuns; ++i) {
        const unsigned int key = irq_lock();
        timing_t start = timing_counter_get();
        timing_t end = timing_counter_get();
        irq_unlock(key);
        const auto c = static_cast<uint32_t>(timing_cycles_get(&start, &end));
        least = c < least ? c : least;
    }
    return least;
}

int main() {
    timing_init();
    timing_start();

    const uint32_t mhz = static_cast<uint32_t>(timing_freq_get_mhz());
    const uint32_t overhead = overhead_cycles();

    printk("frame_cost: %u pixels, %s, gamma %s, %u steps max, %u MHz\n",
           static_cast<unsigned>(kPixels), STRIPSEQ_WHITE_CHANNEL ? "RGBW" : "RGB",
           STRIPSEQ_GAMMA ? "on" : "off", static_cast<unsigned>(STRIPSEQ_MAX_STEPS),
           static_cast<unsigned>(mhz));
    printk("| Scenario | First call | Warm, least | Warm, most | Per pixel | Warm, us |\n");
    printk("| --- | --: | --: | --: | --: | --: |\n");

    for (const Scenario& s : kScenarios) {
        while (strip.segment_count() > 0)
            strip.remove(0);
        const int64_t at = s.setup();

        const uint32_t first = render_cycles(at) - overhead;
        uint32_t least = UINT32_MAX;
        uint32_t most = 0;
        for (uint32_t i = 0; i < kRuns; ++i) {
            const uint32_t c = render_cycles(at) - overhead;
            least = c < least ? c : least;
            most = c > most ? c : most;
        }

        /* Hundredths of a microsecond, in integers: cycles * 100 / MHz. */
        const uint32_t us100 = least * 100 / mhz;
        printk("| %s | %u | %u | %u | %u | %u.%02u |\n", s.name, first, least, most,
               least / kPixels, us100 / 100, us100 % 100);
    }

    printk("frame_cost: done\n");
    return 0;
}
