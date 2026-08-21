/* Sequence::at() -- the function every other part of the library exists to
 * call -- and the easing curves behind it.
 *
 * Phase 0: a stub. Phase 2 brings the lookup, the negative-playhead modulo, the
 * four hold cases, the integer easing curves and the builder's validation.
 */

#include "rgbseq/sequence.h"

namespace rgbseq {

Sample Sequence::at(int32_t /*playhead_ms*/) const {
    return Sample{colors::black, hold_forever};   /* stub -- Phase 2 */
}

}  // namespace rgbseq
