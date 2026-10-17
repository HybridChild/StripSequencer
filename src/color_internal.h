/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The output conversion, shared by the library sources and the tests and seen
 * by nobody else.
 *
 * It lives under src/ rather than include/stripseq/ so it never ships: a
 * consumer's include path reaches include/ only, while the library's sources
 * find this file beside them and the test targets add src/ explicitly.
 */

#ifndef STRIPSEQ_COLOR_INTERNAL_H_
#define STRIPSEQ_COLOR_INTERNAL_H_

#include <cstdint>

#include "stripseq/color.h"

namespace stripseq {

/* Writes c as 3 bytes, or 4 in an RGBW build, in r, g, b, w order -- the
 * driver owns the wire order. Brightness scales each channel while it is still
 * 16-bit, in the space colors are written in, and only then does gamma map it
 * to the 8-bit emitted level. That makes the knob even to the eye: 128 looks
 * half as bright. The price is at the dim end, which needs levels below one
 * byte: channels round to 0 at different knob settings, so hue drifts, and
 * below 22 even full white is black. notes/gamma-and-color-guide.md works
 * through the trade. 255 leaves the color as it is, 0 gives black. */
void to_bytes(Color c, uint8_t brightness, uint8_t* out);

}  // namespace stripseq

#endif  /* STRIPSEQ_COLOR_INTERNAL_H_ */
