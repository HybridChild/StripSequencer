/* Sequence::at() -- the function every other part of the library exists to
 * call -- and the easing curves behind it.
 */

#include "stripseq/sequence.h"

namespace stripseq {

namespace {

constexpr uint32_t full = 65535;   /* t at the end of a fade, and a full channel */

/* How far through a fade, as 0..65535, given elapsed < duration. The target
 * color itself is shown by the next step, from the instant the fade ends.
 *
 * Up to 65536 ms, elapsed * 65535 fits in uint32_t. Past that it does not, and
 * rather than a 64-bit division per pixel -- a library call on a Cortex-M --
 * both sides are shifted down until it fits again. The shifts stop with the
 * duration in (32768, 65536], so a unit of elapsed is still worth at most two
 * of the 65536 levels of t however long the fade, which is invisible; it
 * stays monotonic, and may reach the target slightly early. */
uint16_t progress(uint32_t elapsed, uint32_t duration) {
    while (duration > 65536) {
        elapsed >>= 1;
        duration >>= 1;
    }
    return static_cast<uint16_t>(elapsed * full / duration);
}

/* The curves, on a 0..65535 parameter in and out. Each is exact at both ends
 * and monotonic, because each is the floor of a monotonic function -- a curve
 * that topped out at 65534 would leave a one-LSB step at every transition
 * boundary, a visible tick in a slow breathe. */
uint16_t ease(Transition kind, uint16_t t) {
    switch (kind) {
    case Transition::EaseIn:
        return static_cast<uint16_t>(uint32_t{t} * t / full);            /* t^2 */
    case Transition::EaseOut: {
        const uint32_t u = full - t;
        return static_cast<uint16_t>(full - u * u / full);               /* 1 - (1-t)^2 */
    }
    case Transition::EaseInOut: {
        /* Smoothstep, t^2 (3 - 2t). The product needs 64 bits, and dividing it
         * by 65535^2 would be a 64-bit division per pixel -- a library call on
         * a Cortex-M, in the breathe every status light runs. So t is first
         * stretched onto 0..65536, where the divisor becomes 2^32 and the
         * division a shift. It stays one exact floor of a monotonic function,
         * so monotonic; it is within 2 LSB of the 65535-based curve, and the
         * top lands on 65536, which is clamped. Never round t^2 on its own to
         * stay in 32 bits: two separately floored terms can step backwards. */
        const uint64_t x = t + (t >> 15);
        const uint64_t y = (x * x * (3 * 65536 - 2 * x)) >> 32;
        return static_cast<uint16_t>(y < full ? y : full);
    }
    case Transition::Step:
    case Transition::Linear:
        break;
    }
    return t;
}

}  // namespace

Sample Sequence::at(int32_t playhead_ms) const {
    if (count_ == 0)
        return Sample{colors::black, hold_forever};

    const uint32_t loop = loop_duration_ms();

    /* Where in one pass through the steps, and what the first step fades from.
     * Only the very first pass of a finite sequence starts from start_; every
     * other pass, endless or finite, starts from where the previous one ended. */
    uint32_t local;
    Color    before_first = steps_[count_ - 1].color;

    if (repeats_ == 0) {
        /* An endless sequence is a ring. The modulo is the classic trap: in C++
         * `p % loop` is negative for a negative p, which is exactly the case a
         * staggered pixel produces, so it is folded back into [0, loop). The
         * builder caps a loop at 2^29 ms, so it fits in int32_t, and refuses
         * one of 0 ms, so the modulo is defined. */
        const int32_t ring = static_cast<int32_t>(loop);
        local = static_cast<uint32_t>(((playhead_ms % ring) + ring) % ring);
    } else {
        /* A finite sequence is a line, and its two ends have colors of their
         * own. Before the start the hold is the time until this pixel starts
         * moving -- what lets a wipe report a real deadline rather than a frame
         * rate. Negated in uint32_t, so INT32_MIN is no special case. */
        if (playhead_ms < 0)
            return Sample{start_, 0u - static_cast<uint32_t>(playhead_ms)};

        const uint32_t p = static_cast<uint32_t>(playhead_ms);
        if (p >= loop * repeats_)
            return Sample{landing_, hold_forever};

        local = p % loop;
        if (p < loop)
            before_first = start_;
    }

    /* Find the step containing `local`. A zero-length step contains nothing
     * and is passed over, but its color is still what the next step fades
     * from. The walk always ends inside the loop, because local < loop. */
    uint8_t  i = 0;
    uint32_t begin = 0;
    while (local >= begin + steps_[i].duration_ms) {
        begin += steps_[i].duration_ms;
        ++i;
    }

    const Step& step = steps_[i];
    const uint32_t end = begin + step.duration_ms;

    if (step.transition == Transition::Step)
        return Sample{step.color, end - local};

    const Color from = i == 0 ? before_first : steps_[i - 1].color;
    const uint16_t t = ease(step.transition, progress(local - begin, step.duration_ms));
    return Sample{Color::lerp(from, step.color, t), 0};
}

}  // namespace stripseq
