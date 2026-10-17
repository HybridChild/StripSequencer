/* Segment: the per-pixel loop, the stagger, and the finished() question. */

#include "stripseq/segment.h"

#include <cstddef>

#include "color_internal.h"
#include "stripseq/strip.h"

namespace stripseq {

namespace {

/* The range the default step 1 can produce. The setters keep index * delay_q8
 * within +-INT32_MAX, and an arithmetic shift floors, so the default's offset
 * lands in [-2^23, 2^23 - 1]. A replacement's offset is clamped to the same
 * range, which keeps every pixel's playhead inside int32_t whatever is
 * installed; see Sequencer::playhead for the other half of that argument. */
constexpr int32_t max_offset_ms = INT32_MAX >> 8;
constexpr int32_t min_offset_ms = -INT32_MAX >> 8;

/* The largest whole-millisecond delay that can be stored in 1/256 ms. */
constexpr int32_t max_delay_ms = INT32_MAX / 256;

}  // namespace

Segment::Segment(uint16_t first, uint16_t count, Sequencer& seq)
    : first_(first), count_(count), sequencer_(&seq) {}

bool Segment::set_pixel_delay(int32_t ms) {
    /* Refused before the multiply, whatever the count: a delay that cannot be
     * stored cannot be anything's parameter, even a one-pixel segment's. */
    if (ms > max_delay_ms || ms < -max_delay_ms)
        return false;
    return set_pixel_delay_q8(ms * 256);
}

bool Segment::set_pixel_delay_q8(int32_t ms_q8) {
    /* Checked once here so the pixel loop can multiply in int32_t without a
     * test of its own: |ms_q8| * span must not pass INT32_MAX. Magnitude
     * negated in uint32_t, so INT32_MIN is no special case, and a one-pixel
     * segment, with no span, accepts any delay.
     *
     * The product is taken in two 16-bit halves of the magnitude, so it
     * stays in 32 bits with no 64-bit multiply, and every partial product
     * fits: span is under 2^16 and each half at most 2^16 - 1. Never ask it
     * as INT32_MAX / span instead. Besides costing a library call on a
     * Cortex-M0, GCC 13 then also declares __aeabi_idiv for the unsigned
     * division of a divisor it can bound, and the linker pulls in an
     * undefined symbol whether or not anything calls it. */
    const uint32_t span = count_ > 0 ? count_ - 1u : 0u;
    const uint32_t magnitude = ms_q8 < 0 ? 0u - static_cast<uint32_t>(ms_q8)
                                         : static_cast<uint32_t>(ms_q8);
    const uint32_t high = (magnitude >> 16) * span;
    if (high > (static_cast<uint32_t>(INT32_MAX) >> 16))
        return false;
    if ((magnitude & 0xFFFFu) * span > static_cast<uint32_t>(INT32_MAX) - (high << 16))
        return false;
    delay_q8_ = ms_q8;
    return true;
}

void Segment::set_offset_fn(OffsetFn fn) { where_ = fn; }
void Segment::set_color_fn(ColorFn fn) { what_ = fn; }

bool Segment::finished(int64_t now_ms) const {
    /* Asked before any pixel, because paint holds a sequencer that is not
     * playing forever and finished() must not mistake that for landing. A
     * segment with no sequencer is not playing anything. */
    if (sequencer_ == nullptr || !sequencer_->playing() ||
        sequencer_->sequence().total_duration_ms() == hold_forever)
        return false;

    const int32_t playhead = sequencer_->playhead(now_ms);
    for (uint16_t i = 0; i < count_; ++i)
        if (sample(playhead, i).hold_ms != hold_forever)
            return false;
    return true;
}

bool Segment::drives(const Sequencer& seq) const {
    return sequencer_ == &seq;
}

Sample Segment::sample(int32_t playhead_ms, uint16_t index) const {
    int32_t offset_ms;
    if (where_ == nullptr) {
        /* A shift rather than / 256: it floors in both directions, where a
         * division truncates toward zero and puts a one-count seam at the
         * origin of a negative delay. */
        offset_ms = (int32_t{index} * delay_q8_) >> 8;
    } else {
        offset_ms = where_(index, count_, delay_q8_);
        offset_ms = offset_ms > max_offset_ms ? max_offset_ms
                  : offset_ms < min_offset_ms ? min_offset_ms
                                              : offset_ms;
    }

    const int32_t mine = playhead_ms - offset_ms;
    return what_ != nullptr ? what_(mine, index, count_) : sequencer_->sequence().at(mine);
}

uint32_t Segment::paint(uint8_t* buffer, uint16_t pixel_count, uint8_t brightness,
                        int64_t now_ms) const {
    if (sequencer_ == nullptr || first_ >= pixel_count)
        return hold_forever;

    const uint16_t room = static_cast<uint16_t>(pixel_count - first_);
    const uint16_t visible = count_ < room ? count_ : room;

    const int32_t playhead = sequencer_->playhead(now_ms);
    uint8_t* out = buffer + size_t{first_} * Strip::bytes_per_pixel;
    uint32_t hold = hold_forever;

    /* Every pixel is painted, so a hold of 0 cannot end the loop early; the
     * fold is one comparison on work being done regardless. */
    for (uint16_t i = 0; i < visible; ++i, out += Strip::bytes_per_pixel) {
        const Sample s = sample(playhead, i);
        to_bytes(s.color, brightness, out);
        if (s.hold_ms < hold)
            hold = s.hold_ms;
    }

    /* The lookup reports holds as if the playhead were moving, because a
     * sequence cannot know whether anything plays it. A sequencer that is not
     * playing never moves, so none of these pixels will change. */
    return sequencer_->playing() ? hold : hold_forever;
}

}  // namespace stripseq
