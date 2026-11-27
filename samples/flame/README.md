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

The user button steps through six palettes, one for every ordered pair of primaries. Each fire's cold primary is the whole of its dull glow and stands alone at the ragged top, and its hot one mixes in toward the base: the black body is red with green, so yellow at the base, and the others are red with blue, green with red, green with blue, blue with red and blue with green, all turning color at the same heats. Only the segment's color function changes, so the fire keeps its shape across the switch, and the console names each one as it starts. The button is the board's `sw0` alias, which most development boards already define.

The black body runs at a brightness of 120 of 255, where the other samples run at 74: the fire's dull reds round to black that dim, and its average pixel is dim enough that it still draws little. The other fires are rebalanced to look as bright as it does at every heat, because green looks far brighter than red at the same level and blue dimmer. Each stop's hue comes from the black body's, with whatever plays each part given the level that looks the way the black body's red, green or blue does in it, and a mix that scales the hot part on top, because how strongly one primary reads mixed into another depends on the pair. The whole color is then scaled until it looks as bright as the black body's at that heat, so the mix moves the hue and never the brightness. A fire that would need more than full scale raises the strip's brightness instead. All of it is worked out while compiling, from one table in `flame.cpp`, the level of each primary that looks as bright as full red, and from the mixes in the list of fires beside it. Both are set by eye. After the six fires the button steps through two steady swatches, red on the base half beside green or blue at its table level on the top half, the halves the same size and an odd last pixel dark, and the table is right when the halves look equally bright.
