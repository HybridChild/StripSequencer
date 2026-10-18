# TODO

What remains of hardening and release.

- **Always RGBW.** Remove `STRIPSEQ_WHITE_CHANNEL` and compile four channels always, as [`docs/ALWAYS-RGBW.md`](docs/ALWAYS-RGBW.md) proposes: the RGB build's 12-byte step costs a multiply per step walked, up to an eighth of a frame, more than the missing channel saves. Settle how many bytes per pixel `render()` writes first; the proposal lays out both answers.
- **Tag `v0.1.0`**, which `project()` in `CMakeLists.txt` already declares. The README's `FetchContent` example then pins it in place of `main`.
