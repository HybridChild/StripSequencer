# Always RGBW

A proposal to remove `STRIPSEQ_WHITE_CHANNEL` and always compile the library with four channels. A `Color` always carries red, green, blue and white, and an application that drives an RGB strip leaves white at zero, as it does today in an RGBW build. Below: the measurement that prompted it, its cause, what the change gains and costs, and the one question to settle before implementing it.

## The finding

`samples/frame_cost` times `Strip::render()` over 60 pixels on the bench board, a Cortex-M7 at 480 MHz with caches on, built at `-Os`. The RGB build does three channels of arithmetic per pixel where the RGBW build does four, so it should be the faster of the two everywhere. It is not:

| Scenario | RGB | RGBW | RGB against RGBW |
| --- | --: | --: | --: |
| hold | 9,354 | 9,115 | +2.6% |
| fade | 13,000 | 13,594 | −4.4% |
| comet | 13,671 | 14,062 | −2.8% |
| rainbow | 16,858 | 15,821 | +6.6% |
| longest | 21,036 | 18,703 | +12.5% |
| overlay | 29,843 | 28,937 | +3.1% |

Cycles, least of 100 warm runs. Repeated builds reproduce every figure to the cycle.

RGB wins only where every pixel blends a single fade. It loses wherever the lookup walks several steps — rainbow, longest, overlay — and, slightly, on a hold, where there is no blend for the missing channel to save.

## The cause

`Sequence::at()` indexes `steps_[i]` in two loops per pixel: `loop_duration_ms()` sums every step, and the walk then finds the step containing the playhead. An RGBW `Step` is 16 bytes, so the index becomes a shift folded into the load, and GCC turns the sum into a pointer loop. An RGB `Step` is 12 bytes, so every iteration multiplies before it can load. The sum, as compiled for the bench:

```
RGB, 12-byte step                    RGBW, 16-byte step
1:  mul.w  r4, r0, r1    @ i * 12    1:  ldr   r4, [r1, #12]
    adds   r1, #1                        adds  r1, #16
    ldr    r4, [r7, r4]                  cmp   r0, r1
    add    r6, r4                        add   ip, r4
    uxtb   r4, r1                        bne   1b
    cmp    r2, r4
    bhi    1b
```

The walk has the same `mul.w` feeding its load. In longest each pixel runs 16 of these iterations, and the gap is about 39 cycles a pixel.

Three experiments separate the stride from everything else that differs between the two images:

| Scenario | RGB, 32-byte aligned | RGBW, 32-byte aligned | RGB, no gamma | RGBW, no gamma | RGB, step padded to 16 B |
| --- | --: | --: | --: | --: | --: |
| hold | 9,698 | 9,603 | 8,481 | 8,230 | 8,314 |
| fade | 13,624 | 14,286 | 12,138 | 12,668 | 11,905 |
| comet | 14,356 | 14,800 | 12,800 | 13,186 | 12,494 |
| rainbow | 17,607 | 16,637 | 15,982 | 14,881 | 14,118 |
| longest | 21,928 | 19,710 | 20,163 | 17,844 | 17,034 |
| overlay | 31,234 | 30,503 | 28,052 | 27,064 | 25,925 |

- **Aligned:** `-falign-functions=32 -falign-loops=32 -falign-jumps=32 -falign-labels=32` through `EXTRA_CXXFLAGS`. Both builds move together and the gap stays, so it is not where the code falls relative to cache lines.
- **No gamma:** `CONFIG_STRIPSEQ_GAMMA=n`. Both builds move together and the gap stays, so it is not where the table falls.
- **Padded:** an RGB build with a `uint32_t` appended to `Step`, making it 16 bytes. RGB becomes the faster build in every scenario, as the arithmetic predicts, and its longest drops by a fifth.

## The proposal

Remove the option. `Color` always has `r`, `g`, `b` and `w`, a `Step` is always 16 bytes, and `Color::rgbw()` is always available. Every color an application writes with `Color::rgb()` or the named constants already leaves white at zero, so no sequence changes meaning. Under Zephyr, `CONFIG_STRIPSEQ_WHITE_CHANNEL` goes too.

### What it gains

