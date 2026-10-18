/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The twinkle: white sparks that never repeat, drawn by a step 2 from each
 * pixel's playhead alone. It needs only the library, so nothing here or in
 * twinkle.cpp knows it runs under Zephyr. */

#ifndef SHOWCASE_TWINKLE_H_
#define SHOWCASE_TWINKLE_H_

#include <stripseq/stripseq.h>

/* A ColorFn, for a segment on the timebase with no stagger: it reads the
 * playhead as a time that is never negative. */
stripseq::Sample twinkle(int32_t playhead_ms, uint16_t index, uint16_t count);

#endif  /* SHOWCASE_TWINKLE_H_ */
