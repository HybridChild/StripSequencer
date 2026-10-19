#!/usr/bin/env bash
# Configure and build a host tree from one of CMakePresets.json's presets: the
# library plus one (library, test binary) pair per build variant.
#
#   scripts/build.sh                     # the default preset, into ./build
#   scripts/build.sh gcc                 # the GCC tree, into ./build-gcc
#   scripts/build.sh debug               # with debug information, into ./build-debug
#   scripts/build.sh default -DSTRIPSEQ_BUILD_TESTS=OFF   # extra args go to cmake
#
# The presets hold each tree's configuration, so this, `cmake --preset` and an
# IDE that reads presets all configure the same tree the same way.
#
# The gcc preset runs g++ followed by STRIPSEQ_GCC_SUFFIX. On macOS g++ is Apple
# clang, and Homebrew's GCC is only ever installed with its version on the end:
#
#   STRIPSEQ_GCC_SUFFIX=-16 scripts/build.sh gcc
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

PRESET="${1:-default}"
[[ $# -gt 0 ]] && shift

# CMake's own refusal names only a "disabled preset", which says nothing about
# what would enable it.
if [[ "$PRESET" == gcc && "$(uname -s)" == Darwin && -z "${STRIPSEQ_GCC_SUFFIX:-}" ]]; then
    echo "On macOS g++ is Apple clang; set STRIPSEQ_GCC_SUFFIX to name a real GCC, e.g. -16 for g++-16." >&2
    exit 1
fi

# Presets are read from the current directory, by the build step especially.
cd "$REPO"
cmake --preset "$PRESET" "$@"
cmake --build --preset "$PRESET"
