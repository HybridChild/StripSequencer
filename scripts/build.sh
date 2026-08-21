#!/usr/bin/env bash
# Configure and build the host tree: the library plus one (library, test binary)
# pair per build variant.
#
#   scripts/build.sh                     # into ./build
#   scripts/build.sh build-gcc           # into another directory
#   CXX=g++-16 scripts/build.sh build-gcc   # the other compiler
#   scripts/build.sh build -DRGBSEQ_BUILD_TESTS=OFF   # extra args go to cmake
#
# Why CXX matters on macOS: /usr/bin/g++ is Apple clang, not GCC. The second
# compiler in the matrix is homebrew's, and it is only ever called g++-16.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${1:-build}"
[[ $# -gt 0 ]] && shift

cmake -S "$REPO" -B "$REPO/$BUILD_DIR" -G Ninja "$@"
cmake --build "$REPO/$BUILD_DIR"
