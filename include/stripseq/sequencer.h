/* Sequencer: the playhead, and the one bit of state around it -- whether the
 * playhead follows the clock or stands still.
 *
 * Nothing stores a playhead as such. origin_ms_ and frozen_ are the two
 * representations, playing_ says which is authoritative, and the value is
 * derived on every call; a stored playhead advanced each frame would be the
 * tape model docs/DESIGN.md rejects. Why there is one bit here and not a state
 * machine is argued in docs/API-SKETCH.md, "Sequencer".
 */

#ifndef STRIPSEQ_SEQUENCER_H_
#define STRIPSEQ_SEQUENCER_H_

#include <cstdint>
#include <type_traits>

#include "stripseq/sequence.h"

namespace stripseq {

/* A clock reading narrow enough to wrap while a device runs: 32-bit
 * milliseconds roll over after 49.7 days. Every function that takes now_ms
 * takes int64_t, and has a deleted overload for these, because widening one
 * converts silently -- -Wconversion stays quiet, since no value is lost -- and
 * keeps the wrap as a 2^32 ms jump backwards. Extend the tick to 64 bits
 * first: under Zephyr that is k_uptime_get() in place of k_uptime_get_32().
 * Spelled as a concept rather than as uint32_t, which is unsigned int on one
 * target and unsigned long on another, and built from traits: the standard's
 * own std::unsigned_integral is in <concepts>, outside the three headers the
 * library includes. */
template <typename T>
concept wrapping_tick = std::is_integral_v<T> && std::is_unsigned_v<T> && sizeof(T) <= 4;

class Sequencer {
public:
    void load(const Sequence& s);     /* copies; playhead 0, not playing */

    /* play:  the playhead follows the clock from wherever it stands.
     * pause: it stops following the clock and keeps its value.
     * seek:  it is set directly, and does not follow the clock until play.
     * play while playing and pause while paused change nothing: either one
     * re-derived from the other state's stale field would make the playhead
     * jump. */
    void play(int64_t now_ms);
    void pause(int64_t now_ms);
    void seek(int32_t playhead_ms);

    template <wrapping_tick T> void play(T) = delete;
    template <wrapping_tick T> void pause(T) = delete;

    bool playing() const;
    /* What a segment calls at() on, once per pixel, with that pixel's own
     * staggered playhead. The sequencer cannot do the lookup itself: it knows
     * only the unstaggered playhead. */
    const Sequence& sequence() const;

    /* The sequencer's position at now_ms, unstaggered, narrowed to a range any
     * accepted stagger can be added to or taken from: [0, loop) for an endless
     * sequence, [-2^30, 2^30] for a finite one. Both are exact through at(),
     * which reads an endless playhead modulo the loop and shows a finite one's
     * start color below 0 and its landing color past its total. A segment asks
     * once per render() and staggers every pixel from the answer. */
    int32_t playhead(int64_t now_ms) const;

    template <wrapping_tick T> int32_t playhead(T) const = delete;

private:
    /* The position before narrowing. In int64_t milliseconds it cannot
     * overflow in any span a device runs for, so nothing here protects it. */
    int64_t position(int64_t now_ms) const;

    Sequence sequence_;
    int64_t  origin_ms_ = 0;    /* playing: the instant position 0 sits at */
    int64_t  frozen_ = 0;       /* not playing: the position itself */
    bool     playing_ = false;
};

}  // namespace stripseq

#endif  /* STRIPSEQ_SEQUENCER_H_ */
