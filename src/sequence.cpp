/* Sequence::at() -- the function every other part of the library exists to
 * call -- and the easing curves behind it.
 */

#include "stripseq/sequence.h"

namespace stripseq {

Sample Sequence::at(int32_t /*playhead_ms*/) const {
    return Sample{colors::black, hold_forever};   /* stub */
}

}  // namespace stripseq
