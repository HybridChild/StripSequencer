#!/usr/bin/env bash
# Build the bring-up sample, samples/strip_demo, for the Nucleo-H753ZI against
# the shared Zephyr workspace (~/zephyr-workspace). The sample names this repo
# in ZEPHYR_EXTRA_MODULES, so the workspace's west.yml never has to know it.
#
#   scripts/sample-build.sh              # RGBW, the bench configuration, into ./build-zephyr
#   scripts/sample-build.sh --rgb        # white channel off, into ./build-zephyr-rgb
#   scripts/sample-build.sh -p           # pristine, after a devicetree or Kconfig edit
#   scripts/sample-build.sh <args>       # anything else goes to `west build`
#
# --rgb exists because the two settings take different code paths --
# led_strip_update_channels() and a permute, or led_strip_update_rgb() and a
# straight cast -- and both must keep building. It is still the RGBW board: the
# driver sends zero for the white die, so it runs, three-quarters lit.
#
# ZEPHYR_WORKSPACE= points it elsewhere, which is how CI uses it.
set -euo pipefail

WORKSPACE="${ZEPHYR_WORKSPACE:-$HOME/zephyr-workspace}"
BOARD="${BOARD:-nucleo_h753zi}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

PRISTINE=auto
WHITE=y
BUILD_DIR="$REPO/build-zephyr"
while [[ $# -gt 0 ]]; do
    case "$1" in
    -p)    PRISTINE=always; shift ;;
    --rgb) WHITE=n; BUILD_DIR="$REPO/build-zephyr-rgb"; shift ;;
    *)     break ;;
    esac
done

# A workspace with its own virtualenv brings west and its Python into scope; one
# without (CI's) already has them on the PATH.
if [[ -f "$WORKSPACE/.venv/bin/activate" ]]; then
    # shellcheck source=/dev/null
    source "$WORKSPACE/.venv/bin/activate"
fi
export ZEPHYR_BASE="$WORKSPACE/zephyr"

# west finds its workspace from the current directory.
cd "$WORKSPACE"
exec west build -p "$PRISTINE" -b "$BOARD" -s "$REPO/samples/strip_demo" -d "$BUILD_DIR" "$@" \
    -- -DCONFIG_STRIPSEQ_WHITE_CHANNEL="$WHITE"
