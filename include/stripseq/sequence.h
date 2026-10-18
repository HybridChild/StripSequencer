/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Sequence: pure description, and the one question the whole design is built
 * around -- given a playhead, what color is here, and how long does it stay?
 *
 * A sequence holds its steps inline, behind no pointer, so one built at compile
 * time sits whole in flash and a sequencer only has to refer to it. It holds no
 * state, references no hardware, and can be built at compile time
 * (SequenceBuilder::build_const) or at run time (build).
 *
 * The mechanism is docs/DESIGN.md, "A sequence is a lookup table, not a tape".
 */

#ifndef STRIPSEQ_SEQUENCE_H_
#define STRIPSEQ_SEQUENCE_H_

#include <cstdint>

#include "stripseq/color.h"
#include "stripseq/config.h"

namespace stripseq {

enum class Transition : uint8_t { Step, Linear, EaseIn, EaseOut, EaseInOut };

/* The curve a fade follows, on a 0..65535 parameter in and out: what at()
 * applies to how far through a step the playhead is, before it blends. Public
 * because a ColorFn that eases between values of its own wants the same
 * curves. A Step is not a curve -- at() never eases one -- and here, like
 * Linear, returns t unchanged.
 *
 * Each is exact at both ends and monotonic, because each is the floor of a
 * monotonic function -- a curve that topped out at 65534 would leave a one-LSB
 * step at every transition boundary, a visible tick in a slow breathe. */
constexpr uint16_t ease(Transition kind, uint16_t t) {
    switch (kind) {
    case Transition::EaseIn:
        return static_cast<uint16_t>(detail::div65535(uint32_t{t} * t));        /* t^2 */
    case Transition::EaseOut: {
        const uint32_t u = 65535u - t;
        return static_cast<uint16_t>(65535u - detail::div65535(u * u));        /* 1 - (1-t)^2 */
    }
    case Transition::EaseInOut: {
        /* EaseIn on the first half and EaseOut on the second, each at double
         * speed: 2t^2, then 1 - 2(1-t)^2. Both products stay inside
         * div65535's range, and the halves meet at 32766 and 32769 either
         * side of the middle, so the whole is monotonic and point-symmetric. */
        if (t < 32768)
            return static_cast<uint16_t>(detail::div65535(2 * uint32_t{t} * t));
        const uint32_t u = 65535u - t;
        return static_cast<uint16_t>(65535u - detail::div65535(2 * u * u));
    }
    case Transition::Step:
    case Transition::Linear:
        break;
    }
    return t;
}

struct Step {
    Color      color;        /* the color arrived at by the end of this step */
    Transition transition;   /* how it is approached from the previous color */
    uint32_t   duration_ms;
};

/* Field order above is load-bearing in one respect: the one-byte `transition`
 * sits beside the six-byte `color`, so the two share one eight-byte span and
 * `duration_ms`, before them or after, needs no padding. Never put the
 * duration between them: each is then padded out to its own four-byte
 * boundary, 16 bytes rather than 12 in an RGB build, four more per step,
 * multiplied by STRIPSEQ_MAX_STEPS in every sequence. An RGBW build is 16 in
 * every order, since the eight-byte color already ends aligned; only RGB is
 * sensitive, and RGB is the default. */
static_assert(sizeof(Step) == (STRIPSEQ_WHITE_CHANNEL ? 16 : 12),
              "Step has picked up padding -- check field order.");

/* What every lookup returns, built-in or replacement alike.
 *
 * hold_ms is how long this color stays put, measured forward from the playhead
 * it was asked about. It is not advice or an estimate: the strip folds it into
 * the "when to come back" answer and will not render again before it expires,
 * so understating it wastes frames and overstating it drops them.
 *
 *   0             changing continuously -- mid-fade. Asks for frame rate.
 *   n             this exact color holds for n ms, then something changes.
 *   hold_forever  it never changes again: a paused sequencer, or a finite
 *                 sequence resting on its start or landing color.
 *
 * It costs the built-in lookup nothing, because finding the step containing the
 * playhead already yields that step's end. */
struct Sample {
    Color    color;
    uint32_t hold_ms;
};

inline constexpr uint32_t hold_forever = UINT32_MAX;

enum class Error : uint8_t {
    None,
    NoSteps,
    TooManySteps,
    ZeroDurationFade,        /* a Step transition may be instant; a fade may not */
    ZeroRepeatCount,
    LandingColorOnEndless,   /* an endless sequence never lands */
    StartColorOnEndless,     /* negative playheads wrap, so nothing is ever "before" */
    TooLong,                 /* over 2^29 ms in total, or per loop if endless */
    ZeroLengthLoop,          /* an endless loop of 0 ms has nowhere to wrap to */
};

class SequenceBuilder;

class Sequence {
public:
    constexpr Sequence() = default;          /* empty: black, held forever, like no_sequence */

