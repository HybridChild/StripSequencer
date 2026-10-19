#!/usr/bin/env bash
# Run the host tests of one of CMakePresets.json's trees. Every case is
# registered with CTest individually and prefixed with its variant, so a failure
# names both the case and the configuration it broke in.
#
#   scripts/test.sh                      # everything in ./build
#   scripts/test.sh gcc                  # another preset's tree
#   scripts/test.sh default -R '^rgbw:'  # one variant
#   scripts/test.sh default -N           # list without running
#
# Assumes scripts/build.sh has been run with the same preset; ctest does not
# build.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

PRESET="${1:-default}"
[[ $# -gt 0 ]] && shift

cd "$REPO"
ctest --preset "$PRESET" -j "$@"
