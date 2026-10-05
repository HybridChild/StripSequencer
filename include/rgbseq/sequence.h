/* Sequence: pure description, and the one question the whole design is built
 * around -- given a playhead, what color is here, and how long does it stay?
 *
 * A sequence holds its steps inline, so a sequencer can own one by value and
 * get snapshot semantics: rebuilding a sequence cannot disturb one already
 * running. It holds no state, references no hardware, and can be built at
 * compile time (SequenceBuilder::build_const) or at run time (build).
 *
 * The mechanism is docs/DESIGN.md, "A sequence is a lookup table, not a tape";
 * the decisions beside each declaration are docs/API-SKETCH.md, "Sequence".
 */

#ifndef RGBSEQ_SEQUENCE_H_
#define RGBSEQ_SEQUENCE_H_

#include <cstdint>
#include <type_traits>

#include "rgbseq/color.h"
#include "rgbseq/config.h"

namespace rgbseq {

enum class Transition : uint8_t { Step, Linear, EaseIn, EaseOut, EaseInOut };

struct Step {
    Color      color;        /* the color arrived at by the end of this step */
    Transition transition;   /* how it is approached from the previous color */
    uint32_t   duration_ms;
};

/* Field order above is load-bearing, not stylistic. `duration_ms` follows the
 * one-byte `transition` so that it lands in the padding trailing `color`.
 * Putting the duration first -- the order that reads more naturally -- costs
 * four bytes per step in an RGB build, multiplied by RGBSEQ_MAX_STEPS in every
 * sequencer. An RGBW build is 16 either way, since the eight-byte color leaves
 * the duration already aligned; only RGB is sensitive, and RGB is the default. */
static_assert(sizeof(Step) == (RGBSEQ_WHITE_CHANNEL ? 16 : 12),
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
};

class Sequence {
public:
    constexpr Sequence() = default;          /* empty; valid to hold, not to render */

    /* The one question a sequence answers. The playhead is signed and unbounded:
     * negative means before the timeline, large means past the end. Instants and
     * durations elsewhere are uint32_t; only playheads are signed, and they must
     * be -- a pixel pointing before the start of a timeline is the mechanism,
     * not an edge case. */
    Sample at(int32_t playhead_ms) const;

    /* These two are here for the library's own use rather than the caller's:
     * Sequencer needs the loop length to re-base an endless origin, and
     * Segment::finished() needs the total. Endless reads as hold_forever. */
    constexpr uint32_t loop_duration_ms() const { return 0; }   /* stub */
    constexpr uint32_t total_duration_ms() const { return 0; }  /* stub */

private:
    Step     steps_[RGBSEQ_MAX_STEPS] = {};   /* capacity: always allocated in full */
    uint8_t  count_ = 0;                      /* of those, how many are live */
    uint16_t repeats_ = 0;                    /* 0 = endless */
    Color    start_ = {};                     /* finite only; build() resolves the default */
    Color    landing_ = {};                   /* finite only; build() resolves the default */
};

/* Sequencer::load() copies a sequence, which is cheap and safe only while a
 * sequence is a plain value: steps inline, nothing behind a pointer, no copy
 * constructor of its own. It is also why load() has no move overload -- with
 * nothing to steal, moving a sequence is the same copy. */
static_assert(std::is_trivially_copyable_v<Sequence>,
              "Sequence must stay a plain value -- Sequencer::load() copies it.");

class SequenceBuilder {
public:
    constexpr SequenceBuilder() = default;

    constexpr SequenceBuilder& fade_to(Color /*c*/, uint32_t /*ms*/,
                                       Transition /*t*/ = Transition::Linear) {
        return *this;   /* stub */
    }
    constexpr SequenceBuilder& hold(Color /*c*/, uint32_t /*ms*/) {   /* sugar for Transition::Step */
        return *this;   /* stub */
    }
    constexpr SequenceBuilder& endless() {                            /* the default */
        return *this;   /* stub */
    }
    constexpr SequenceBuilder& repeat(uint16_t /*times*/) {
        return *this;   /* stub */
    }
    constexpr SequenceBuilder& start_color(Color /*c*/) {             /* finite only */
        start_set_ = true;
        return *this;   /* stub */
    }
    constexpr SequenceBuilder& landing_color(Color /*c*/) {           /* finite only */
        landing_set_ = true;
        return *this;   /* stub */
    }

    /* Runtime path: reports rather than traps, because a sequence assembled from
     * user input can legitimately be invalid. */
    [[nodiscard]] constexpr Error build(Sequence& /*out*/) const {
        return Error::None;   /* stub */
    }

    /* Compile-time path: the same rules, enforced by constant evaluation.
     * consteval, so an invalid sequence is a compile error rather than a
     * runtime check that cannot fail. The failure path cannot use throw:
     * -fno-exceptions is how Zephyr builds C++ by default. */
    consteval Sequence build_const() const {
        return Sequence{};    /* stub */
    }

private:
    /* Whether start_color() and landing_color() were called. Color has no spare
     * value to mean "unset" -- black is a legitimate start color -- and the
     * builder needs the distinction twice: to reject either call on an endless
     * sequence even when the color is black, and to know when build() should
     * supply the default instead. Sequence keeps neither bit, so at() never
     * branches on one. Not std::optional: <optional> is outside the three
     * headers the core may include. */
    bool start_set_ = false;
    bool landing_set_ = false;
};

}  // namespace rgbseq

#endif  /* RGBSEQ_SEQUENCE_H_ */
