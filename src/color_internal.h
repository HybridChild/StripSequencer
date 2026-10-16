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
 * 16-bit and linear, and only then is it gamma-encoded down to 8 bits: scaling
 * after the encoding would make brightness non-linear and shift hue as the
 * channels crush toward zero at different rates. 255 leaves the color as it is,
 * 0 gives black. */
void to_bytes(Color c, uint8_t brightness, uint8_t* out);

}  // namespace stripseq

#endif  /* STRIPSEQ_COLOR_INTERNAL_H_ */