    /* The one question a sequence answers. The playhead is signed and unbounded:
     * negative means before the timeline, large means past the end. Durations
     * elsewhere are uint32_t and clock readings int64_t; a playhead is signed, and
     * it must be -- a pixel pointing before the start of a timeline is the
     * mechanism, not an edge case. */
    Sample at(int32_t playhead_ms) const;

    /* These two are here for the library's own use rather than the caller's:
     * Sequencer needs the loop length to narrow an endless playhead, and
     * Segment::finished() asks whether there is a total at all. Endless reads
     * as hold_forever.
     *
     * Neither can overflow: the builder rejects anything over 2^29 ms. */
    constexpr uint32_t loop_duration_ms() const { return loop_ms_; }
    constexpr uint32_t total_duration_ms() const {
        return repeats_ == 0 ? hold_forever : loop_duration_ms() * repeats_;
    }

private:
    /* The builder is the only writer. Nothing else can produce a Sequence with
     * steps in it, so every rule build() checks holds for every Sequence that
     * at() is ever asked about. */
    friend class SequenceBuilder;

    Step     steps_[STRIPSEQ_MAX_STEPS] = {};   /* capacity: always allocated in full */
    uint8_t  count_ = 0;                      /* of those, how many are live */
    uint16_t repeats_ = 0;                    /* 0 = endless */
    /* The sum of the live steps' durations. Stored rather than summed, because
     * at() needs it for every pixel of every frame and it cannot change once
     * build() has written it. After count_ and repeats_, which fill four bytes,
     * so it lands aligned and adds no padding. */
    uint32_t loop_ms_ = 0;
    Color    start_ = {};                     /* finite only; build() resolves the default */
    Color    landing_ = {};                   /* finite only; build() resolves the default */
};

/* The sequence with no steps: black, held forever. A sequencer refers to it
 * until something is loaded, and load(no_sequence) is how to unload, so a
 * sequencer always has a sequence and nothing that reads one checks for null.
 * inline, so every translation unit shares the one object at one address. */
inline constexpr Sequence no_sequence{};

/* The compile-time failure path of SequenceBuilder::build_const(), one function
 * per Error.
 *
 * Declared, never defined, never constexpr. Calling one in a constant
 * expression is a compile error, and the diagnostic names the function -- so
 * the name is the error message every user of a malformed compile-time
 * sequence reads, and says what is wrong rather than only that something is.
 * Calling one at run time would be a link error, but build_const() is consteval
 * and so never runs then.
 *
 * Not `throw`: a throw in a constant expression is the usual consteval trick,
 * and it is ill-formed under -fno-exceptions, which is how Zephyr builds C++. */
namespace invalid_sequence {
void sequence_has_no_steps();
void sequence_has_more_steps_than_STRIPSEQ_MAX_STEPS();
void sequence_has_a_zero_duration_fade();
void sequence_repeat_count_is_zero();
void sequence_is_endless_but_has_a_landing_color();
void sequence_is_endless_but_has_a_start_color();
void sequence_is_longer_than_2_pow_29_ms();
void sequence_is_endless_but_its_loop_is_0_ms();
}  // namespace invalid_sequence

class SequenceBuilder {
public:
    constexpr SequenceBuilder() = default;

