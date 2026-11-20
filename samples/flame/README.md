# flame

A whole fire on the strip, drawn by a replaced step 2 from each pixel's height and the time alone, where the usual LED fire keeps a heat per pixel and diffuses it upward. That array is exactly the per-pixel state the library refuses, and this fire does without it.

## Build, flash, watch

```sh
scripts/sample-build.sh flame                  # RGBW, into ./build-zephyr-flame-nucleo_h753zi
scripts/sample-flash.sh flame
```

`--rgb` builds and flashes the RGB configuration, as for strip_demo. The board is the bench's unless `-b` names another, as in `scripts/sample-build.sh flame -b nucleo_u083rc`, and each board's overlay is strip_demo's, included rather than copied, so the hardware is described in one place. Any other board needs what strip_demo's [On other hardware](../strip_demo/README.md#on-other-hardware) lists.

## Where the code lives

[`src/main.cpp`](src/main.cpp) is the Zephyr half: the device and strip_demo's frame loop, with one segment over the whole strip on the library's `timebase`. The fire is in [`src/flame.cpp`](src/flame.cpp), which includes nothing but the library.

## The fire

Pixel 0 is the base, and every height and speed is a fraction of the strip, so the fire fills any length. Smoothed value noise sets the flame's height, from a slow draught to a fast flicker; more noise rises through the body faster than the flame grows, so bright knots run upward and tongues tear off the top; embers leave the tip at hashed moments and cool as they climb, drawn between pixels rather than jumping; and a blackbody palette turns heat into dull red, orange and yellow, on RGBW up into the white die.

The user button steps through four palettes: the black body's, then blue, green and violet, each climbing from a dim glow to a pale tip at the same heats. Only the segment's color function changes, so the fire keeps its shape across the switch, and the console names each one as it starts. The button is the board's `sw0` alias, which most development boards already define.

It runs at a brightness of 120 of 255, where the other samples run at 74: the fire's dull reds round to black that dim, and its average pixel is dim enough that it still draws little.