- **One `Color`, one `Step`, one `Sequence` layout.** The mismatch the README warns about — an application compiled with a different `STRIPSEQ_WHITE_CHANNEL` from the library, linking cleanly against a `Color` of the wrong size — can no longer happen, and the `PUBLIC`-definitions rule exists for the remaining options only.
- **No load-bearing field order.** `Step`'s layout is 16 bytes in any order, so the comment and `static_assert` that guard against padding in RGB go away.
- **A smaller matrix.** The host variants drop from five to three (`default`, `nogamma`, `tight`), the footprint tool from four configurations to two, and the `--rgb` builds of both samples, with their CI steps, go away.
- **Fewer conditionals.** The `#if STRIPSEQ_WHITE_CHANNEL` blocks in `color.h`, `color.cpp`, `strip.h`, the tests' support code and the strip_demo adapter collapse to one path.
- **RGB strips are faster on multi-step sequences.** They get the RGBW column above: about 11% less time in longest, 6% in rainbow, 3% in overlay and on a hold.
- **One build drives both kinds of strip,** which DESIGN.md today describes as costing memory: "an RGB strip in an RGBW build simply leaves white at zero".

### What it costs

- **Memory, for applications on RGB strips.** `Color` grows from 6 to 8 bytes, `Step` from 12 to 16, and every `Sequence` from 112 to 148 bytes at the default eight steps. `Sequencer`, `Segment` and `Strip` do not change. Flash grows by 32 B on a Cortex-M0 and 12 B on a Cortex-M4, the README's RGBW rows against its RGB rows: the one empty `Sequence` the library keeps grows by 36 B, and the code moves by a few bytes either way.
- **Time, for RGB strips on single fades.** The blend does a fourth channel nobody sees: about 4.6% more in fade and 2.9% in comet.
- **Documentation.** The guides in `notes/` use the white channel as their running example of a build variant, and need another; `STRIPSEQ_GAMMA` is the candidate.

## To decide before implementing

**How many bytes per pixel `render()` writes.** With colors always four channels, the arithmetic is settled; what reaches the user's buffer is not.

1. **Always four.** One layout everywhere and nothing to choose: `Strip::bytes_per_pixel` stays a constant, now `4`. An application on an RGB strip allocates a third more buffer and repacks it to three bytes a pixel before sending — per-pixel work in its driver, and a second buffer unless done in place.
2. **Three or four, per strip.** `Strip` takes a pixel format and writes R, G, B or R, G, B, W, dropping white on a three-byte strip. An RGB buffer still goes straight to a WS2812 driver, and under Zephyr straight to `led_strip_update_rgb()`. It costs a format member in `Strip` and a choice per pixel in the conversion, and raises a second question: whether `Strip(buffer)` without a format means RGB, so that Blink and every recipe compile unchanged at the price of an RGBW strip silently showing three channels when the format is forgotten, or whether the format is always spelled out.

Dropping white on a three-byte strip, rather than folding it into red, green and blue, keeps DESIGN.md's rule that the library never derives one channel from the others.

## What the change touches

- `include/stripseq/config.h`, `color.h`, `sequence.h`, `strip.h`; `src/color.cpp`, `src/color_internal.h`.
- `CMakeLists.txt`'s variant list and `cmake/stripseq_variant.cmake`; `zephyr/Kconfig` and `zephyr/CMakeLists.txt`.
- `tests/`: the support code, the layout and conversion tests, and the RGB golden-frame hashes, which change with `Color`'s layout and are regenerated deliberately, reviewing the diff.
- `scripts/sample-build.sh`, `scripts/sample-flash.sh`, `.github/workflows/ci.yml`, `tools/footprint/`.
- `samples/strip_demo/` (adapter, `prj.conf`, README) and `samples/frame_cost/` (`prj.conf`, README, whose closing paragraph explains the RGB/RGBW gap this removes).
- `docs/DESIGN.md`'s "White is a fourth channel", `docs/FEATURES.md`, `docs/COOKBOOK.md`, the README's Configuration and Footprint sections, `CLAUDE.md`'s variant list and bench note, and `notes/build-variants-guide.md` and `notes/cmake-usage-requirements-guide.md`.

## Reproducing the measurements

The baseline is `SAMPLE=frame_cost scripts/sample-build.sh`, with `--rgb` for the RGB column, flashed and read as `samples/frame_cost/README.md` describes. The experiments add to the `west build` command line after `--`: `-DEXTRA_CXXFLAGS="-falign-functions=32 -falign-loops=32 -falign-jumps=32 -falign-labels=32"` for the aligned columns and `-DCONFIG_STRIPSEQ_GAMMA=n` for the no-gamma ones. The padded column needs `uint32_t pad_ = 0;` after `duration_ms` in `Step`, with the `static_assert` beside it changed to `16`, in a scratch copy of the repository. The disassembly is `arm-zephyr-eabi-objdump -d -C` of `zephyr/zephyr.elf` in the build directory, at `stripseq::Sequence::at(int) const`.
