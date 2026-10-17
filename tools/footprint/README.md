# footprint

What the library costs on a bare-metal Cortex-M: the size of each of its objects on the target, and the flash and RAM it adds to an application, in each of the four configurations that change compiled code (`STRIPSEQ_WHITE_CHANNEL` × `STRIPSEQ_GAMMA`), on a Cortex-M0 and a Cortex-M4.

```sh
tools/footprint/footprint.sh              # the tables, built under ./build-footprint
tools/footprint/footprint.sh --symbols    # and every symbol behind them, largest first
```

It needs what [`scripts/cross-check.sh`](../../scripts/cross-check.sh) needs: `arm-none-eabi-gcc`, Ninja and CMake 3.24. Nothing is run and no board is involved. The time a frame takes is measured on hardware instead, by [`samples/frame_cost/`](../../samples/frame_cost/README.md).

## What is measured

[`app.cpp`](app.cpp) calls every public entry point once, with inputs held in `volatile` variables so that none of the calls can be evaluated at compile time. It is linked with `-Os -ffunction-sections -fdata-sections -Wl,--gc-sections`, as any embedded build would be, so whatever the linker keeps is what an application calling all of the API pays. An application that never builds a sequence at run time, never installs a plug-in or never pauses pays less.

[`CMakeLists.txt`](CMakeLists.txt) is a project of its own, a consumer of the library: it brings the library in with `add_subdirectory()`, sets the configuration as `PUBLIC` definitions on it as the top-level README asks of every consumer, and adds the section flags at directory scope so the library inherits them without setting any itself. The toolchain is the cross-check's, [`cmake/arm-none-eabi.cmake`](../../cmake/arm-none-eabi.cmake).

## How it is attributed

By symbol, from the linked image's symbol table, rather than by the object file each section came from. An inline variable such as `stripseq::no_sequence` is emitted into every object that uses it, and the linker keeps whichever copy it reads first, which is usually the application's. Attributed by file it would be charged to the application. By name, it is the library's.

| Column | What it counts |
| --- | --- |
| Library code | Functions in namespace `stripseq`. |
| Library constants | Read-only objects in namespace `stripseq`: the 256-byte gamma table when `STRIPSEQ_GAMMA=1`, and `no_sequence`, the empty sequence every `Sequencer` refers to until something is loaded, which is one full `Sequence`. |
| Library RAM | Static data of the library's own. Zero: every byte of RAM the library uses is inside a `Sequence`, `Sequencer` or `Strip` the application declares. |
| libgcc helpers | The compiler's run-time routines, for operations the core has no instruction for. None on a Cortex-M4. On a Cortex-M0, `__udivsi3` for 32-bit division and `__aeabi_lmul` for the builder's 64-bit duration check. |
| memcpy, memset | From newlib-nano, emitted by the compiler for copying and clearing structures: in `Sequence::at`, the emptied slot in `Strip::remove`, the sequence the builder copies out. |
| build() at run time | `SequenceBuilder` is header-only, so building a sequence at run time is compiled where the application does it. `app.cpp` does it in a function of its own so it can be counted. `build_const()` costs nothing here: it runs in the compiler. |

The object sizes are read from the symbol table too: `app.cpp` declares one of each at file scope, under names the script looks for.

Where a symbol came from and why it was kept is answered by the linker map, written beside each image as `build-footprint/<core>-<configuration>/footprint.map`.
