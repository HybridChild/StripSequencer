# StripSequencer

A C++20 library for animating WS2812-style addressable LED strips on microcontrollers, with no heap, no exceptions, no RTTI and no floating point.

[![CI](https://github.com/HybridChild/StripSequencer/actions/workflows/ci.yml/badge.svg)](https://github.com/HybridChild/StripSequencer/actions/workflows/ci.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)](https://github.com/HybridChild/StripSequencer)
[![Zephyr module](https://img.shields.io/badge/Zephyr-module-blueviolet)](zephyr/module.yml)
[![License: Apache-2.0](https://img.shields.io/badge/License-Apache_2.0-blue)](LICENSE)

---

## Overview

You describe an animation once, as a list of colors and durations. The library works out what every pixel should be at any moment, writes those colors into a buffer you own, and tells you when to come back. Pushing the buffer to the hardware is your driver's job.

It is a standalone CMake library with no knowledge of any operating system, and the same repository is a Zephyr module. It never touches hardware, never reads a clock and never allocates, which is what makes the same code testable on a laptop and runnable on bare metal.

**Key features:**

- **One mechanism, not a catalogue of effects.** A sequence is a stateless lookup table indexed by a playhead, and every pixel of a segment reads it a fixed `pixel_delay` behind its neighbour. Blink, breathe, comet, rainbow, static gradient, wipe and progress bar all come out of that one expression with different numbers.
- **No per-pixel state, ever.** Nothing in the library scales with strip length except the pixel buffer you were allocating anyway.
- **Drift-free.** Colors are looked up at the real current time, so a late frame is a slightly late frame, never a slowly desynchronising animation, and animations started together stay together.
- **Exact sleep hints.** `render()` returns how long until any pixel changes: come back at frame rate, after exactly *n* ms, or never. A strip holding a steady color sleeps for the whole hold.
- **Overlapping segments.** Paint a background across the whole strip and an alert over it; remove the alert and the background is still running, exactly where it would have been.
- **Two plug-in points.** Replace where a pixel looks on the timeline, what color is there, or both, and keep segments, brightness, gamma and the sleep hint working.
- **RGBW as a build option.** A fourth channel that fades and eases like the other three, and is never derived from them.
- **Gamma and brightness** applied once, in the conversion to the 8-bit buffer.
- **No floating point anywhere**, not even at compile time: integer math is bit-exact across compilers, and nothing pulls in soft-float on a Cortex-M0.
- **Sequences checked at compile time.** A malformed sequence known at compile time is a compile error naming what is wrong; one built from user input reports an error instead.

## Quick start

### Add it to a CMake project

```cmake
include(FetchContent)
FetchContent_Declare(StripSequencer
    GIT_REPOSITORY https://github.com/HybridChild/StripSequencer.git
    GIT_TAG        main)
FetchContent_MakeAvailable(StripSequencer)

target_link_libraries(app PRIVATE StripSequencer::stripseq)
```

`add_subdirectory()` on a checkout works the same way. The library asks for C++20 as a compile feature and sets no other compiler flag, and its tests are built only when it is the top-level project.

### Or as a Zephyr module

Add the repository to your `west.yml`, or point `ZEPHYR_EXTRA_MODULES` at a checkout as [`samples/strip_demo/CMakeLists.txt`](samples/strip_demo/CMakeLists.txt) does, then in `prj.conf`:

```ini
CONFIG_CPP=y
CONFIG_STD_CPP20=y
CONFIG_STRIPSEQ=y
```

Zephyr's default minimal C++ library is enough: the core includes only `<cstdint>` and `<cstddef>`.

### Blink

```cpp
#include <stripseq/stripseq.h>

using namespace stripseq;

static constexpr uint16_t kPixels = 30;

static uint8_t   buffer[kPixels * Strip::bytes_per_pixel];   // yours, not the library's
static Sequencer seq;
constinit static Strip strip(buffer);   // pixel count deduced from the buffer

// Red 500 ms, off 500 ms, forever. Checked by the compiler, stored in flash.
static constexpr Sequence kBlink =
    SequenceBuilder{}.hold(colors::red, 500).hold(colors::black, 500).build_const();

int main() {
    seq.load(kBlink);
    seq.play(millis());                     // your 64-bit millisecond clock
    strip.add(Segment(0, kPixels, seq));    // every pixel, no stagger

    for (;;) {
        const uint32_t hold = strip.render(millis());
        send_to_strip(buffer, sizeof buffer);   // your driver, in R, G, B order

        if (hold == hold_forever)
            wait_for_event();                   // nothing will change again
        else
            sleep_ms(hold != 0 ? hold : 16);    // 0 means mid-fade: frame rate
    }
}
```

Give the segment a `pixel_delay` and the same sequence travels down the strip instead of flashing all at once. The [cookbook](docs/COOKBOOK.md#the-recipes) has ten more recipes, and the Zephyr frame loop, with `k_work_reschedule()` in place of the sleep, is [at the end of it](docs/COOKBOOK.md#on-zephyr).

## Configuration

Four build-time options, each an `#ifndef`-guarded macro in [`include/stripseq/config.h`](include/stripseq/config.h). Set them on the library target as `PUBLIC` definitions, so the library and every file that includes its headers agree, or through Kconfig under Zephyr.

| Macro | Kconfig | Default | Effect |
| --- | --- | --- | --- |
| `STRIPSEQ_MAX_STEPS` | `CONFIG_STRIPSEQ_MAX_STEPS` | `8` | Steps a sequence can hold. Sets the size of every `Sequence`. |
| `STRIPSEQ_MAX_SEGMENTS` | `CONFIG_STRIPSEQ_MAX_SEGMENTS` | `4` | Segments a strip can hold. Sets the size of every `Strip`. |
| `STRIPSEQ_WHITE_CHANNEL` | `CONFIG_STRIPSEQ_WHITE_CHANNEL` | `0` | `1` adds a fourth, white channel to every color and every pixel, for RGBW parts such as the SK6812. |
| `STRIPSEQ_GAMMA` | `CONFIG_STRIPSEQ_GAMMA` | `1` | Gamma-corrects the 8-bit output through a 256-byte table. `0` converts linearly and links no table. |

```cmake
target_compile_definitions(stripseq PUBLIC STRIPSEQ_WHITE_CHANNEL=1)
```

`PUBLIC` matters. An application compiled with a different `STRIPSEQ_WHITE_CHANNEL` from the library links cleanly against a `Color` of the wrong size.

## Footprint

On a 32-bit target, at the default capacities:

| Object | RGB | RGBW | Where it lives |
| --- | --: | --: | --- |
| `Sequence` | 116 B | 152 B | Flash, when built with `build_const()` |
| `Sequencer` | 24 B | 24 B | RAM; it refers to its sequence rather than copying it |
| `Segment` | 20 B | 20 B | Inside the strip, `STRIPSEQ_MAX_SEGMENTS` of them |
| `Strip` | 92 B | 92 B | RAM; its segments, and where your buffer is |
| Pixel buffer | 3 B per pixel | 4 B per pixel | Yours |

A sequence grows by one step's size, 12 bytes or 16 in RGBW, for each step of `STRIPSEQ_MAX_STEPS`, and a strip by 20 bytes for each slot of `STRIPSEQ_MAX_SEGMENTS`. Nothing runs before `main()`: every table and every `constexpr` sequence is constant-initialised, and a `Strip` can be `constinit` at file scope.

### Flash

The library's code and constants, linked into an application that calls every part of the API, at `-Os` with `--gc-sections`, by GCC 15:

| Configuration | Cortex-M0 | Cortex-M4 |
| --- | --: | --: |
| RGB, gamma (the default) | 1938 B | 1840 B |
| RGB, no gamma | 1668 B | 1570 B |
| RGBW, gamma | 1990 B | 1906 B |
| RGBW, no gamma | 1720 B | 1636 B |

Gamma is the 256-byte table, and the white channel costs almost nothing in code. The constants include one empty `Sequence`, which every sequencer refers to until something is loaded. The library has no RAM of its own: all of it is in the objects above.

On top of that come the compiler's run-time helpers: 372 B on a Cortex-M0, for the 32-bit division it has no instruction for and the builder's 64-bit duration check, and none at all on a Cortex-M4. Nothing in the library divides in 64 bits. Building a sequence at run time adds about 300 B where `SequenceBuilder` is used; `build_const()` adds nothing. [`tools/footprint/`](tools/footprint/README.md) produces these figures, symbol by symbol.

### Time per frame

`Strip::render()` over 60 pixels on the bench board, a Cortex-M7 at 480 MHz with caches on, warm, built at `-Os`:

| Every pixel… | RGB | RGBW |
| --- | --: | --: |
| on a hold | 7,918 cycles (16.5 µs) | 8,504 cycles (17.7 µs) |
| mid-fade | 11,603 cycles (24.2 µs) | 12,889 cycles (26.9 µs) |
| in a comet's tail, staggered | 12,268 cycles (25.6 µs) | 13,356 cycles (27.8 µs) |
| mid-fade in a rainbow spread across the strip | 13,402 cycles (27.9 µs) | 14,529 cycles (30.3 µs) |
| in the last of eight eased fades | 16,640 cycles (34.7 µs) | 17,151 cycles (35.7 µs) |
| painted twice, a comet over a rainbow | 24,994 cycles (52.1 µs) | 26,955 cycles (56.2 µs) |

That is 130 to 290 cycles a pixel for each segment covering it. At 60 frames a second, the most expensive of these is under 0.4% of this core. A core without a divide instruction or caches will take more. [`samples/frame_cost/`](samples/frame_cost/README.md) measures them.

## Documentation

- **[FEATURES.md](docs/FEATURES.md)** — every feature, with examples.
- **[COOKBOOK.md](docs/COOKBOOK.md)** — how the pieces fit, what one frame does, how a sequencer is driven, and eleven recipes compiled verbatim by the test suite.
- **[DESIGN.md](docs/DESIGN.md)** — why the library is shaped the way it is: the lookup-table mechanism, the supporting decisions, the non-goals.
- **[`samples/strip_demo/`](samples/strip_demo/README.md)** — the library on real hardware under Zephyr, and a staged check of that hardware for when it disagrees with the host.
- **[`samples/showcase/`](samples/showcase/README.md)** — a dozen animations on the same hardware, one per button press, each showing one thing the library does.
- **[`include/stripseq/`](include/stripseq/)** — the API, with the reason for each declaration beside it.
- **[`notes/`](notes/README.md)** — guides to the C++, CMake and Zephyr concepts the project leans on, using it as the running example.

## Building and testing

```sh
scripts/build.sh                   # configure and build into ./build
scripts/test.sh                    # run every test in every variant
CXX=g++-16 scripts/build.sh build-gcc && scripts/test.sh build-gcc
scripts/cross-check.sh             # freestanding Cortex-M4 and M0 builds
scripts/sample-build.sh            # the Zephyr sample; --rgb for the RGB path
tools/footprint/footprint.sh       # flash and object sizes, Cortex-M0 and M4
```

The host suite builds the whole library five times, once per configuration that changes compiled behaviour plus one at tight capacities, and runs every test in each. The cross-check compiles the library for bare-metal Cortex-M with only the two headers Zephyr's minimal C++ library provides, and fails if an object calls a run-time helper outside a short allowlist: a soft-float routine, `operator new`, anything from libc, or a division the design does not account for. It also fails on anything initialised at run time: an `.init_array` entry or a static's guard variable. CI runs the host suite on gcc and clang, the cross-check, and the Zephyr sample in both channel configurations.

## Licence

Copyright 2026 Esben Dueholm Nørgaard. Licensed under the [Apache License, Version 2.0](LICENSE), the licence Zephyr itself uses, so a Zephyr application that includes this library takes on no obligations it did not already have.
