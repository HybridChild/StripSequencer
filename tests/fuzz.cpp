/* SequenceBuilder and Sequence::at() on generated input.
 *
 * at() is the one function in the library that takes fully unconstrained
 * input: every int32_t is a legal playhead. The cases in tests/sequence.cpp
 * pin down chosen values; this file asks the same questions of thousands of
 * sequences nobody wrote by hand, and checks properties that must hold for
 * every one of them rather than expected values.
 *
 * The checks live in check(), a function of a byte buffer with no doctest in
 * it, so that anything able to produce bytes can drive it. Here that is a
 * seeded generator inside a test case, which runs in every variant on both
 * compilers and under the sanitizers job; a coverage-guided fuzzer could call
 * the same function from LLVMFuzzerTestOneInput. Bytes past the end of the
 * buffer read as zero, so every buffer, however short, is a valid input.
 *
 * Two environment variables reach the test case. STRIPSEQ_FUZZ_SEED replays a
 * single seed, the one a failure names. STRIPSEQ_FUZZ_ITERATIONS runs more
 * seeds than the default, for a longer search on demand.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>             /* getenv, strtoull */
#include <initializer_list>   /* range-for over a braced list; gcc insists */

#include <stripseq/sequence.h>

using namespace stripseq;

namespace {

/* ------------------------------------------------------------------------ */
/* Reading values out of the buffer                                          */
/* ------------------------------------------------------------------------ */

class Bytes {
public:
    Bytes(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    uint8_t u8() { return pos_ < size_ ? data_[pos_++] : 0; }
    uint16_t u16() { return static_cast<uint16_t>(u8() | u8() << 8); }
    uint32_t u32() { return uint32_t{u16()} | uint32_t{u16()} << 16; }

    /* One of n, for picking from a table. */
    size_t pick(size_t n) { return u8() % n; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
};

constexpr uint32_t limit_ms = uint32_t{1} << 29;   /* the builder's TooLong line */

/* Uniform uint32_t durations would make nearly every sequence TooLong and leave
 * at() untested, so most are small. The rest are the values where something
 * changes: zero, where a fade becomes an error; 65536, past which progress()
 * shifts; the limit either side; and the largest there is. */
constexpr uint32_t edge_durations[] = {
    0, 1, 2, 65535, 65536, 65537, limit_ms - 1, limit_ms, limit_ms + 1, UINT32_MAX,
};

constexpr uint16_t edge_repeats[] = {0, 1, 2, 3, UINT16_MAX};

constexpr int32_t edge_playheads[] = {
    INT32_MIN, INT32_MIN + 1, -1, 0, 1, INT32_MAX - 1, INT32_MAX,
};

uint32_t draw_duration(Bytes& in) {
    const uint8_t kind = in.u8();
    if (kind < 32)
        return edge_durations[in.pick(sizeof edge_durations / sizeof edge_durations[0])];
    if (kind < 224)
        return in.u16() % 1000;
    return in.u32() % (uint32_t{1} << 22);
}

/* Mostly a few, since a large count times anything but a short loop is
 * TooLong. */
uint16_t draw_repeats(Bytes& in) {
    const uint8_t kind = in.u8();
    if (kind < 32)
        return edge_repeats[in.pick(sizeof edge_repeats / sizeof edge_repeats[0])];
    if (kind < 224)
        return static_cast<uint16_t>(1 + in.pick(8));
    return in.u16();
}

/* Random channels mostly, since a fade between two arbitrary colors is what
 * the between-the-endpoints check needs; sometimes a named color, so that two
 * adjacent steps are now and then the same. */
Color draw_color(Bytes& in) {
    if (in.u8() < 64) {
        constexpr Color named[] = {colors::black, colors::red, colors::green, colors::blue,
                                   colors::rgbwhite};
        return named[in.pick(sizeof named / sizeof named[0])];
    }
    Color c = Color::rgb(in.u16(), in.u16(), in.u16());
#if STRIPSEQ_WHITE_CHANNEL
    c.w = in.u16();
#endif
    return c;
}

/* ------------------------------------------------------------------------ */
/* The model                                                                 */
/* ------------------------------------------------------------------------ */

/* The builder's rules, kept apart from the builder: every call is applied to
 * both, and build() has to agree with expected_error(). It keeps the steps,
 * which a Sequence will not show, and so is also what expect() reads to work
 * out independently what at() should answer. */
struct Model {
    Step     steps[STRIPSEQ_MAX_STEPS] = {};
    size_t   count = 0;
    uint16_t repeats = 0;
    bool     start_set = false;
    bool     landing_set = false;
    Color    start = colors::black;
    Color    landing = colors::black;
    Error    error = Error::None;

    void add(Color c, uint32_t ms, Transition t) {
        if (error != Error::None)
            return;
        if (ms == 0 && t != Transition::Step)
            error = Error::ZeroDurationFade;
        else if (count == STRIPSEQ_MAX_STEPS)
            error = Error::TooManySteps;
        else
            steps[count++] = Step{c, t, ms};
    }

    int64_t loop() const {
        int64_t sum = 0;
        for (size_t i = 0; i < count; ++i)
            sum += steps[i].duration_ms;
        return sum;
    }

    int64_t total() const { return repeats == 0 ? loop() : loop() * repeats; }

    Error expected_error() const {
        if (error != Error::None)
            return error;
        if (count == 0)
            return Error::NoSteps;
        if (repeats == 0 && landing_set)
            return Error::LandingColorOnEndless;
        if (repeats == 0 && start_set)
            return Error::StartColorOnEndless;
        if (total() > limit_ms)
            return Error::TooLong;
        if (repeats == 0 && loop() == 0)
            return Error::ZeroLengthLoop;
        return Error::None;
    }

    /* What build() resolves an unset color to. */
    Color start_color() const { return start_set ? start : colors::black; }
    Color landing_color() const { return landing_set ? landing : steps[count - 1].color; }
};

enum class Call : uint8_t { FadeTo, Hold, Repeat, Endless, StartColor, LandingColor };

/* Weighted towards steps, and away from endless() and the two finite-only
 * colors, each of which on its own is enough to make a sequence invalid. */
constexpr Call calls_drawn_from[] = {
    Call::FadeTo, Call::FadeTo, Call::FadeTo, Call::FadeTo, Call::FadeTo, Call::FadeTo,
    Call::Hold,   Call::Hold,   Call::Hold,   Call::Repeat, Call::Repeat, Call::Endless,
    Call::StartColor, Call::LandingColor,
};

/* A random sequence of builder calls, each applied to the builder and the model
 * alike. Up to four more than the capacity, so that TooManySteps comes up, and
 * in any order, so that start_color() before repeat() does too. */
void draw_calls(Bytes& in, SequenceBuilder& b, Model& m) {
    const size_t calls = in.pick(STRIPSEQ_MAX_STEPS + 5);
    for (size_t k = 0; k < calls; ++k) {
        switch (calls_drawn_from[in.pick(sizeof calls_drawn_from / sizeof calls_drawn_from[0])]) {
        case Call::FadeTo: {
            const Color c = draw_color(in);
            const uint32_t ms = draw_duration(in);
            const auto t = static_cast<Transition>(in.pick(5));
            b.fade_to(c, ms, t);
            m.add(c, ms, t);
            break;
        }
        case Call::Hold: {
            const Color c = draw_color(in);
            const uint32_t ms = draw_duration(in);
            b.hold(c, ms);
            m.add(c, ms, Transition::Step);
            break;
        }
        case Call::Repeat: {
            const uint16_t times = draw_repeats(in);
            b.repeat(times);
            if (m.error == Error::None) {
                if (times == 0)
                    m.error = Error::ZeroRepeatCount;
                else
                    m.repeats = times;
            }
            break;
        }
        case Call::Endless:
            b.endless();
            if (m.error == Error::None)
                m.repeats = 0;
            break;
        case Call::StartColor: {
            const Color c = draw_color(in);
            b.start_color(c);
            if (m.error == Error::None) {
                m.start = c;
                m.start_set = true;
            }
            break;
        }
        case Call::LandingColor: {
            const Color c = draw_color(in);
            b.landing_color(c);
            if (m.error == Error::None) {
                m.landing = c;
                m.landing_set = true;
            }
            break;
        }
        }
    }
}

/* ------------------------------------------------------------------------ */
/* What at() should answer                                                   */
/* ------------------------------------------------------------------------ */

/* Either an exact color and hold, or a fade: hold 0, and a color somewhere
 * between `from` and `to`. Where the fade has only just begun the color is
 * exactly `from`, since lerp is exact at t = 0. */
struct Expected {
    bool     fading;
    Color    color;          /* exact; or, fading, the color faded from */
    Color    to;             /* fading only */
    uint32_t hold_ms;        /* not fading only */
    bool     at_fade_start;
    uint32_t fade_left_ms;   /* fading only: until the next step takes over */
};

/* The timeline worked out from the model in int64_t, where nothing wraps: the
 * remainder is the textbook ((p % n) + n) % n, and the pass a playhead falls in
 * is a plain division, rather than at()'s unsigned folding. */
Expected expect(const Model& m, int32_t playhead) {
    const int64_t p = playhead;
    const int64_t loop = m.loop();
    Color before_first = m.steps[m.count - 1].color;
    int64_t local;

    if (m.repeats == 0) {
        local = ((p % loop) + loop) % loop;
    } else {
        if (p < 0)
            return Expected{false, m.start_color(), {}, static_cast<uint32_t>(-p), false, 0};
        if (p >= m.total())
            return Expected{false, m.landing_color(), {}, hold_forever, false, 0};
        local = p % loop;
        if (p / loop == 0)
            before_first = m.start_color();
    }

    int64_t begin = 0;
    size_t i = 0;
    while (local >= begin + m.steps[i].duration_ms) {
        begin += m.steps[i].duration_ms;
        ++i;
    }
    const Step& step = m.steps[i];
    const int64_t end = begin + step.duration_ms;

    if (step.transition == Transition::Step)
        return Expected{false, step.color, {}, static_cast<uint32_t>(end - local), false, 0};

    const Color from = i == 0 ? before_first : m.steps[i - 1].color;
    return Expected{true, from, step.color, 0, local == begin,
                    static_cast<uint32_t>(end - local)};
}

/* ------------------------------------------------------------------------ */
/* The checks                                                                */
/* ------------------------------------------------------------------------ */

bool same(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b
#if STRIPSEQ_WHITE_CHANNEL
        && a.w == b.w
#endif
        ;
}

bool same(Sample a, Sample b) { return same(a.color, b.color) && a.hold_ms == b.hold_ms; }

bool between(uint16_t x, uint16_t a, uint16_t b) {
    return a <= b ? a <= x && x <= b : b <= x && x <= a;
}

bool between(Color c, Color a, Color b) {
    return between(c.r, a.r, b.r) && between(c.g, a.g, b.g) && between(c.b, a.b, b.b)
#if STRIPSEQ_WHITE_CHANNEL
        && between(c.w, a.w, b.w)
#endif
        ;
}

bool in_range(int64_t p) { return p >= INT32_MIN && p <= INT32_MAX; }

int32_t clamped(int64_t p) {
    return p < INT32_MIN ? INT32_MIN : p > INT32_MAX ? INT32_MAX : static_cast<int32_t>(p);
}

/* Playheads aimed where at() makes decisions: the ends of int32_t, the edges
 * of each step in passes either side of zero and of the last, the end of the
 * last repeat, near zero, and anywhere at all. Clamped rather than discarded
 * when the arithmetic leaves int32_t, which only adds weight to the ends. */
int32_t draw_playhead(Bytes& in, const Model& m) {
    const int64_t loop = m.loop();
    switch (in.pick(8)) {
    case 0:
        return edge_playheads[in.pick(sizeof edge_playheads / sizeof edge_playheads[0])];
    case 1:
    case 2:
    case 3: {
        int64_t edge = 0;
        const size_t step = in.pick(m.count + 1);
        for (size_t i = 0; i < step; ++i)
            edge += m.steps[i].duration_ms;
        const int64_t last = m.repeats == 0 ? 3 : m.repeats;
        const int64_t passes[] = {-2, -1, 0, 1, 2, last - 1, last, static_cast<int16_t>(in.u16())};
        const int64_t pass = passes[in.pick(sizeof passes / sizeof passes[0])];
        return clamped(pass * loop + edge + static_cast<int64_t>(in.pick(3)) - 1);
    }
    case 4:
        return clamped(m.total() + static_cast<int64_t>(in.pick(5)) - 2);
    case 5:
        return static_cast<int16_t>(in.u16());
    default:
        return static_cast<int32_t>(in.u32());
    }
}

struct Finding {
    const char* failure;   /* null when every property held */
    int32_t     playhead;
    bool        built;     /* whether the input made a sequence at all */
};

constexpr size_t playheads_per_input = 16;

/* Every property, for one input. The first that fails is returned, with the
 * playhead it failed at where there is one. */
Finding check(const uint8_t* data, size_t size) {
    Bytes in(data, size);
    SequenceBuilder b;
    Model m;
    draw_calls(in, b, m);

    /* A failed build must leave its output alone, so it starts as something
     * recognisable. */
    const Sequence sentinel = SequenceBuilder{}.hold(colors::blue, 7).repeat(3).build_const();
    Sequence s = sentinel;
    const Error error = b.build(s);

    if (error != m.expected_error())
        return {"build() disagrees with the model", 0, false};

    if (error != Error::None) {
        if (s.loop_duration_ms() != sentinel.loop_duration_ms() ||
            s.total_duration_ms() != sentinel.total_duration_ms())
            return {"a failed build() changed its output's durations", 0, false};
        for (int32_t p : edge_playheads)
            if (!same(s.at(p), sentinel.at(p)))
                return {"a failed build() changed its output", p, false};
        return {nullptr, 0, false};
    }

    if (s.loop_duration_ms() != m.loop())
        return {"loop_duration_ms() is not the sum of the durations", 0, true};
    if (s.total_duration_ms() != (m.repeats == 0 ? hold_forever : m.total()))
        return {"total_duration_ms() is not loop times repeats", 0, true};

    const int64_t loop = m.loop();
    for (size_t n = 0; n < playheads_per_input; ++n) {
        const int32_t p = draw_playhead(in, m);
        const Sample got = s.at(p);
        const Expected want = expect(m, p);

        if (!same(no_sequence.at(p), Sample{colors::black, hold_forever}))
            return {"no_sequence is not black forever", p, true};

        /* Against the model. */
        if (want.fading) {
            if (got.hold_ms != 0)
                return {"a fade reports a hold", p, true};
            if (!between(got.color, want.color, want.to))
                return {"a fade's color is outside its endpoints", p, true};
            if (want.at_fade_start && !same(got.color, want.color))
                return {"a fade does not start from the color before it", p, true};

            /* Monotonic: anywhere later in the same fade is no nearer where it
             * came from, and no further than where it is going. Between the
             * endpoints alone would pass a fade that jumped back. */
            const uint32_t d = 1 + in.u32() % want.fade_left_ms;
            if (d < want.fade_left_ms && in_range(int64_t{p} + d) &&
                !between(s.at(static_cast<int32_t>(p + int64_t{d})).color, got.color, want.to))
                return {"a fade went backwards", p, true};
        } else {
            if (got.hold_ms != want.hold_ms)
                return {"hold_ms disagrees with the model", p, true};
            if (!same(got.color, want.color))
                return {"color disagrees with the model", p, true};
        }

        /* A hold of n is exact: the color stays, and the hold counts down by
         * one each millisecond. k = n - 1 catches a hold that is one too long;
         * k = 1 one that skips ahead. */
        if (got.hold_ms != 0 && got.hold_ms != hold_forever) {
            const uint32_t ks[] = {1, got.hold_ms - 1, in.u32() % got.hold_ms};
            for (uint32_t k : ks) {
                if (k == 0 || k >= got.hold_ms || !in_range(int64_t{p} + k))
                    continue;
                const Sample later = s.at(static_cast<int32_t>(p + int64_t{k}));
                if (!same(later.color, got.color))
                    return {"the color changed inside its hold", p, true};
                if (later.hold_ms != got.hold_ms - k)
                    return {"the hold did not count down", p, true};
            }
        }

        /* Forever is forever. */
        if (got.hold_ms == hold_forever) {
            const int64_t later[] = {int64_t{p} + 1, int64_t{p} + in.u32() % 1'000'000, INT32_MAX};
            for (int64_t q : later)
                if (in_range(q) && !same(s.at(static_cast<int32_t>(q)), got))
                    return {"a forever hold ended", p, true};
        }

        /* A loop later or earlier is the same answer: always when endless, and
         * between the first pass and the last when finite. */
        for (int64_t q : {int64_t{p} - loop, int64_t{p} + loop}) {
            if (!in_range(q))
                continue;
            const bool periodic =
                m.repeats == 0 ||
                (p >= loop && q >= loop && p < m.total() && q < m.total());
            if (periodic && !same(s.at(static_cast<int32_t>(q)), got))
                return {"a whole loop away gives a different answer", p, true};
        }
    }
    return {nullptr, 0, true};
}

/* ------------------------------------------------------------------------ */
/* Driving it                                                                */
/* ------------------------------------------------------------------------ */

/* splitmix64: a few lines, and the same stream on every compiler and standard
 * library, which <random>'s distributions do not promise. */
uint64_t splitmix64(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15u);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9u;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBu;
    return z ^ (z >> 31);
}

void fill(uint8_t* buffer, size_t size, uint64_t seed) {
    uint64_t state = seed;
    for (size_t i = 0; i < size; i += 8) {
        const uint64_t word = splitmix64(state);
        for (size_t k = 0; k < 8 && i + k < size; ++k)
            buffer[i + k] = static_cast<uint8_t>(word >> (8 * k));
    }
}

uint64_t from_env(const char* name, uint64_t otherwise) {
    const char* text = std::getenv(name);
    return text != nullptr ? std::strtoull(text, nullptr, 10) : otherwise;
}

}  // namespace

TEST_CASE("generated sequences and playheads keep every property of build() and at()") {
    constexpr uint64_t default_iterations = 20'000;

    const char* replay = std::getenv("STRIPSEQ_FUZZ_SEED");
    const uint64_t first = from_env("STRIPSEQ_FUZZ_SEED", 0);
    const uint64_t iterations =
        replay != nullptr ? 1 : from_env("STRIPSEQ_FUZZ_ITERATIONS", default_iterations);

    /* Enough for the most calls and playheads one input reads; past that the
     * reads are zeros. */
    uint8_t buffer[512];
    uint64_t built = 0;

    for (uint64_t seed = first; seed < first + iterations; ++seed) {
        fill(buffer, sizeof buffer, seed);
        const Finding f = check(buffer, sizeof buffer);
        if (f.failure != nullptr) {
            INFO("seed ", seed, ", playhead ", f.playhead,
                 "; replay with STRIPSEQ_FUZZ_SEED=", seed);
            FAIL_CHECK(doctest::String(f.failure));
            return;
        }
        built += f.built;
    }

    /* Every property holds vacuously of an input that never builds, so a
     * generator that drifted into making nothing but errors would pass. A
     * replay is one seed and proves nothing either way. */
    if (replay == nullptr) {
        CAPTURE(built);
        CHECK(built * 3 >= iterations);
    }
}
