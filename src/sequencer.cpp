/* Sequencer: deriving the playhead from a clock reading.
 *
 * Phase 0: stubs. Phase 3 brings the derivation itself, the re-basing that
 * keeps an endless sequence clear of the 49.7-day wrap, and the clamp that
 * keeps a finite one clear of int32_t overflow.
 */

#include "rgbseq/sequencer.h"

namespace rgbseq {

void Sequencer::load(const Sequence& /*s*/) {}          /* stub -- Phase 3 */
void Sequencer::play(uint32_t /*now_ms*/) {}            /* stub -- Phase 3 */
void Sequencer::pause(uint32_t /*now_ms*/) {}           /* stub -- Phase 3 */
void Sequencer::seek(int32_t /*playhead_ms*/) {}        /* stub -- Phase 3 */

bool Sequencer::playing() const { return playing_; }

const Sequence& Sequencer::sequence() const { return sequence_; }

int32_t Sequencer::playhead(uint32_t /*now_ms*/) {
    return frozen_;   /* stub -- Phase 3 */
}

}  // namespace rgbseq
