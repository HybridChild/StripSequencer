/* Sequencer: deriving the playhead from a clock reading. */

#include "stripseq/sequencer.h"

namespace stripseq {

namespace {

/* Where a finite sequence's playhead is held once it is far past the end.
 * Safe because the builder refuses a total over 2^29 ms and a segment refuses
 * a stagger of 2^23 ms or more, so the capped playhead minus any stagger is
 * still past every total; see docs/API-SKETCH.md, "Sequencer". */
constexpr int32_t finite_cap_ms = int32_t{1} << 30;

}  // namespace

void Sequencer::load(const Sequence& s) {
    sequence_ = s;
    frozen_ = 0;
    playing_ = false;
}

void Sequencer::play(uint32_t now_ms) {
    if (playing_)
        return;   /* frozen_ is stale while playing: rebuilding from it would jump */
    origin_ms_ = now_ms - static_cast<uint32_t>(frozen_);
    playing_ = true;
    /* The playhead now reads frozen_. Re-base or cap it here, not on the next
     * call: one sought near INT32_MAX would overflow within milliseconds. */
    (void)playhead(now_ms);
}

void Sequencer::pause(uint32_t now_ms) {
    /* No guard needed: while paused, playhead() answers frozen_ itself and
     * never reads the stale origin_ms_, so pausing again changes nothing. */
    frozen_ = playhead(now_ms);
    playing_ = false;
}

void Sequencer::seek(int32_t playhead_ms) {
    frozen_ = playhead_ms;
    playing_ = false;
}

bool Sequencer::playing() const { return playing_; }

const Sequence& Sequencer::sequence() const { return sequence_; }

int32_t Sequencer::playhead(uint32_t now_ms) {
    if (!playing_)
        return frozen_;

    /* Unsigned first, so the clock's wrap cancels out of the difference. */
    const int32_t p = static_cast<int32_t>(now_ms - origin_ms_);

    if (sequence_.total_duration_ms() == hold_forever) {
        /* Endless: move the origin by whole loops, which no pixel can see. A
         * negative playhead is one at() already wraps, and an empty sequence
         * has nothing to wrap by. */
        const uint32_t loop = sequence_.loop_duration_ms();
        if (loop == 0 || p < 0 || static_cast<uint32_t>(p) < loop)
            return p;
        const uint32_t into_loop = static_cast<uint32_t>(p) % loop;
        origin_ms_ += static_cast<uint32_t>(p) - into_loop;
        return static_cast<int32_t>(into_loop);
    }

    /* Finite: hold the playhead at the cap by moving the origin with the
     * clock. Clamping only the answer would leave the difference underneath
     * growing until the cast above turned it negative. */
    if (p > finite_cap_ms) {
        origin_ms_ = now_ms - static_cast<uint32_t>(finite_cap_ms);
        return finite_cap_ms;
    }
    return p;
}

}  // namespace stripseq
