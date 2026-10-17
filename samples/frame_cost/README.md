# frame_cost

How long `Strip::render()` takes over a 60-pixel strip, in CPU cycles, on the bench board. It runs six scenarios once at boot, prints a table to the console, and stops. No strip is driven: the buffer is written and never sent, because the transfer is the driver's cost, not the library's.

## Build, flash, read

```sh
SAMPLE=frame_cost scripts/sample-build.sh           # RGBW, into ./build-zephyr-frame_cost
SAMPLE=frame_cost scripts/sample-flash.sh
scripts/console.sh                                  # then reset the board to see it again
```

`--rgb` builds and flashes the RGB configuration, as for strip_demo. The board is the Nucleo-H753ZI, and nothing here depends on the strip's overlay, so any board with a DWT cycle counter will run it with `BOARD=`.

## How it measures

The cycle counter is the Cortex-M's DWT unit, read through Zephyr's timing API ([`CONFIG_TIMING_FUNCTIONS`](prj.conf)). It counts the core's own clock, so every flash wait state and cache miss is in the figure. Each scenario renders one fixed instant 101 times with interrupts locked around each call, and the cost of reading the counter is subtracted. The first call is reported on its own. Of the other hundred, the least and the most are reported.

The image is built at Zephyr's default `-Os`, with the instruction and data caches on, as an application on this board would be. That makes these figures a best case for the library: a 480 MHz Cortex-M7 with caches, running code it has just run. A core with no divide instruction, such as a Cortex-M0, calls a library routine for every division a pixel needs, and will take more.

| Scenario | What every pixel does |
| --- | --- |
| hold | Sits on a hold step: one step test, no blend. The floor. |
| fade | Blends partway through one linear fade, every pixel at the same point. |
| comet | A head running down the strip, with a 16 ms stagger: a few pixels fading, the rest at the end of the loop. |
| rainbow | Six linear fades spread across the whole strip, so every pixel is mid-fade, on every step. |
| longest | Every pixel in the last of `STRIPSEQ_MAX_STEPS` eased fades, so the step walk is as long as the build allows and the blend is the most expensive one. |
| overlay | The rainbow with the comet painted over it: two segments over every pixel, so every pixel is painted twice. |

The RGB build is not simply one channel cheaper than the RGBW build. It is faster where every pixel blends a single fade, by about the cost of the channel, but slower wherever the lookup walks several steps — by an eighth in longest, by less in rainbow and overlay — and slightly slower on a hold. `Sequence::at` walks the steps by index twice, once to sum the loop and once to find the current step. An RGBW step is 16 bytes, so each index is a shift folded into the load. An RGB step is 12 bytes, so each one costs a multiply that the load then waits for. With its step padded to 16 bytes, RGB is the faster build in every scenario, and its longest takes a fifth less time. Aligning the code to cache lines or dropping the gamma table moves both builds alike and leaves the gap where it is.
