#!/usr/bin/env bash
# Run the host tests. Every case is registered with CTest individually and
# prefixed with its variant, so a failure names both the case and the
# configuration it broke in.
#
#   scripts/test.sh                      # everything in ./build
#   scripts/test.sh build-gcc            # another build directory
#   scripts/test.sh build -R '^rgbw:'    # one variant
#   scripts/test.sh build -N             # list without running
#
# Assumes scripts/build.sh has been run; ctest does not build.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${1:-build}"
[[ $# -gt 0 ]] && shift

ctest --test-dir "$REPO/$BUILD_DIR" --output-on-failure -j "$@"
