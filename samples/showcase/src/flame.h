/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The flame: a whole fire drawn by a step 2, with no heat array. It needs only
 * the library, so nothing here or in flame.cpp knows it runs under Zephyr. */

#ifndef SHOWCASE_FLAME_H_
#define SHOWCASE_FLAME_H_

#include <stripseq/stripseq.h>

/* A ColorFn, for a segment on the timebase with no stagger: it reads the
 * playhead as a time that is never negative. Pixel 0 is the base, and the
 * fire scales to whatever count it is given. */
stripseq::Sample flame(int32_t playhead_ms, uint16_t index, uint16_t count);

#endif  /* SHOWCASE_FLAME_H_ */
