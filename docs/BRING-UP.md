# StripSequencer — bench bring-up

How the library meets the bench hardware, and in what order to look. The sample in [`samples/strip_demo/`](../samples/strip_demo/) is the adapter from [API-SKETCH.md](API-SKETCH.md#the-zephyr-adapter) made real; this note is what to do with it.

## The bench

A Nucleo-H753ZI driving 11 ILPL-K501 pixels — SK6812-class RGBW, green-red-blue-white on the wire — from I2S3, data out on PD6, through `worldsemi,ws2812-i2s`. Everything board-specific is in [`boards/nucleo_h753zi.overlay`](../samples/strip_demo/boards/nucleo_h753zi.overlay), and each value there carries its reason: the nibble timing, the DMA controllers the I2S binding needs and the board leaves off, and the `color-mapping` the RGBW path reads to build its permute.

## Build, flash, watch

```sh
scripts/sample-build.sh        # RGBW, the bench configuration
scripts/sample-flash.sh
scripts/console.sh             # 115200 8N1 on the ST-LINK port
```

`scripts/sample-build.sh --rgb` builds the same sample with `CONFIG_STRIPSEQ_WHITE_CHANNEL=n`, which takes the other adapter path: `led_strip_update_rgb()` on the buffer as it stands instead of a permute and `led_strip_update_channels()`. On this strip it runs with the white die dark, which is the driver's documented behaviour rather than a fault. CI builds both.

## The stages

The user button (B1) advances. Each stage names itself on the console when it starts, and prints how many frames it rendered, and over how long, when it ends. They run cheapest signal first, so a failure is localised by the first stage that shows it.

| Stage | What it proves | What a failure looks like |
| --- | --- | --- |
| Solid red | Wiring, power, timing, and that the permute puts red where the chip expects it | Nothing lit: wiring, DMA or the driver failed to start — check the console for `not ready`. Green instead of red: `color-mapping` is RGB-first where the chip is GRB, or the reverse. Random colors: the nibble timing. |
| White die only | That the white channel reaches the hardware at all | Dark strip on an RGBW build. Nothing else distinguishes a working `update_channels()` path from `update_rgb()`'s silent zero, which is why this stage exists. Absent from an `--rgb` build. |
| Blink | The frame loop and the hold path: red and black, 500 ms each | Flicker or a stuck color: the reschedule is not honouring the hold. Much more than one frame per 500 ms, plus the first, says the same. |
| Comet | The stagger and its sign: a white head running from pixel 0 to the far end, trailing a fade | Running the wrong way: the sign of `pixel_delay`. All pixels together: the delay never reached the segment. Some pixel is always mid-fade, so the hold is always 0: about one frame per 17 ms, the 16 ms frame period plus the 0.83 ms the driver sleeps while each transfer drains. |
| Rainbow | HSV, and the seam on a ring | A visible step between the last pixel and the first, or a hue jump anywhere else. The same rate as the comet, for the same reason. |
| Static gradient | That a frozen picture costs nothing | Leaving it should print `1 frames`. More means the hold fold returned 0 where it should have returned `hold_forever` and the workqueue is spinning. |

Solid red and the white die are seeked rather than played, so they are static too and also print `1 frames` when left. The stages wrap, so pressing again after the gradient starts over at red.

The sample runs at a quarter of full brightness. Bring-up proves wiring, timing and code paths, not light output, and eleven RGBW pixels at full white draw more than a USB-powered bench should be asked for.
