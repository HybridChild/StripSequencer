/* The parts of Color that are not constexpr: the gamma table and the
 * conversion into the 8-bit buffer the hardware wants.
 *
 * Phase 0: empty. Everything color.h declares today is constexpr and therefore
 * lives in the header; Phase 1 fills this file with the 256-entry gamma table
 * and the internal to_bytes(Color, uint8_t brightness, uint8_t* out) that Strip
 * calls -- including the decision that brightness is applied *before* gamma, in
 * linear space.
 */

#include "rgbseq/color.h"
