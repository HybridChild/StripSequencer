/* Sequencer: the playhead, and the one bit of state around it -- whether the
 * playhead is advancing or standing still.
 *
 * Nothing stores a playhead as such. origin_ms_ and frozen_ are the two
 * representations, playing_ says which is authoritative, and the value is
 * derived on every call; a stored playhead advanced each frame would be the
 * tape model docs/DESIGN.md rejects. Why there is one bit here and not a state
 * machine is argued in docs/API-SKETCH.md, "Sequencer".
 *
 * Phase 0: declarations and stubs. Phase 3 brings the derivation, the re-basing
 * that keeps an endless sequence clear of the 49.7-day millisecond wrap, and
 * the clamp that keeps a finite one clear of int32_t overflow.
 */

#ifndef RGBSEQ_SEQUENCER_H_
#define RGBSEQ_SEQUENCER_H_

#include <cstdint>

#include "rgbseq/sequence.h"

namespace rgbseq {

class Sequencer {
public:
    void load(const Sequence& s);     /* copies; playhead 0, not playing */
    void play(uint32_t now_ms);       /* advance from wherever the playhead is */
    void pause(uint32_t now_ms);      /* stop advancing; the playhead stays put */
    void seek(int32_t playhead_ms);   /* place the playhead; stops advancing */

    bool playing() const;
    const Sequence& sequence() const;

    /* Non-const on purpose. For an endless sequence this re-bases the origin by
     * whole loops so the 49.7-day wrap is never reached, and that mutation has
     * to happen somewhere the sequencer is being asked for its position anyway. */
    int32_t playhead(uint32_t now_ms);

private:
    Sequence sequence_;
    uint32_t origin_ms_ = 0;    /* playing: the instant playhead 0 sits at */
    int32_t  frozen_ = 0;       /* not playing: the playhead itself */
    bool     playing_ = false;
};

}  // namespace rgbseq

#endif  /* RGBSEQ_SEQUENCER_H_ */
