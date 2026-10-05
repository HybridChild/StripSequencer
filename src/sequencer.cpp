/* Sequencer: deriving the playhead from a clock reading. */

#include "rgbseq/sequencer.h"

namespace rgbseq {

void Sequencer::load(const Sequence& /*s*/) {}          /* stub */
void Sequencer::play(uint32_t /*now_ms*/) {}            /* stub */
void Sequencer::pause(uint32_t /*now_ms*/) {}           /* stub */
void Sequencer::seek(int32_t /*playhead_ms*/) {}        /* stub */

bool Sequencer::playing() const { return playing_; }

const Sequence& Sequencer::sequence() const { return sequence_; }

int32_t Sequencer::playhead(uint32_t /*now_ms*/) {
    return frozen_;   /* stub */
}

}  // namespace rgbseq
