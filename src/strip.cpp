/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Strip: the segment list and the frame loop. */

#include "stripseq/strip.h"

namespace stripseq {

bool Strip::add(const Segment& s) {
    if (count_ >= STRIPSEQ_MAX_SEGMENTS)
        return false;
    /* In uint32_t, so a segment near the top of the uint16_t range cannot wrap
     * its end back onto the strip whatever the width of int. Promotion alone
     * keeps the sum exact only where int is wider than 16 bits. */
    if (uint32_t{s.first_} + s.count_ > pixel_count_)
        return false;
    segments_[count_++] = s;
    return true;
}

bool Strip::remove(uint8_t at) {
    if (at >= count_)
        return false;
    for (uint8_t i = at; i + 1 < count_; ++i)
        segments_[i] = segments_[i + 1];
    segments_[--count_] = Segment();
    return true;
}

/* Each slot is reset as remove() resets the one it vacates, so a slot past the
 * count never holds a stale segment. */
void Strip::clear() {
    for (uint8_t i = 0; i < count_; ++i)
        segments_[i] = Segment();
    count_ = 0;
}

uint8_t Strip::segment_count() const { return count_; }

Segment& Strip::segment(uint8_t at) { return segments_[at]; }
const Segment& Strip::segment(uint8_t at) const { return segments_[at]; }

uint8_t Strip::drop_finished(int64_t now_ms) {
    /* Backwards, because remove() shifts every later entry down: a forward
     * loop would skip the segment after each removal. */
    const uint8_t before = count_;
    for (uint8_t i = count_; i > 0; --i)
        if (segments_[i - 1].finished(now_ms))
            remove(i - 1);
    return static_cast<uint8_t>(before - count_);
}

void Strip::set_brightness(uint8_t level) { brightness_ = level; }
uint8_t Strip::brightness() const { return brightness_; }

uint32_t Strip::render(int64_t now_ms) {
    /* A loop rather than memset, which would need <cstring>: not a freestanding
     * header in C++20, and outside the two the library includes. The
     * compiler is free to emit memset for it anyway. */
    const size_t bytes = size_t{pixel_count_} * bytes_per_pixel;
    for (size_t i = 0; i < bytes; ++i)
        buffer_[i] = 0;

    uint32_t hold = hold_forever;
    for (uint8_t i = 0; i < count_; ++i) {
        const uint32_t h = segments_[i].paint(buffer_, pixel_count_, brightness_, now_ms);
        if (h < hold)
            hold = h;
    }
    return hold;
}

}  // namespace stripseq
