# CLAUDE.md

Guidance for Claude Code working in this repository. It covers how to *operate* here; the specification lives in `docs/`.

## What this is

An OS-agnostic C++20 library for animating WS2812-style LED strips: a **standalone CMake library** that a Zephyr module wraps, with no Zephyr, heap, exceptions, RTTI or floating point anywhere in the core.

## Stop and ask when something is unclear

**During implementation, ask rather than pick an interpretation and carry on.** That covers ambiguity in the three documents, a case the spec does not cover, a decision the plan deliberately left open, and anything that looks wrong — including a design decision that turns out not to survive contact with the code. Say what is unclear, give the concrete options and what each costs, and recommend one.

Asking is cheap here and guessing is not. This is a learning project, so an unexamined assumption does not just risk being wrong — it skips the part that was the point. It also hardens fast: it becomes code, then precedent, then something a later phase quietly builds on.

This is not licence to stop at every fork. Do everything that does not depend on the answer first, then ask once, with the question narrowed to what actually blocks.

## The three documents in `docs/`

They are a spec, not notes, and they answer different questions. Read them in this order; each assumes the one before it.

- **[`DESIGN.md`](docs/DESIGN.md) — why.** The mechanism: a sequence as a stateless lookup table indexed by a continuous playhead, `pixel_delay` as the whole spatial dimension, the two plug-in points, hold as a binding contract rather than advice, and the invariant that there is **no per-pixel state inside the library, ever**. Also the cookbook, the supporting decisions and the non-goals.
- **[`API-SKETCH.md`](docs/API-SKETCH.md) — what.** The calling surface, signatures only. Decisions are recorded beside the declaration they govern, so the rationale for a name or a signature is next to it rather than in a changelog. The eleven cookbook recipes at the end are the acceptance criteria: if Blink is not three lines, something is wrong upstream.
- **[`IMPLEMENTATION-PLAN.md`](docs/IMPLEMENTATION-PLAN.md) — how, and in what order.** Eight phases with per-phase exit criteria, the ground rules that hold across all of them, the decisions deliberately left to their phase, and a risk register.

When something is decided, record it beside the thing it decides — that is the convention all three already follow. Don't start a decision log.

## The learning goal, and the guides in `notes/`

**This project is a vehicle for learning C++, CMake, and more Zephyr** — the way `../EthernetProtobufZephyr/` was a vehicle for Zephyr, MQTT and Protobuf. Where a choice trades a shortcut against understanding one of those three, prefer the one that teaches.

That learning is written down in `notes/`. The split mirrors the reference project: `docs/` records what *this* project decided, while **`notes/` teaches the concept behind it**, from first principles, using this repo as the running example — see [`../EthernetProtobufZephyr/notes/README.md`](../EthernetProtobufZephyr/notes/README.md) for the model. A guide should be readable by someone who has never seen this project.

**Each phase of the implementation plan ends with a conversation about what it taught**, and produces at least one guide out of that.

Follow the shared skeleton so every guide navigates the same way: an opening roadmap, numbered concept sections each building on the last, a synthesis where there is one, a **lab** of numbered exercises with commands, expected output and a **Proves:** line, then the model in one paragraph and where to go next. Cross-reference by `§` within a guide and by name across them. Cite this repo by function name, and Zephyr by `file:line` against v4.4.1. Keep `notes/README.md` current as the index — reading order, dependencies, and what each guide assumes.

## One reference repository, for one thing only

**[`../EthernetProtobufZephyr/`](../EthernetProtobufZephyr/) — a reference for the eventual Zephyr *test app*, and nothing else.** Useful for how a freestanding Zephyr app is laid out against the shared workspace at `~/zephyr-workspace` (v4.4.1): the `scripts/` wrappers, the heavily justified `prj.conf`, the `notes/` guides paired with `docs/` references. It is a Zephyr application; this is a standalone library, so do **not** carry over its testing approach — the core is pure arithmetic and is tested on the host, not with Ztest on qemu.

## Bench hardware

A Nucleo-H753ZI driving an 11-pixel RGBW strip over I2S3 on PB2, via `worldsemi,ws2812-i2s`. The one fact that reaches back into every earlier phase: the board runs `STRIPSEQ_WHITE_CHANNEL=1`, which is **not** the library default, so RGBW is a first-class entry in the host test matrix rather than a variant. Phase 6 of the plan lists what has already been verified against the driver source; check there before re-deriving it.
