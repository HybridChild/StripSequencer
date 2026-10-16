/* Segment: the only part of the design that knows pixels exist.
 *
 * A run of pixels, the sequencer driving them, and the pixel_delay that
 * staggers them -- plus the two optional replacements for the render loop's two
 * steps. There is no per-pixel state here and there must never be: that
 * invariant is the whole argument of docs/DESIGN.md.
 */

#ifndef STRIPSEQ_SEGMENT_H_
#define STRIPSEQ_SEGMENT_H_

#include <cstdint>

#include "stripseq/sequence.h"
#include "stripseq/sequencer.h"

namespace stripseq {

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
 * a linear ramp already in place. A replacement gets no context pointer, so one
 * that wants a configurable stagger must bake the constant in (see
 * docs/DESIGN.md, Deferred).
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
 * delay -- once computed there is nothing about it left to remember.
 *
 * Rounded to the nearest 1/256 ms, so the accumulated error across a ring
 * stays under 1 ms up to 511 pixels; a floor reaches it at 257. A count
 * of 0 gives 0, and a delay too large for int32_t saturates. */
constexpr int32_t spread(uint32_t /*total_ms*/, uint16_t /*count*/) {
    return 0;   /* stub */
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

    /* How a caller finds a segment again after the strip's list has shifted.
     * The sequencer is the identity because the application already holds it
     * by name, so a segment needs no ID field of its own. Several segments may
     * share one sequencer, so this is a predicate to sweep with rather than a
     * lookup that returns one index. */
    bool drives(const Sequencer& seq) const;

private:
    /* Strip::render() is the only caller. The pixel loop lives here because a
     * segment is what knows its pixels: it asks its sequencer for the playhead
     * once, runs steps 1 and 2 for each pixel, converts through the strip's
     * brightness and gamma, and writes 8-bit bytes straight into the buffer at
     * its own offset. Returns the smallest hold among its pixels.
     *
     * Private, because painting one segment on its own skips the clear and the
     * paint order, and nothing outside the strip has a reason to.
     *
     * Clips to pixel_count: pixels past the end of the strip are neither
     * painted nor counted towards the hold, so a segment overhanging the end
     * paints its visible part and one wholly off the strip returns
     * hold_forever. add() already rejects such a segment; the clip is for
     * `strip.segment(i) = Segment(...)`, which never passes through the strip
     * and so cannot be checked, and would otherwise overrun the buffer. */
    friend class Strip;
    uint32_t paint(uint8_t* buffer, uint16_t pixel_count, uint8_t brightness,
                   uint32_t now_ms) const;

    uint16_t   first_ = 0;
    uint16_t   count_ = 0;
    Sequencer* sequencer_ = nullptr;
    int32_t    delay_q8_ = 0;
    PlayheadFn where_ = nullptr;
    ColorFn    what_  = nullptr;
};                                 /* 20 bytes on a 32-bit target */

}  // namespace stripseq

#endif  /* STRIPSEQ_SEGMENT_H_ */
