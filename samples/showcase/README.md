# showcase

What the library can make a strip do, one animation per press of the user button. Where [strip_demo](../strip_demo/README.md) checks the hardware, this one only shows off: each animation is there because it looks like something and demonstrates one idea from the library, and none has a failure signature to read.

## Build, flash, watch

```sh
SAMPLE=showcase scripts/sample-build.sh        # RGBW, into ./build-zephyr-showcase
SAMPLE=showcase scripts/sample-flash.sh
scripts/console.sh                             # names each animation as it starts
```

`--rgb` builds and flashes the RGB configuration, as for strip_demo. The board is the bench's, and its overlay is strip_demo's, included rather than copied, so the hardware is described in one place. Any other board needs what strip_demo's [On other hardware](../strip_demo/README.md#on-other-hardware) lists.

## The animations

The button advances, and the last wraps round to the first. Split animations put the extra pixel of an odd strip in the right half.

| Animation | What it shows |
| --- | --- |
| Blink | The whole strip on one sequence: red and black, 500 ms each. |
| Breathe | Eased fades in place of holds, white in and out. |
| Comet | A stagger: the same sequence on every pixel, each 16 ms later than the one before, so a white head runs down the strip trailing a fade. |
| Rainbow | Six fades around the hue wheel, spread so the whole wheel fits the strip, rotating. |
| Static gradient | The rainbow seeked and never played: a frozen picture that costs one frame. |
| Wipe | A sequence that runs once and stays on its landing color. Every three seconds it turns around, green sweeping in and then black sweeping it out. |
| Progress bar | A sequencer that never plays, whose playhead is the value. It fills over three seconds, rests full for two, and starts again, with the edge pixel fading in rather than stepping. |
| Two halves | Two sequencers on two segments: breathing on the left, the rainbow on the right. |
| Converging | One comet on both halves with opposite delays, so both flow toward the centre. Pixels the same distance from the centre are a whole half's stagger apart, so it is not a reflection. |
| Mirrored | The same comet, with the right half's step 1 replaced by one that counts from the far end. Now it is a reflection. |
| Twinkle | Step 2 replaced by a function of the pixel's playhead. Time is cut into 400 ms slots, and in each a pixel sparks once at a hashed moment, or a quarter of the time not at all, so each flashes about twice a second, more than half the strip is lit at once, and the picture never repeats. Nothing is stored: the hash of pixel and slot is the whole state. A replaced step 1 cannot do this, because every pixel would replay one loop and the strip would repeat once a loop. |
| Flame | A whole fire drawn by a replaced step 2, from each pixel's height and the time alone, where the usual LED fire keeps a heat per pixel and diffuses it upward. Smoothed value noise sets the flame's height, from a slow draught to a fast flicker; more noise rises through the body faster than the flame grows, so bright knots run upward and tongues tear off the top; embers leave the tip at hashed moments and cool as they climb, drawn between pixels rather than jumping; and a blackbody palette turns heat into dull red, orange and yellow, on RGBW up into the white die. Pixel 0 is the base. |
| Alert | Five red flashes every seven seconds on the 3rd to the 6th pixel, painted over a slow blue breathe that never goes fully dark. The flashes are a second, shorter segment added on top, and `drop_finished()` takes it away once they are done. |

The wipe, the progress bar and the alert change on a schedule of their own rather than the sequencer's, so the sample adds a tick to strip_demo's frame loop: a second work item that changes the animation and then renders at once, since the last frame may have reported `hold_forever` and stopped the loop.

It runs at a brightness of 74 of 255, for strip_demo's reason: eleven RGBW pixels at full white draw more than a USB-powered bench should be asked for. Brightness is applied before gamma, so 74 is about 5% of the light, and leaves a channel 12 output levels from dark to full: enough for the fades to read as smooth, where 64 leaves eight and a slow fade climbs them as a visible staircase. The flame runs at 120: its dull reds round to black this dim, and its average pixel is dim enough that it still draws little.
