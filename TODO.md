# TODO

What remains of hardening and release.

- **Always RGBW.** Remove `STRIPSEQ_WHITE_CHANNEL` and compile four channels always, as [`docs/ALWAYS-RGBW.md`](docs/ALWAYS-RGBW.md) proposes: the RGB build's 12-byte step costs a multiply per step walked, up to an eighth of a frame, more than the missing channel saves. Settle how many bytes per pixel `render()` writes first; the proposal lays out both answers.
- **Coverage-guided fuzzing.** Drive `check()` in `tests/fuzz.cpp` from `LLVMFuzzerTestOneInput` as well as from its seeded test case: libFuzzer finds inputs the generator was not written to reach. It needs clang with its runtime — Apple clang does not ship it, Homebrew LLVM does — a target of its own outside the variant loop, and a time budget in CI. Move `check()` and what it calls into a header both can include, keeping doctest out of it.
- **No-dynamic-initialisation check in CI.** Read the cross-compiled objects and fail when `.init_array` is non-empty or a `__cxa_guard` symbol appears. The undefined-symbol allowlist cannot see either.
- **`-Wconversion`.** Declined for now in `CMakeLists.txt`, with the reason beside it. If it is turned on, reach zero warnings on gcc and clang in every variant, and regenerate any golden hash a fix invalidates deliberately, reviewing the diff.
- **Licence.** Choose one — MIT, Apache-2.0, or both as rgb-sequencer does — and add the licence file or files, and a section and a badge in the README. Before the tag, since a release with no licence is not usable by anyone else.
- **Tag `v0.1.0`**, which `project()` in `CMakeLists.txt` already declares. The README's `FetchContent` example then pins it in place of `main`.
