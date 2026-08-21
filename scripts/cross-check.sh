#!/usr/bin/env bash
# The freestanding cross-compile check: build the library -- and only the
# library -- for a bare-metal Cortex-M, with no host libc anywhere near it.
#
#   scripts/cross-check.sh               # into ./build-cross
#   scripts/cross-check.sh build-m4      # somewhere else
#
# It catches host-libc dependencies, accidental heap use through operator new,
# and code that only compiles because the host happens to be 64-bit. Nothing is
# linked and nothing is run: on a target with no startup file and no linker
# script, compiling is the whole signal. From Phase 7 the resulting objects are
# also what gets checked for a non-empty .init_array.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${1:-build-cross}"

cmake -S "$REPO" -B "$REPO/$BUILD_DIR" -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$REPO/cmake/arm-none-eabi.cmake" \
      -DRGBSEQ_BUILD_TESTS=OFF
cmake --build "$REPO/$BUILD_DIR"

# An archive that exists is the exit criterion; listing it is what makes the
# check readable in CI output.
arm-none-eabi-size "$REPO/$BUILD_DIR"/CMakeFiles/rgbseq.dir/src/*.obj