    /* Every call below is a no-op once one has failed. The error is sticky:
     * the first one is kept and build() reports it, so a tenth fade_to against
     * a capacity of eight neither clobbers the eighth nor vanishes silently. */

    constexpr SequenceBuilder& fade_to(Color c, uint32_t ms,
                                       Transition t = Transition::Linear) {
        if (error_ != Error::None)
            return *this;
        if (ms == 0 && t != Transition::Step)
            error_ = Error::ZeroDurationFade;
        else if (seq_.count_ == STRIPSEQ_MAX_STEPS)
            error_ = Error::TooManySteps;
        else
            seq_.steps_[seq_.count_++] = Step{c, t, ms};
        return *this;
    }
    constexpr SequenceBuilder& hold(Color c, uint32_t ms) {   /* sugar for Transition::Step */
        return fade_to(c, ms, Transition::Step);
    }
    constexpr SequenceBuilder& endless() {                    /* the default */
        if (error_ == Error::None)
            seq_.repeats_ = 0;
        return *this;
    }
    constexpr SequenceBuilder& repeat(uint16_t times) {
        if (error_ != Error::None)
            return *this;
        if (times == 0)
            error_ = Error::ZeroRepeatCount;   /* endless() is how to say "forever" */
        else
            seq_.repeats_ = times;
        return *this;
    }
    /* Finite only. Left unset, build() resolves start_color to black and
     * landing_color to the last step's color: a sequence that starts dark is
     * the wipe's default, and one that blanks as soon as it finishes is almost
     * never wanted. The first pass fades from start_color, so a default of the
     * first step's color would make a one-step finite fade no fade at all. */
    constexpr SequenceBuilder& start_color(Color c) {
        if (error_ == Error::None) {
            seq_.start_ = c;
            start_set_ = true;
        }
        return *this;
    }
    constexpr SequenceBuilder& landing_color(Color c) {
        if (error_ == Error::None) {
            seq_.landing_ = c;
            landing_set_ = true;
        }
        return *this;
    }

    /* Runtime path: reports rather than traps, because a sequence assembled from
     * user input can legitimately be invalid. `out` is written only on success,
     * so a rejected rebuild leaves the previous sequence intact.
     *
     * The finite-only colors are checked here rather than in their setters,
     * because the calls may come in any order: the wipe recipe sets
     * start_color() before repeat() has made the sequence finite. */
    [[nodiscard]] constexpr Error build(Sequence& out) const {
        if (error_ != Error::None)
            return error_;
        if (seq_.count_ == 0)
            return Error::NoSteps;
        if (seq_.repeats_ == 0 && landing_set_)
            return Error::LandingColorOnEndless;
        if (seq_.repeats_ == 0 && start_set_)
            return Error::StartColorOnEndless;

        /* In uint64_t, because both the sum and the product overflow uint32_t
         * long before they look suspicious: eight steps of 2^29 ms, or one loop
         * of 2^16 ms repeated 2^16 times. Neither can overflow uint64_t, whose
         * largest possible value here is 8 x (2^32 - 1) x (2^16 - 1). */
        uint64_t loop = 0;
        for (uint8_t i = 0; i < seq_.count_; ++i)
            loop += seq_.steps_[i].duration_ms;
        const uint64_t total = seq_.repeats_ == 0 ? loop : loop * seq_.repeats_;
        if (total > max_duration_ms)
            return Error::TooLong;
        /* Only endless: a finite sequence of 0 ms is a clean edge, start_color
         * before zero and landing_color from it, while an endless one would
         * leave at() taking a playhead modulo 0. */
        if (seq_.repeats_ == 0 && loop == 0)
            return Error::ZeroLengthLoop;

        out = seq_;
        out.loop_ms_ = static_cast<uint32_t>(loop);   /* under 2^29, checked above */
        if (seq_.repeats_ != 0) {
            if (!start_set_)
                out.start_ = colors::black;
            if (!landing_set_)
                out.landing_ = seq_.steps_[seq_.count_ - 1].color;
        }
        return Error::None;
    }

