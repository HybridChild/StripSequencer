/* Strip: the segment list and the frame loop.
 *
 * The strip owns its segments; the pixel buffer is yours. Rendering is a pure
 * function of the segments and the time you hand in -- nothing accumulates
 * between frames, and there is no 16-bit shadow buffer, because that would be
 * the per-pixel state docs/DESIGN.md forbids outright.
 *
 * Note what is absent: no LED type, no driver reference, no clock object, no
 * callbacks. Everything platform-shaped lives outside.
 */

#ifndef RGBSEQ_STRIP_H_
#define RGBSEQ_STRIP_H_

#include <cstddef>
#include <cstdint>

#include "rgbseq/config.h"
#include "rgbseq/segment.h"

namespace rgbseq {

class Strip {
public:
    /* Neutral packed layout: R,G,B[,W], one byte per channel, no padding.
     * Channel order is the platform's business, not the library's. */
    static constexpr size_t bytes_per_pixel = RGBSEQ_WHITE_CHANNEL ? 4 : 3;

    /* The buffer's size is deduced rather than passed, and the pixel count
     * follows from it, so neither can drift from the array it describes.
     * constexpr so the object can be constinit at file scope. */
    template <size_t B>
    constexpr explicit Strip(uint8_t (&buffer)[B])
        : buffer_(buffer), pixel_count_(static_cast<uint16_t>(B / bytes_per_pixel)) {
        /* A buffer that does not divide evenly is a channel-count mismatch --
         * usually an RGB-sized buffer handed to an RGBW build, which would
         * otherwise run off the end of the last pixel. */
        static_assert(B % bytes_per_pixel == 0,
                      "Buffer size is not a whole number of pixels -- "
                      "check it against Strip::bytes_per_pixel.");
        static_assert(B / bytes_per_pixel <= UINT16_MAX,
                      "More pixels than a uint16_t index can address.");
    }

    /* Position in the list is paint order: index 0 is painted first and later
     * entries overwrite it wherever they overlap. Segments are only appended,
     * so a background goes in first and overlays after it. */
    bool add(const Segment& s);   /* on top of everything already present */
    bool remove(uint8_t at);      /* false if at >= count */

    /* remove shifts every entry after `at`, so an index is a position and never
     * a handle: one held across a removal may now name a different segment, and
     * a Segment& returned earlier may now refer to one. Re-find the segment you
     * want rather than caching where it was. */

    uint8_t segment_count() const;

    /* The non-const reference is assignable, which is how a segment is resized:
     * `strip.segment(i) = Segment(0, n, seq);`. A segment holds no animation
     * state -- the playhead lives in the sequencer -- so replacing one costs
     * nothing and the animation underneath does not flinch.
     *
     * Note the asymmetry with add(): this path bypasses add()'s check that the
     * segment fits the buffer, by design, since the caller has already named a
     * position that does. */
    Segment& segment(uint8_t at);
    const Segment& segment(uint8_t at) const;

    /* Removes every segment reporting finished(), and returns how many went.
     * Not automatic: a finished segment keeps painting its landing color, which
     * is exactly what the wipe recipe depends on. */
    uint8_t drop_finished(uint32_t now_ms);

    void set_brightness(uint8_t level);   /* 0..255, applied during 8-bit conversion */
    uint8_t brightness() const;

    /* Paints every segment into the buffer and returns when to come back: the
     * smallest hold across every pixel of every segment. 0 = call again at your
     * frame rate, n = nothing changes for n ms, hold_forever = nothing on this
     * strip will ever change again. */
    uint32_t render(uint32_t now_ms);

private:
    /* Owned outright, which is the point: there is no array to hand in, so two
     * strips cannot be pointed at the same segments. */
    Segment  segments_[RGBSEQ_MAX_SEGMENTS];
    uint8_t  count_ = 0;
    uint8_t* buffer_ = nullptr;
    uint16_t pixel_count_ = 0;
    uint8_t  brightness_ = 255;
};

}  // namespace rgbseq

#endif  /* RGBSEQ_STRIP_H_ */
