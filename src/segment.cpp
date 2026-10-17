/* Segment: the per-pixel loop, the stagger, and the finished() question. */

#include "stripseq/segment.h"

namespace stripseq {

Segment::Segment(uint16_t first, uint16_t count, Sequencer& seq)
    : first_(first), count_(count), sequencer_(&seq) {}

bool Segment::set_pixel_delay(int32_t /*ms*/) { return false; }        /* stub */
bool Segment::set_pixel_delay_q8(int32_t /*ms_q8*/) { return false; }  /* stub */

void Segment::set_offset_fn(OffsetFn /*fn*/) {}       /* stub */
void Segment::set_color_fn(ColorFn /*fn*/) {}         /* stub */

bool Segment::finished(int64_t /*now_ms*/) const {
    return false;   /* stub */
}

bool Segment::drives(const Sequencer& seq) const {
    return sequencer_ == &seq;
}

Sample Segment::sample(int32_t /*playhead_ms*/, uint16_t /*index*/) const {
    return {};   /* stub */
}

uint32_t Segment::paint(uint8_t* /*buffer*/, uint16_t /*pixel_count*/,
                        uint8_t /*brightness*/, int64_t /*now_ms*/) const {
    return hold_forever;   /* stub */
}

}  // namespace stripseq