    /* Compile-time path: the same rules, enforced by constant evaluation.
     * consteval, so an invalid sequence is a compile error rather than a
     * runtime check that cannot fail. The error path calls one of the
     * functions in invalid_sequence above, which is what makes it an error.
     *
     * Which of the two to call is decided by where the values come from, not
     * where the call sits: build_const() compiles anywhere the whole
     * expression is constant, inside an ordinary function included, and stops
     * compiling the moment a color or duration comes from a variable. */
    consteval Sequence build_const() const {
        Sequence out{};
        using namespace invalid_sequence;
        switch (build(out)) {
        case Error::None:
            break;
        case Error::NoSteps:
            sequence_has_no_steps();
            break;
        case Error::TooManySteps:
            sequence_has_more_steps_than_STRIPSEQ_MAX_STEPS();
            break;
        case Error::ZeroDurationFade:
            sequence_has_a_zero_duration_fade();
            break;
        case Error::ZeroRepeatCount:
            sequence_repeat_count_is_zero();
            break;
        case Error::LandingColorOnEndless:
            sequence_is_endless_but_has_a_landing_color();
            break;
        case Error::StartColorOnEndless:
            sequence_is_endless_but_has_a_start_color();
            break;
        case Error::TooLong:
            sequence_is_longer_than_2_pow_29_ms();
            break;
        case Error::ZeroLengthLoop:
            sequence_is_endless_but_its_loop_is_0_ms();
            break;
        }
        return out;
    }

private:
    /* The limit is what lets the sequencer cap a finite playhead at 2^30 ms and
     * still have every segment finish, and keeps an endless one's remainder
     * plus any stagger inside int32_t -- see Sequencer::playhead(). */
    static constexpr uint64_t max_duration_ms = uint64_t{1} << 29;

    /* The sequence under construction. build() copies it out and fills in the
     * defaults the two flags below call for. */
    Sequence seq_ = {};
    Error    error_ = Error::None;

    /* Whether start_color() and landing_color() were called. Color has no spare
     * value to mean "unset" -- black is a legitimate start color -- and the
     * builder needs the distinction twice: to reject either call on an endless
     * sequence even when the color is black, and to know when build() should
     * supply the default instead. Sequence keeps neither bit, so at() never
     * branches on one. Not std::optional: <optional> is outside the two
     * headers the core may include. */
    bool start_set_ = false;
    bool landing_set_ = false;
};

/* A sequence that is nothing but a running playhead, for a ColorFn that draws
 * from time alone and never looks at a step. A segment needs a sequencer and
 * a sequencer needs a sequence, so even a fire computed from the clock has to
 * load one; this is that one, so it does not have to be worked out again.
 *
 * Endless, so a segment on it is never finished and is never dropped, and one
 * black step of the longest loop the builder accepts, 2^29 ms, so the
 * playhead the sequencer hands out takes as long as it can to come round: 0
 * at play(), and back to 0 every 6.2 days. A ColorFn that keys a pattern off
 * the playhead -- a slot of time hashed into a spark -- jumps once at that
 * wrap, and nowhere else. Each pixel's playhead is the sequencer's less its
 * stagger, as always, so on a staggered segment a ColorFn sees negative
 * playheads for the first moments after play(). The black step is what the
 * segment shows if its ColorFn is taken away.
 *
 * Not named clock: with `using namespace stripseq`, that collides with
 * <time.h>'s clock() the moment both are in scope. */
inline constexpr Sequence timebase =
    SequenceBuilder{}.hold(colors::black, uint32_t{1} << 29).build_const();

}  // namespace stripseq

#endif  /* STRIPSEQ_SEQUENCE_H_ */
