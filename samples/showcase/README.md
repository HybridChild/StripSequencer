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
| Progress bar | A sequencer that never plays, whose playhead is the value. It fills over two seconds, rests full for one, and starts again, with the edge pixel fading in rather than stepping. |
| Two halves | Two sequencers on two segments: breathing on the left, the rainbow on the right. |
| Converging | One comet on both halves with opposite delays, so both flow toward the centre. Pixels the same distance from the centre are a whole half's stagger apart, so it is not a reflection. |
| Mirrored | The same comet, with the right half's step 1 replaced by one that counts from the far end. Now it is a reflection. |
| Twinkle | Step 1 replaced by a hash, so every pixel flashes at its own moment of a one-second loop: a quick spark, a half-second fade, and dark for the rest, so about half the strip is lit at once. |
| Alert | Three red flashes every seven seconds, painted over a slow blue breathe that never goes fully dark. The flashes are a second segment added on top, and `drop_finished()` takes it away once they are done. |

The wipe, the progress bar and the alert change on a schedule of their own rather than the sequencer's, so the sample adds a tick to strip_demo's frame loop: a second work item that changes the animation and then renders at once, since the last frame may have reported `hold_forever` and stopped the loop.

It runs at a brightness of 64 of 255, for strip_demo's reason: eleven RGBW pixels at full white draw more than a USB-powered bench should be asked for. Brightness is applied before gamma, so a quarter of the scale is about 3% of the light, and a slow fade that dim steps visibly: the alert's blue breathe would cross seven output levels, each held for about a fifth of a second. The alert therefore runs at 128, about a fifth of full light, where the same fade crosses 36.
