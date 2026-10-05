/* Strip: the segment list and the frame loop. */

#include "rgbseq/strip.h"

namespace rgbseq {

bool Strip::add(const Segment& /*s*/) { return false; }   /* stub */
bool Strip::remove(uint8_t /*at*/) { return false; }      /* stub */

uint8_t Strip::segment_count() const { return count_; }

Segment& Strip::segment(uint8_t at) { return segments_[at]; }
const Segment& Strip::segment(uint8_t at) const { return segments_[at]; }

uint8_t Strip::drop_finished(uint32_t /*now_ms*/) { return 0; }   /* stub */

void Strip::set_brightness(uint8_t level) { brightness_ = level; }
uint8_t Strip::brightness() const { return brightness_; }

uint32_t Strip::render(uint32_t /*now_ms*/) {
    return hold_forever;   /* stub */
}

}  // namespace rgbseq
