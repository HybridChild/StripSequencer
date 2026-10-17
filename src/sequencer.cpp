/* Sequencer: deriving the playhead from a clock reading. */

#include "stripseq/sequencer.h"

namespace stripseq {

namespace {

/* How far either side of zero a finite sequence's playhead is narrowed to.
 * Exact because the builder refuses a total over 2^29 ms and a segment refuses
 * a stagger of 2^23 ms or more, so the clamped playhead minus any stagger is
 * still past every total, plus any stagger still before the start, and either
 * way still inside int32_t; see docs/API-SKETCH.md, "Sequencer". */
constexpr int64_t finite_limit_ms = int64_t{1} << 30;

}  // namespace

void Sequencer::load(const Sequence& s) {
    sequence_ = s;
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

const Sequence& Sequencer::sequence() const { return sequence_; }

int64_t Sequencer::position(int64_t now_ms) const {
    return playing_ ? now_ms - origin_ms_ : frozen_;
}

int32_t Sequencer::playhead(int64_t now_ms) const {
    const int64_t p = position(now_ms);

    if (sequence_.total_duration_ms() == hold_forever) {
        /* Endless: at() reads modulo the loop anyway, so this is exact. In C++
         * `p % loop` is negative for a negative p, hence the fold back up. An
         * empty sequence has a loop of zero and nothing to wrap by, and at()
         * answers black whatever it is asked. */
        const int64_t loop = sequence_.loop_duration_ms();
        if (loop == 0)
            return 0;
        const int64_t into_loop = p % loop;
        return static_cast<int32_t>(into_loop < 0 ? into_loop + loop : into_loop);
    }

    /* Finite: clamping the answer is enough, because the difference underneath
     * is int64_t and cannot overflow on its way here. */
    const int64_t clamped = p < -finite_limit_ms ? -finite_limit_ms
                          : p > finite_limit_ms  ? finite_limit_ms
                                                 : p;
    return static_cast<int32_t>(clamped);
}

}  // namespace stripseq
