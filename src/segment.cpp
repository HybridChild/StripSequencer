/* Segment: the per-pixel loop, the stagger, and the finished() question. */

#include "rgbseq/segment.h"

namespace rgbseq {

Segment::Segment(uint16_t first, uint16_t count, Sequencer& seq)
    : first_(first), count_(count), sequencer_(&seq) {}

bool Segment::set_pixel_delay(int32_t /*ms*/) { return false; }        /* stub */
bool Segment::set_pixel_delay_q8(int32_t /*ms_q8*/) { return false; }  /* stub */

void Segment::set_playhead_fn(PlayheadFn /*fn*/) {}   /* stub */
void Segment::set_color_fn(ColorFn /*fn*/) {}         /* stub */

bool Segment::finished(uint32_t /*now_ms*/) const {
    return false;   /* stub */
}

bool Segment::drives(const Sequencer& seq) const {
    return sequencer_ == &seq;
}

uint32_t Segment::paint(uint8_t* /*buffer*/, uint16_t /*pixel_count*/,
                        uint8_t /*brightness*/, uint32_t /*now_ms*/) const {
    return hold_forever;   /* stub */
}

}  // namespace rgbseq
