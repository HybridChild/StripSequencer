/* The reference application the footprint is measured against: every public
 * entry point the library has, called once, on inputs the compiler cannot see
 * through, linked with --gc-sections. What survives the link is the most an
 * application can pay for the library; one that never builds a sequence at run
 * time, or never pauses, pays less.
 *
 * Nothing here is realistic, and nothing runs: _start is the entry point only
 * so the linker has a root to keep everything else alive from. The volatile
 * inputs and outputs stand in for a clock, a user and a driver, so no call is
 * folded away at compile time.
 *
 * The objects are at file scope, under the names footprint.sh looks for, so
 * their sizes on the target can be read off the symbol table rather than
 * printed by a program that would have to run.
 */

#include <stripseq/stripseq.h>

using namespace stripseq;

static constexpr uint16_t kPixels = 60;

static uint8_t buffer[kPixels * Strip::bytes_per_pixel];
constinit static Strip strip(buffer);
static Sequencer sequencer;
static Sequencer overlay;
static Segment   segment;
static Sequence  built;

static constexpr Sequence kComet = SequenceBuilder{}
    .hold(colors::rgbwhite, 50)
    .fade_to(colors::black, 450)
    .build_const();

volatile int64_t  clock_ms;
volatile uint16_t input;
volatile uint32_t output;

/* A plug-in for each of the two points, so the paths that call them are kept. */
static int32_t reversed(uint16_t index, uint16_t count, int32_t delay_q8) {
    return ((count - 1 - index) * delay_q8) >> 8;
}

static Sample dimmed(int32_t playhead_ms, uint16_t, uint16_t) {
    return Sample{Color::lerp(colors::black, kComet.at(playhead_ms).color, 0x8000), 0};
}

/* SequenceBuilder is header-only, so building at run time is inlined into the
 * caller. In a function of its own, it is a line in the symbol table rather
 * than an invisible part of _start. */
[[gnu::noinline]] static Error build_from_input() {
    return SequenceBuilder{}
        .fade_to(Color::hsv(input, 255, 255), input, Transition::EaseInOut)
        .hold(colors::red, input)
        .repeat(3)
        .landing_color(colors::blue)
        .build(built);
}

extern "C" [[noreturn]] void _start() {
    output = static_cast<uint32_t>(build_from_input());

    sequencer.load(kComet);
    sequencer.play(clock_ms);
    overlay.load(built);
    overlay.play(clock_ms);

    segment = Segment(0, kPixels, sequencer);
    segment.set_pixel_delay(input);
    segment.set_offset_fn(reversed);
    strip.add(segment);

    Segment top(0, kPixels, overlay);
    top.set_pixel_delay_q8(spread(input, kPixels));
    top.set_color_fn(dimmed);
    strip.add(top);

    strip.set_brightness(static_cast<uint8_t>(input));

    for (;;) {
        strip.drop_finished(clock_ms);
        output = strip.render(clock_ms);
        output = strip.brightness() + strip.segment_count() +
                 strip.segment(0).drives(overlay) + overlay.playing();
        if (input) {
            sequencer.pause(clock_ms);
            sequencer.seek(input);
            strip.remove(0);
        }
    }
}
