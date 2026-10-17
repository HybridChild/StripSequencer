/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Sequencer: deriving the playhead from a clock reading. */

#include "stripseq/sequencer.h"

namespace stripseq {

namespace {

/* How far either side of zero a finite sequence's playhead is narrowed to.
 * Exact because the builder refuses a total over 2^29 ms and a segment refuses
 * a stagger beyond 2^23 ms either way, so the clamped playhead minus any
 * stagger is still past every total, plus any stagger still before the start,
 * and either way still inside int32_t. A constant rather than
 * total_duration_ms(): a pixel's playhead is this one minus its stagger, so a
 * clamp at the total would leave every staggered pixel short of its landing
 * color forever, and the sequencer cannot add the stagger itself, since it
 * does not know which segments read from it. */
constexpr int64_t finite_limit_ms = int64_t{1} << 30;

/* n % d, for a d of at most 2^31, in 32-bit arithmetic only. A 64-bit `%` is
 * a call into libgcc on every Cortex-M, and this is the one place the library
 * would make it.
 *
 * Long division in base 2: n is hi * 2^32 + lo, so the high word is reduced by
 * the core's own 32-bit remainder and the low word's bits are brought down one
 * at a time, most significant first, each doubling what is left over. The
 * remainder stays below d, so doubling it and adding a bit stays below 2^32.
 * One division and 32 shift-and-subtract steps, whatever n is: the cost of a
 * frame never depends on how long the sequencer has been playing.
 *
 * Bit by bit rather than three bits at a time through `%`, which the headroom
 * under a 2^29 loop would also allow: that is eleven divisions, and on a
 * Cortex-M0, which has no divide instruction, eleven library calls. */
uint32_t remainder(uint64_t n, uint32_t d) {
    uint32_t r = static_cast<uint32_t>(n >> 32) % d;
    const auto lo = static_cast<uint32_t>(n);
    for (int bit = 31; bit >= 0; --bit) {
        r = (r << 1) | ((lo >> bit) & 1u);
        if (r >= d)
            r -= d;
    }
    return r;
}

}  // namespace

void Sequencer::load(const Sequence& s) {
    sequence_ = &s;
    frozen_ = 0;
    playing_ = false;
}

void Sequencer::play(int64_t now_ms) {
    if (playing_)
        return;   /* frozen_ is stale while playing: rebuilding from it would jump */
    origin_ms_ = now_ms - frozen_;
    playing_ = true;
}

void Sequencer::pause(int64_t now_ms) {
    /* No guard needed: while paused, position() answers frozen_ itself and
     * never reads the stale origin_ms_, so pausing again changes nothing. */
    frozen_ = position(now_ms);
    playing_ = false;
}

void Sequencer::seek(int32_t playhead_ms) {
    frozen_ = playhead_ms;
    playing_ = false;
}

bool Sequencer::playing() const { return playing_; }

const Sequence& Sequencer::sequence() const { return *sequence_; }

int64_t Sequencer::position(int64_t now_ms) const {
    return playing_ ? now_ms - origin_ms_ : frozen_;
}

int32_t Sequencer::playhead(int64_t now_ms) const {
    const int64_t p = position(now_ms);

    if (sequence_->total_duration_ms() == hold_forever) {
        /* Endless: at() reads modulo the loop anyway, so this is exact. An
         * empty sequence has a loop of zero and nothing to wrap by, and at()
         * answers black whatever it is asked.
         *
         * The remainder is taken of p's magnitude, unsigned, and a negative p
         * folded back up by hand, because in C++ `p % loop` takes the sign of
         * p. Negated in uint64_t, so INT64_MIN is no special case. The builder
         * holds the loop to 2^29 ms, inside what remainder() can divide by. */
        const uint32_t loop = sequence_->loop_duration_ms();
        if (loop == 0)
            return 0;
        const uint64_t magnitude = p < 0 ? 0 - static_cast<uint64_t>(p) : static_cast<uint64_t>(p);
        const uint32_t into_loop = remainder(magnitude, loop);
        return static_cast<int32_t>(p < 0 && into_loop != 0 ? loop - into_loop : into_loop);
    }

    /* Finite: clamping the answer is enough, because the difference underneath
     * is int64_t and cannot overflow on its way here. */
    const int64_t clamped = p < -finite_limit_ms ? -finite_limit_ms
                          : p > finite_limit_ms  ? finite_limit_ms
                                                 : p;
    return static_cast<int32_t>(clamped);
}

}  // namespace stripseq
