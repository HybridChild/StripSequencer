#!/usr/bin/env bash
# Build a sample for a board against the shared Zephyr workspace
# (~/zephyr-workspace): by default the bring-up sample, samples/strip_demo, on
# the bench's Nucleo-H753ZI. Each sample names this repo in
# ZEPHYR_EXTRA_MODULES, so the workspace's west.yml never has to know it.
#
#   scripts/sample-build.sh                           # into ./build-zephyr-strip_demo-nucleo_h753zi
#   scripts/sample-build.sh --rgb                     # white channel off, into ...-nucleo_h753zi-rgb
#   scripts/sample-build.sh -p                        # pristine, after a devicetree or Kconfig edit
#   scripts/sample-build.sh showcase                  # into ./build-zephyr-showcase-nucleo_h753zi
#   scripts/sample-build.sh showcase -b nucleo_u083rc # into ./build-zephyr-showcase-nucleo_u083rc
#   scripts/sample-build.sh <args>                    # anything else goes to `west build`
#   scripts/sample-build.sh -- -DCONFIG_FOO=y         # and after --, to CMake
#
# scripts/lib/sample-args.sh has the arguments in full and how the tree is
# named.
#
# --rgb exists because the two settings take different code paths --
# led_strip_update_channels() and a permute, or led_strip_update_rgb() and a
# straight cast -- and both must keep building. It has nothing to do with the
# board: every bench strip is RGBW, and an --rgb build of one runs with the
# driver sending zero for the white die, three-quarters lit.
#
# ZEPHYR_WORKSPACE= points it elsewhere, which is how CI uses it.
set -euo pipefail

WORKSPACE="${ZEPHYR_WORKSPACE:-$HOME/zephyr-workspace}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

# shellcheck source=lib/sample-args.sh
source "$SCRIPT_DIR/lib/sample-args.sh"

# What is left splits at the first --: west's arguments before it, CMake's
# after. The script adds a CMake argument of its own, so it must own the one
# --; a second would reach CMake as a stray argument.
WEST_ARGS=()
CMAKE_ARGS=()
while [[ $# -gt 0 && "$1" != -- ]]; do
    WEST_ARGS+=("$1")
    shift
done
if [[ $# -gt 0 ]]; then
    shift
    CMAKE_ARGS=("$@")
fi

# A workspace with its own virtualenv brings west and its Python into scope; one
# without (CI's) already has them on the PATH.
if [[ -f "$WORKSPACE/.venv/bin/activate" ]]; then
    # shellcheck source=/dev/null
    source "$WORKSPACE/.venv/bin/activate"
fi
export ZEPHYR_BASE="$WORKSPACE/zephyr"

# west finds its workspace from the current directory.
cd "$WORKSPACE"
# ${a[@]+"${a[@]}"} rather than "${a[@]}": macOS ships bash 3.2, where an
# empty array under set -u is an error rather than nothing.
exec west build -p "$PRISTINE" -b "$BOARD" -s "$REPO/samples/$SAMPLE" -d "$BUILD_DIR" \
    ${WEST_ARGS[@]+"${WEST_ARGS[@]}"} \
    -- -DCONFIG_STRIPSEQ_WHITE_CHANNEL="$WHITE" ${CMAKE_ARGS[@]+"${CMAKE_ARGS[@]}"}
