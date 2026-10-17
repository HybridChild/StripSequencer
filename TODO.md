# TODO

What remains of hardening and release.

- **README.** What the library is, the Blink recipe, the four build-time macros, how to consume it standalone (`add_subdirectory` or `FetchContent`, linking `StripSequencer::stripseq`) and as a Zephyr module, the footprint numbers below, and a link to `docs/DESIGN.md`.
- **Footprint.** Flash and RAM for each of the four behavioural configurations (`STRIPSEQ_WHITE_CHANNEL` × `STRIPSEQ_GAMMA`), and the per-frame cycle cost of a 60-pixel strip. Check them against the sizes `docs/DESIGN.md` argues from — a `Sequence` at about 112 bytes in RGB and 148 in RGBW, a `Sequencer` at 24, a `Segment` at 20 — and revisit the default capacities (`STRIPSEQ_MAX_STEPS=8`, `STRIPSEQ_MAX_SEGMENTS=4`) in their light.
- **Sanitizers in CI.** Run the host suite under UBSan and ASan, with no reports. UBSan is the one that matters: signed overflow in the stagger, shift against divide, and the remainder of a negative playhead are exactly what it detects.
- **Fuzz harness.** Random steps and random playheads, extremes included, through `SequenceBuilder` and `Sequence::at`, under the sanitizers. `at()` is the one function that takes fully unconstrained input.
- **No-dynamic-initialisation check in CI.** Read the cross-compiled objects and fail when `.init_array` is non-empty or a `__cxa_guard` symbol appears. The undefined-symbol allowlist cannot see either.
- **`-Wconversion`.** Declined for now in `CMakeLists.txt`, with the reason beside it. If it is turned on, reach zero warnings on gcc and clang in every variant, and regenerate any golden hash a fix invalidates deliberately, reviewing the diff.
- **Tag `v0.1.0`**, which `project()` in `CMakeLists.txt` already declares.
