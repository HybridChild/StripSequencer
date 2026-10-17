# TODO

What remains of hardening and release.

- **The 64-bit remainder in `Sequencer::playhead`.** It is the only thing that links libgcc's 64-bit division, 544 B on a Cortex-M0 and 832 B on a Cortex-M4 (`tools/footprint/footprint.sh --symbols`), a third as much again as the library's own code on the one and half as much again on the other. Reduce the position modulo the loop using 32-bit division only, take `__aeabi_uldivmod` off the allowlist in `scripts/cross-check.sh`, and update the README's flash figures.
- **Always RGBW.** Remove `STRIPSEQ_WHITE_CHANNEL` and compile four channels always, as [`docs/ALWAYS-RGBW.md`](docs/ALWAYS-RGBW.md) proposes: the RGB build's 12-byte step costs a multiply per step walked, up to an eighth of a frame, more than the missing channel saves. Settle how many bytes per pixel `render()` writes first; the proposal lays out both answers.
- **Sanitizers in CI.** Run the host suite under UBSan and ASan, with no reports. UBSan is the one that matters: signed overflow in the stagger, shift against divide, and the remainder of a negative playhead are exactly what it detects.
- **Fuzz harness.** Random steps and random playheads, extremes included, through `SequenceBuilder` and `Sequence::at`, under the sanitizers. `at()` is the one function that takes fully unconstrained input.
- **No-dynamic-initialisation check in CI.** Read the cross-compiled objects and fail when `.init_array` is non-empty or a `__cxa_guard` symbol appears. The undefined-symbol allowlist cannot see either.
- **`-Wconversion`.** Declined for now in `CMakeLists.txt`, with the reason beside it. If it is turned on, reach zero warnings on gcc and clang in every variant, and regenerate any golden hash a fix invalidates deliberately, reviewing the diff.
- **Licence.** Choose one — MIT, Apache-2.0, or both as rgb-sequencer does — and add the licence file or files, and a section and a badge in the README. Before the tag, since a release with no licence is not usable by anyone else.
- **Tag `v0.1.0`**, which `project()` in `CMakeLists.txt` already declares. The README's `FetchContent` example then pins it in place of `main`.
