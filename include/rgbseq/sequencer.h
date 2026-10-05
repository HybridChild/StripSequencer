/* Sequencer: the playhead, and the one bit of state around it -- whether the
 * playhead follows the clock or stands still.
 *
 * Nothing stores a playhead as such. origin_ms_ and frozen_ are the two
 * representations, playing_ says which is authoritative, and the value is
 * derived on every call; a stored playhead advanced each frame would be the
 * tape model docs/DESIGN.md rejects. Why there is one bit here and not a state
 * machine is argued in docs/API-SKETCH.md, "Sequencer".
 */

#ifndef RGBSEQ_SEQUENCER_H_
#define RGBSEQ_SEQUENCER_H_

#include <cstdint>

#include "rgbseq/sequence.h"

namespace rgbseq {

class Sequencer {
public:
    void load(const Sequence& s);     /* copies; playhead 0, not playing */

    /* play:  the playhead follows the clock from wherever it stands.
     * pause: it stops following the clock and keeps its value.
     * seek:  it is set directly, and does not follow the clock until play.
     * play while playing and pause while paused change nothing: either one
     * re-derived from the other state's stale field would make the playhead
     * jump. */
    void play(uint32_t now_ms);
    void pause(uint32_t now_ms);
    void seek(int32_t playhead_ms);

    bool playing() const;
    /* What a segment calls at() on, once per pixel, with that pixel's own
     * staggered playhead. The sequencer cannot do the lookup itself: it knows
     * only the unstaggered playhead. */
    const Sequence& sequence() const;

    /* The sequencer's position at now_ms, unstaggered: now_ms - origin_ms_
     * while playing, frozen_ otherwise. A segment asks once per render() and
     * staggers every pixel from the answer.
     *
     * Non-const on purpose. It moves the origin as it goes -- by whole loops for
     * an endless sequence, to hold a finite one at 2^30 ms past its start -- so
     * the 49.7-day wrap is never reached, and that mutation has to happen
     * somewhere the sequencer is being asked for its position anyway. */
    int32_t playhead(uint32_t now_ms);

private:
    Sequence sequence_;
    uint32_t origin_ms_ = 0;    /* playing: the instant playhead 0 sits at */
    int32_t  frozen_ = 0;       /* not playing: the playhead itself */
    bool     playing_ = false;
};

}  // namespace rgbseq

#endif  /* RGBSEQ_SEQUENCER_H_ */
