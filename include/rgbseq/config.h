/* Build-time configuration: four knobs, all overridable from the build system.
 *
 * Every macro is #ifndef-guarded, which is what lets the Zephyr module in
 * zephyr/CMakeLists.txt translate Kconfig into -D definitions without a single
 * Zephyr-aware line anywhere in include/ or src/.
 *
 * Two of the four change *compiled behaviour* rather than a constant:
 * RGBSEQ_WHITE_CHANNEL changes sizeof(Color), sizeof(Step) and
 * Strip::bytes_per_pixel; RGBSEQ_GAMMA changes the output conversion and
 * whether a table is linked. No single compiled artifact represents both
 * settings, which is why the host build compiles the whole library once per
 * combination -- see cmake/rgbseq_variant.cmake.
 *
 * Why macros rather than template parameters is argued in docs/API-SKETCH.md,
 * "Build-time configuration".
 */

#ifndef RGBSEQ_CONFIG_H_
#define RGBSEQ_CONFIG_H_

#ifndef RGBSEQ_MAX_STEPS
#define RGBSEQ_MAX_STEPS 8        /* steps a sequence can hold; sets sequencer size */
#endif

#ifndef RGBSEQ_MAX_SEGMENTS
#define RGBSEQ_MAX_SEGMENTS 4     /* segments a strip can hold; sets strip size */
#endif

#ifndef RGBSEQ_WHITE_CHANNEL
#define RGBSEQ_WHITE_CHANNEL 0    /* 1 adds the fourth channel everywhere */
#endif

#ifndef RGBSEQ_GAMMA
#define RGBSEQ_GAMMA 1            /* 1 links the correction table (~256 bytes) */
#endif

#endif  /* RGBSEQ_CONFIG_H_ */
