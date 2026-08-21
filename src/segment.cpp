/* Segment: the per-pixel stagger and the finished() question.
 *
 * Phase 0: stubs. Phase 4 brings the stagger arithmetic (an arithmetic shift,
 * not a division), the overflow guard at set time rather than per pixel, and
 * finished(), which lands a full stagger later than the sequencer's own end.
 */

#include "rgbseq/segment.h"

namespace rgbseq {

Segment::Segment(uint16_t first, uint16_t count, Sequencer& seq)
    : first_(first), count_(count), sequencer_(&seq) {}

bool Segment::set_pixel_delay(int32_t /*ms*/) { return false; }        /* stub -- Phase 4 */
bool Segment::set_pixel_delay_q8(int32_t /*ms_q8*/) { return false; }  /* stub -- Phase 4 */

void Segment::set_playhead_fn(PlayheadFn /*fn*/) {}   /* stub -- Phase 4 */
void Segment::set_color_fn(ColorFn /*fn*/) {}         /* stub -- Phase 4 */

bool Segment::finished(uint32_t /*now_ms*/) const {
    return false;   /* stub -- Phase 4 */
}

}  // namespace rgbseq
