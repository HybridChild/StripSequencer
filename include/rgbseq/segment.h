/* Segment: the only part of the design that knows pixels exist.
 *
 * A run of pixels, the sequencer driving them, and the pixel_delay that
 * staggers them -- plus the two optional replacements for the render loop's two
 * steps. There is no per-pixel state here and there must never be: that
 * invariant is the whole argument of docs/DESIGN.md.
 *
 * Phase 0: declarations and stubs. Phase 4 brings the per-pixel loop, the
 * stagger arithmetic (an arithmetic shift, not a division) and finished().
 */

#ifndef RGBSEQ_SEGMENT_H_
#define RGBSEQ_SEGMENT_H_

#include <cstdint>

#include "rgbseq/sequence.h"
#include "rgbseq/sequencer.h"

namespace rgbseq {

/* Step 1 of the render loop: where does this pixel look?
 *
 *   playhead_ms  the sequencer's own playhead, unstaggered -- the single number
 *                the sequencer owns, identical for every pixel in the segment.
 *   index        the pixel's position *within the segment*, 0-based -- not its
 *                position on the strip. A segment therefore behaves identically
 *                wherever it sits, and hash(index) stays stable if it moves.
 *   count        how many pixels the segment covers. The only way a replacement
 *                can normalise position: mirroring is (count - 1 - index).
 *   returns      this pixel's playhead, which is what step 2 gets asked about.
 *
 * Note what is absent: pixel_delay. The offset it produces is precisely the
 * difference between the returned value and playhead_ms -- the default is
 * playhead_ms - index * pixel_delay. A replacement computes that difference for
 * itself, so the delay is applied neither before the call nor after it.
 * Installing a replacement therefore clears the segment's pixel_delay, and the
 * setters refuse while one is in place, so no dormant value can linger.
 * Twinkle relies on this -- hash(index) has to *be* the offset, not be added to
 * a linear ramp already in place.
 *
 * Both plug-in types take (playhead_ms, index, count) in that order, so the
 * pipeline reads playhead in -> playhead out -> color out with the spatial
 * context trailing in both. */
using PlayheadFn = int32_t (*)(int32_t playhead_ms, uint16_t index, uint16_t count);

/* Step 2: what color is at that playhead?
 *
 *   playhead_ms  *this pixel's* playhead -- what step 1 returned, already
 *                staggered. Deliberately not the same number PlayheadFn was
 *                handed, despite the shared parameter name.
 *   index        as above: position within the segment, 0-based.
 *   count        as above: pixels in the segment. Present so that a color may
 *                depend on position directly, not only through time.
 *   returns      a Sample. See Sample for the contract; reporting the hold
 *                honestly is what lets a segment driven by your function sleep
 *                exactly as long as one driven by a step list. */
using ColorFn = Sample (*)(int32_t playhead_ms, uint16_t index, uint16_t count);

/* Fit intent: the delay that spreads total_ms evenly across count pixels.
 * A free function rather than a Segment method, because the result is just a
 * delay -- once computed there is nothing about it left to remember. */
constexpr int32_t spread(uint32_t /*total_ms*/, uint16_t /*count*/) {
    return 0;   /* stub -- Phase 4 */
}

class Segment {
public:
    Segment(uint16_t first, uint16_t count, Sequencer& seq);

    /* Strip holds an array of these, so they must be default-constructible.
     * A default-constructed segment covers no pixels and paints nothing. */
    constexpr Segment() = default;

    /* Both setters feed the *default* step 1 only, and each returns false if a
     * PlayheadFn is installed -- the value is rejected rather than stored
     * dormant. See the note on PlayheadFn above. */
    bool set_pixel_delay(int32_t ms);            /* milliseconds of lag per pixel */
    bool set_pixel_delay_q8(int32_t ms_q8);      /* 1/256 ms, which is how it is stored */

    /* Installing a PlayheadFn clears any pixel_delay, so a segment has either
     * one or the other and never a dormant leftover. nullptr restores the
     * default, with a delay of 0 until one is set again. */
    void set_playhead_fn(PlayheadFn fn);
    void set_color_fn(ColorFn fn);                /* nullptr restores the default */

    /* True once every pixel here has passed the end of a finite sequence --
     * a full stagger later than the sequencer's own playhead reaching the end.
     * Narrower than "nothing here will change again", too: a sequencer that is
     * not playing holds forever and is never finished. */
    bool finished(uint32_t now_ms) const;

private:
    uint16_t   first_ = 0;
    uint16_t   count_ = 0;
    Sequencer* sequencer_ = nullptr;
    int32_t    delay_q8_ = 0;
    PlayheadFn where_ = nullptr;
    ColorFn    what_  = nullptr;
};                                 /* 20 bytes on a 32-bit target */

}  // namespace rgbseq

#endif  /* RGBSEQ_SEGMENT_H_ */
