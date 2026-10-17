# CLAUDE.md

Guidance for Claude Code working in this repository. It covers how to *operate* here; why the library is shaped the way it is lives in `docs/DESIGN.md` and in the comments beside the code.

## What this is

An OS-agnostic C++20 library for animating WS2812-style LED strips: a **standalone CMake library** that a Zephyr module wraps, with no Zephyr, heap, exceptions, RTTI or floating point anywhere in the core.

## Stop and ask when something is unclear

**Ask rather than pick an interpretation and carry on.** That covers ambiguity in the design, a case it does not cover, and anything that looks wrong — including a recorded decision that turns out not to survive contact with a change. Say what is unclear, give the concrete options and what each costs, and recommend one.

Asking is cheap here and guessing is not. This is a learning project, so an unexamined assumption does not just risk being wrong — it skips the part that was the point. It also hardens fast: it becomes code, then precedent, then something later work quietly builds on.

This is not licence to stop at every fork. Do everything that does not depend on the answer first, then ask once, with the question narrowed to what actually blocks.

## Where decisions live

- **[`docs/DESIGN.md`](docs/DESIGN.md) — why.** The mechanism: a sequence as a stateless lookup table indexed by a continuous playhead, `pixel_delay` as the whole spatial dimension, the two plug-in points, hold as a binding contract rather than advice, and the invariant that there is **no per-pixel state inside the library, ever**. Also the cookbook, the supporting decisions and the non-goals. Read it first; everything else assumes it.
- **The headers and sources — what, and why this way.** Every decision about a name, a signature, a layout or an algorithm is recorded in the comment beside the declaration or definition it governs. The same holds for the build: `CMakeLists.txt`, `cmake/*.cmake`, `zephyr/` and the sample's overlay and `prj.conf` carry their reasons with them.
- **[`docs/COOKBOOK.md`](docs/COOKBOOK.md) — how it is called.** How the pieces relate, one frame, driving a sequencer, and DESIGN.md's recipes as real code. The recipes are the acceptance criteria: `tests/cookbook.cpp` compiles them verbatim and a CTest entry fails if the two drift, so a recipe that needs an edit to compile is a finding about the API, not a reason to edit the copy. If Blink takes more than describing the sequence and loading, starting and placing it, something is wrong upstream.
- **[`README.md`](README.md) and [`docs/FEATURES.md`](docs/FEATURES.md) — what it offers.** The front door and the feature-by-feature guide. Unlike the recipes, nothing checks their snippets, so a change to the API updates them by hand.
- **[`samples/strip_demo/README.md`](samples/strip_demo/README.md) — the bench.** The sample, and the order to look in when the hardware disagrees with the host.

When something is decided, record it beside the thing it decides. Don't start a decision log.

## Ground rules

These hold for every change.

- **The library never sets a toolchain flag.** Zephyr owns the compiler, the standard library, the optimisation level and the warning set for anything built inside it. The core target states `cxx_std_20` as a compile feature — a requirement CMake reconciles with the consumer — and nothing else. Warnings are applied only when `PROJECT_IS_TOP_LEVEL`, so they shape our own builds and never a consumer's.
- **No floating point anywhere in the library**, not even at compile time. Many Cortex-M parts have no FPU, soft-float pulls in library code, and integer math is bit-exact across compilers, which is what makes the golden-frame hashes meaningful. Tests may use `double` as an independent oracle.
- **No dynamic initialisation at file scope, and no function-local statics.** A function-local static with a non-constant initialiser emits a guard variable and calls `__cxa_guard_acquire`. Every table is `constexpr` or a `const` array in flash.
- **Only `<cstdint>` and `<cstddef>`.** They are what Zephyr's default minimal C++ library provides, so a third header is a library that does not build there. Where a `min` is wanted, write it. `scripts/cross-check.sh` enforces this, along with an allowlist of the run-time helpers each object may call.
- **Every change ends green**: all five variants (`default`, `rgbw`, `nogamma`, `rgbw_nogamma`, `tight`) on gcc and clang, the cross-check for both cores, and the Zephyr build of the sample in both channel configurations.

## The learning goal, and the guides in `notes/`

**This project is a vehicle for learning C++, CMake, and more Zephyr** — the way `../ClimateNode/` was a vehicle for Zephyr, MQTT and Protobuf. Where a choice trades a shortcut against understanding one of those three, prefer the one that teaches.

That learning is written down in `notes/`. The split mirrors the reference project: `docs/` and the code record what *this* project decided, while **`notes/` teaches the concept behind it**, from first principles, using this repo as the running example — see [`../ClimateNode/notes/README.md`](../ClimateNode/notes/README.md) for the model. A guide should be readable by someone who has never seen this project.

The guides follow a shared skeleton, and an edit keeps it: an opening roadmap, numbered concept sections each building on the last, a synthesis where there is one, a **lab** of numbered exercises with commands, expected output and a **Proves:** line, then the model in one paragraph and where to go next. Cross-reference by `§` within a guide and by name across them. Cite this repo by function name, and Zephyr by `file:line` against v4.4.1. Keep `notes/README.md` current as the index — reading order, dependencies, and what each guide assumes.

## One reference repository, for one thing only

**[`../ClimateNode/`](../ClimateNode/) — a reference for the Zephyr sample in `samples/strip_demo/`, and nothing else.** Useful for how a freestanding Zephyr app is laid out against the shared workspace at `~/zephyr-workspace` (v4.4.1): the `scripts/` wrappers, the heavily justified `prj.conf`, the `notes/` guides paired with `docs/` references. It is a Zephyr application; this is a standalone library, so do **not** carry over its testing approach — the core is pure arithmetic and is tested on the host, not with Ztest on qemu.

## Bench hardware

A Nucleo-H753ZI driving an 11-pixel RGBW strip over I2S3 on PD6, via `worldsemi,ws2812-i2s`. The one fact that reaches into everything host-side: the board runs `STRIPSEQ_WHITE_CHANNEL=1`, which is **not** the library default, so RGBW is a first-class entry in the host test matrix rather than a variant. What has been verified against the driver source is recorded where it applies — the overlay, `prj.conf`, `zephyr/Kconfig` and the sample's adapter — so check there before re-deriving it.
