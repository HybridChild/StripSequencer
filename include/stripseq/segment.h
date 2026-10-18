/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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

/* Step 1 of the render loop: how far behind the segment's playhead does this
 * pixel look?
 *
 *   index     the pixel's position *within the segment*, 0-based -- not its
 *             position on the strip. A segment therefore behaves identically
 *             wherever it sits, and hash(index) stays stable if it moves.
 *   count     how many pixels the segment covers. The only way a replacement
 *             can normalise position: mirroring is (count - 1 - index).
 *   delay_q8  the segment's pixel_delay, in 1/256 ms. The default reads it as
 *             lag per pixel, (index * delay_q8) >> 8; a replacement reads it
 *             however it documents -- Twinkle takes it as the span its offsets
 *             scatter across.
 *   returns   the offset in ms, positive for behind. The segment subtracts it
 *             from the sequencer's playhead to get this pixel's.
 *
 * An offset rather than a playhead, so that the segment does the subtraction
 * on every path. A replacement's offset is clamped to the same 2^23 ms either
 * way that the setters' guard gives the default, and the sequencer's playhead
 * is already narrowed, so no pixel's playhead can leave int32_t whatever is
 * installed (see Sequencer::playhead). The price is that step 1 cannot warp
 * time per pixel: reversing or rescaling a pixel's playhead is beyond it, and
 * a ColorFn, which receives that playhead, may do what it likes with it.
 *
 * pixel_delay is a replacement's one parameter, which is what lets a Twinkle
 * span or a mirror's delay be set at run time. Anything richer -- a hash
 * seeded per device, say -- needs the context pointer docs/DESIGN.md defers. */
using OffsetFn = int32_t (*)(uint16_t index, uint16_t count, int32_t delay_q8);

/* Step 2: what color is at that playhead?
 *
 *   playhead_ms  *this pixel's* playhead: the sequencer's, less the offset
 *                step 1 returned.
 *   index        as above: position within the segment, 0-based.
 *   count        as above: pixels in the segment. Present so that a color may
 *                depend on position directly, not only through time.
 *   returns      a Sample. See Sample for the contract; reporting the hold
 *                honestly is what lets a segment driven by your function sleep
 *                exactly as long as one driven by a step list. */
using ColorFn = Sample (*)(int32_t playhead_ms, uint16_t index, uint16_t count);

/* A pseudo-random number for a replacement that wants one: a hash of the
 * pixel's index for an offset, or of the index and a time slot for a spark.
 * Neither step may keep state, so a hash of what it is given is the only
 * randomness either has, and the same arguments always draw the same picture.
 *
 * Wellons' lowbias32: every input bit reaches every output bit, so any bits
 * of the result are as good as any others and `hash(i) % span` scatters
 * evenly. Never reduce a multiplicative hash like i * 2654435761 that way: the
 * remainder is near-linear in i, so neighbouring pixels land one of two fixed
 * distances apart and fire as a chase. A bijection on uint32_t, so distinct inputs
 * never collide; hash(0) is 0. */
constexpr uint32_t hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/* Fit intent: the delay that spreads total_ms evenly across count pixels.
 * A free function rather than a Segment method, because the result is just a
 * delay -- once computed there is nothing about it left to remember.
 *
 * Rounded to the nearest 1/256 ms, so the accumulated error across a ring
 * stays under 1 ms up to 511 pixels; a floor reaches it at 257. A count
 * of 0 gives 0, and a delay too large for int32_t saturates. */
constexpr int32_t spread(uint32_t total_ms, uint16_t count) {
    if (count == 0)
        return 0;

    /* The whole milliseconds and the remainder's share of 256, rounded to
     * nearest, so total_ms * 256 is never formed: in uint32_t it would wrap
     * past 4.6 hours, and in uint64_t it is a library call on a Cortex-M.
     * rest < count, so rest * 256 stays under 2^24. */
    const uint32_t whole = total_ms / count;
    const uint32_t rest = total_ms % count;
    const uint32_t fraction = (rest * 256 + count / 2) / count;   /* 0..256 */

    if (whole > (uint32_t{INT32_MAX} - fraction) / 256)
        return INT32_MAX;
    return static_cast<int32_t>(whole * 256 + fraction);
}

class Segment {
public:
    Segment(uint16_t first, uint16_t count, Sequencer& seq);

    /* Strip holds an array of these, so they must be default-constructible.
     * A default-constructed segment covers no pixels and paints nothing. */
    constexpr Segment() = default;

    /* pixel_delay is step 1's parameter: lag per pixel to the default, and
     * whatever an installed OffsetFn documents. Each setter returns false for
     * a delay whose product with count - 1 would overflow the default's
     * stagger, checked whatever is installed, so restoring the default never
     * inherits a delay it cannot use. Not [[nodiscard]], for the reason
     * Strip::add() gives. */
    bool set_pixel_delay(int32_t ms);            /* milliseconds */
    bool set_pixel_delay_q8(int32_t ms_q8);      /* 1/256 ms, which is how it is stored */

    /* Leaves pixel_delay as it is, since every step 1 reads it. nullptr
     * restores the default. */
    void set_offset_fn(OffsetFn fn);
    void set_color_fn(ColorFn fn);                /* nullptr restores the default */

    /* True when the sequencer is playing a finite sequence and no pixel here
     * will change again: every pixel's lookup, or its ColorFn, reports
     * hold_forever. The hold is a binding contract, so this holds for any
     * OffsetFn and any ColorFn; on the default steps it comes a full stagger
     * after the sequencer's own playhead reaches the end.
     *
     * Narrower than "nothing here will change again" by the two conditions. A
     * sequencer that is not playing holds forever and is never finished, and
     * nor is one playing an endless sequence, so a static gradient drawn by a
     * ColorFn is never removed: a finite sequence is how a ColorFn says it is
     * meant to end. Pixels clipped off the strip still count.
     *
     * The scan stops at the first pixel still changing, so it runs in full only
     * on a segment that has finished. */
    bool finished(int64_t now_ms) const;
    template <wrapping_tick T> bool finished(T) const = delete;

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
                   int64_t now_ms) const;

    /* Steps 1 and 2 for one pixel, with the hold the lookup or ColorFn
     * reported. paint and finished() both go through it, so what a segment
     * paints and whether it has finished cannot disagree. When the sequencer
     * is not playing, paint then reports hold_forever whatever the pixels
     * said, which is why finished() asks here rather than paint. */
    Sample sample(int32_t playhead_ms, uint16_t index) const;

    uint16_t   first_ = 0;
    uint16_t   count_ = 0;
    Sequencer* sequencer_ = nullptr;
    int32_t    delay_q8_ = 0;
    OffsetFn   where_ = nullptr;
    ColorFn    what_  = nullptr;
};                                 /* 20 bytes on a 32-bit target */

}  // namespace stripseq

#endif  /* STRIPSEQ_SEGMENT_H_ */
