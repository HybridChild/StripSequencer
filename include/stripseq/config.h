/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Build-time configuration: four knobs, all overridable from the build system.
 *
 * Every macro is #ifndef-guarded, which is what lets the Zephyr module in
 * zephyr/CMakeLists.txt translate Kconfig into -D definitions without a single
 * Zephyr-aware line anywhere in include/ or src/.
 *
 * Two of the four change *compiled behaviour* rather than a constant:
 * STRIPSEQ_WHITE_CHANNEL changes sizeof(Color), sizeof(Step) and
 * Strip::bytes_per_pixel; STRIPSEQ_GAMMA changes the output conversion and
 * whether a table is linked. No single compiled artifact represents both
 * settings, which is why the host build compiles the whole library once per
 * combination -- see cmake/stripseq_variant.cmake.
 *
 * Macros rather than template parameters. For the white channel that is
 * forced: it changes a struct's layout, which a template parameter cannot do
 * without splitting every downstream type as well. For the two capacities it is
 * a choice.
 *
 * Step capacity, because a step count is not always known at compile time:
 * sequences built from user input would still need a fixed-capacity fallback
 * beside a template family. A template would also make the fluent builder
 * change type on every call, stop Segment holding a plain Sequencer* (or cost a
 * vtable and an indirect call per pixel), and instantiate at() and the builder
 * once per N, where flash is usually the tighter budget. The price is a global
 * ceiling: one sequence needing sixteen steps sizes every sequence to sixteen,
 * which costs flash per constant sequence and RAM per sequence built at run
 * time, since a sequencer only refers to its sequence. If that ever becomes the
 * real problem, the cheaper escape is caller-supplied step storage, not
 * templates.
 *
 * Segment capacity has none of those objections -- nothing holds a Strip*, so
 * Strip<N> would infect nothing. It is a macro because an unused slot costs 20
 * bytes against a pixel buffer of hundreds, a system has one or two strips
 * rather than many sequencers, and two mechanisms for one job would need
 * explaining every time either was read. The price is per-strip sizing.
 */

#ifndef STRIPSEQ_CONFIG_H_
#define STRIPSEQ_CONFIG_H_

#ifndef STRIPSEQ_MAX_STEPS
#define STRIPSEQ_MAX_STEPS 8        /* steps a sequence can hold; sets sequence size */
#endif

#ifndef STRIPSEQ_MAX_SEGMENTS
#define STRIPSEQ_MAX_SEGMENTS 4     /* segments a strip can hold; sets strip size */
#endif

/* Both capacities are counted in uint8_t -- Sequence::count_, Strip::count_
 * and every segment index -- so past 255 a count wraps before it reaches the
 * capacity, and the builder or the strip goes on writing from the first slot.
 * Zero would be an array of nothing. zephyr/Kconfig states the same range;
 * this is the check for a build that defines the macros itself. */
#if STRIPSEQ_MAX_STEPS < 1 || STRIPSEQ_MAX_STEPS > 255
#error "STRIPSEQ_MAX_STEPS must be 1..255: a sequence counts its steps in a uint8_t"
#endif

#if STRIPSEQ_MAX_SEGMENTS < 1 || STRIPSEQ_MAX_SEGMENTS > 255
#error "STRIPSEQ_MAX_SEGMENTS must be 1..255: a strip counts its segments in a uint8_t"
#endif

#ifndef STRIPSEQ_WHITE_CHANNEL
#define STRIPSEQ_WHITE_CHANNEL 0    /* 1 adds the fourth channel everywhere */
#endif

#ifndef STRIPSEQ_GAMMA
#define STRIPSEQ_GAMMA 1            /* 1 links the correction table (~256 bytes) */
#endif

#endif  /* STRIPSEQ_CONFIG_H_ */
