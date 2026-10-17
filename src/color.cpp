/* The parts of Color that are not constexpr: the gamma table and the
 * conversion into the 8-bit buffer the hardware wants.
 */

#include "color_internal.h"

namespace stripseq {

namespace {

#if STRIPSEQ_GAMMA

/* Gamma 2.5: out = round(255 * (in / 255)^2.5), for each of the 256 values the
 * top byte of a channel can take.
 *
 * 2.5 because it is exact in integers -- x^2.5 is sqrt(x^5) -- so the table
 * is generated here rather than pasted in, with no floating point even at
 * compile time. Squaring away the root, out is the largest n for which
 * n - 1/2 <= sqrt(in^5 / 255^3), that is (2n - 1)^2 * 255^3 <= 4 * in^5. Both
 * sides reach about 2^42, hence uint64_t, and only while compiling.
 *
 * consteval rather than constexpr: the generator must never run on the target,
 * and consteval turns any call that would run it there into a compile error. */
struct GammaTable {
    uint8_t entry[256];
};

consteval GammaTable make_gamma_table() {
    GammaTable table{};
    constexpr uint64_t cube = uint64_t{255} * 255 * 255;
    for (uint64_t in = 0; in < 256; ++in) {
        const uint64_t limit = 4 * in * in * in * in * in;
        uint64_t n = 0;
        while (n < 255 && (2 * n + 1) * (2 * n + 1) * cube <= limit) {
            ++n;
        }
        table.entry[in] = static_cast<uint8_t>(n);
    }
    return table;
}

/* constexpr at namespace scope is constant-initialised, so the table is in
 * flash with no startup code. */
constexpr GammaTable gamma = make_gamma_table();

static_assert(gamma.entry[0] == 0 && gamma.entry[255] == 255,
              "Gamma must keep black black and full scale full.");

#endif

/* The emitted level for a top byte, gamma-encoded when the build asks for
 * it. */
uint8_t encode(uint8_t top) {
#if STRIPSEQ_GAMMA
    return gamma.entry[top];
#else
    return top;
#endif
}

/* The top byte of channel * brightness / 255, as a multiply and a shift:
 * scaling by brightness + 1 makes the divisor 2^16, which drops the eight bits
 * that kept blending accurate along with the division. The wire carries eight,
 * so they are spent by design. 255 maps every channel to its own top byte, 0
 * to black, and in between it is never more than one level from the division.
 * The product stays under 2^24. */
uint8_t convert(uint16_t channel, uint8_t brightness) {
    return encode(static_cast<uint8_t>((uint32_t{channel} * (brightness + 1u)) >> 16));
}

}  // namespace

void to_bytes(Color c, uint8_t brightness, uint8_t* out) {
    out[0] = convert(c.r, brightness);
    out[1] = convert(c.g, brightness);
    out[2] = convert(c.b, brightness);
#if STRIPSEQ_WHITE_CHANNEL
    out[3] = convert(c.w, brightness);
#endif
}

}  // namespace stripseq
