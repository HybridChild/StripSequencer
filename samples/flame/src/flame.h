/*
 * Copyright (c) 2026 Esben Dueholm Nørgaard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The flame: a whole fire drawn by a step 2, with no heat array. It needs only
 * the library, so nothing here or in flame.cpp knows it runs under Zephyr. */

#ifndef FLAME_FLAME_H_
#define FLAME_FLAME_H_

#include <stripseq/stripseq.h>

/* One fire, in one palette. Each draw is a ColorFn, for a segment on the
 * timebase with no stagger: it reads the playhead as a time that is never
 * negative. Pixel 0 is the base, and the fire scales to whatever count it is
 * given. Every fire has the same shape; only the colors differ. Its colors
 * look as bright as the black body's only at the strip brightness it names,
 * which the application sets along with the draw. */
struct Flame {
    const char*       name;
    stripseq::ColorFn draw;
    uint8_t           brightness;
};

inline constexpr size_t kFlameCount = 6;
extern const Flame flames[kFlameCount];   /* the black body's first */

/* Not fires: steady blocks of two primaries side by side, for balancing the
 * table every fire's brightness is matched through. A Flame because the
 * application installs them the same way. */
inline constexpr size_t kSwatchCount = 2;
extern const Flame swatches[kSwatchCount];

#endif  /* FLAME_FLAME_H_ */
