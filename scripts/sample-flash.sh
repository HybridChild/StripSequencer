#!/usr/bin/env bash
# Flash the bring-up sample to the Nucleo-H753ZI over its ST-LINK.
#
#   scripts/sample-flash.sh              # what scripts/sample-build.sh built
#   scripts/sample-flash.sh --rgb        # what scripts/sample-build.sh --rgb built
#   STLINK_SERIAL=0052... scripts/sample-flash.sh   # one probe of several
#
# The openocd runner, because the board defaults to stm32cubeprogrammer, which
# the workspace does not install; the SDK bundles openocd. With more than one
# ST-LINK attached, openocd takes whichever it finds first, so this refuses to
# guess -- a wrong guess writes the image to the wrong board and reports
# success. `west flash -r openocd --help` lists the probes' serials.
set -euo pipefail

WORKSPACE="${ZEPHYR_WORKSPACE:-$HOME/zephyr-workspace}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="$REPO/build-zephyr"
if [[ "${1:-}" == "--rgb" ]]; then
    BUILD_DIR="$REPO/build-zephyr-rgb"
    shift
fi

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "No build at $BUILD_DIR -- run scripts/sample-build.sh first." >&2
    exit 1
fi

# An unmatched glob stays literal, so test the first entry for existence.
PORTS=(/dev/cu.usbmodem*)
if [[ -z "${STLINK_SERIAL:-}" && -e "${PORTS[0]}" ]] && (( ${#PORTS[@]} > 1 )); then
    echo "More than one ST-LINK is attached; set STLINK_SERIAL= to choose one." >&2
    exit 2
fi

# shellcheck source=/dev/null
source "$WORKSPACE/.venv/bin/activate"
export ZEPHYR_BASE="$WORKSPACE/zephyr"
cd "$WORKSPACE"

# Two spelled-out exec lines rather than an argument array: macOS ships bash 3.2,
# where expanding an empty array under `set -u` is an error rather than nothing.
if [[ -n "${STLINK_SERIAL:-}" ]]; then
    exec west flash -d "$BUILD_DIR" -r openocd --serial "$STLINK_SERIAL" "$@"
fi
exec west flash -d "$BUILD_DIR" -r openocd "$@"
