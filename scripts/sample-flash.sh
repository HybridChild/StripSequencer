#!/usr/bin/env bash
# Flash a sample to a Nucleo over its ST-LINK: the bring-up sample, unless
# SAMPLE= names another, as for scripts/sample-build.sh. The board is whichever
# the build was made for -- BOARD= there, not here.
#
#   scripts/sample-flash.sh              # what scripts/sample-build.sh built
#   scripts/sample-flash.sh --rgb        # what scripts/sample-build.sh --rgb built
#   SAMPLE=frame_cost scripts/sample-flash.sh
#   STLINK_SERIAL=0052... scripts/sample-flash.sh   # one probe of several
#
# No board's default runner, stm32cubeprogrammer, is installed in the
# workspace, so the runner follows the board the build was made for:
#
#   nucleo_h753zi  openocd, which the SDK bundles. The board's openocd config
#                  connects under reset, which a running bench build needs: it
#                  idles in sleep, and Zephyr gates D1's debug clocks there
#                  unless CONFIG_STM32_ENABLE_DEBUG_SLEEP_STOP is set. pyocd
#                  cannot get past that, under reset or not.
#   nucleo_u083rc  pyocd, as Zephyr's board file offers no openocd.
#                  pyocd comes with the workspace's Python requirements, but
#                  knows the part only once its CMSIS pack is installed, once
#                  per machine: `pyocd pack install stm32u083rctx`.
#   nucleo_f072rb  openocd, the SDK's, with the board's own openocd config.
#   nucleo_f401re  none. The bench's F401 has a dead ST-LINK and is flashed
#                  through another Nucleo's debug header, which no runner here
#                  describes.
#
# With more than one ST-LINK attached, openocd takes whichever it finds first
# and pyocd stops to ask, so this refuses to guess -- a wrong guess writes the
# image to the wrong board and reports success. `pyocd list` shows each probe's
# serial.
set -euo pipefail

WORKSPACE="${ZEPHYR_WORKSPACE:-$HOME/zephyr-workspace}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

SAMPLE="${SAMPLE:-strip_demo}"
BUILD_DIR="$REPO/build-zephyr"
[[ "$SAMPLE" != strip_demo ]] && BUILD_DIR+="-$SAMPLE"
if [[ "${1:-}" == "--rgb" ]]; then
    BUILD_DIR+="-rgb"
    shift
fi

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "No build at $BUILD_DIR -- run scripts/sample-build.sh first." >&2
    exit 1
fi

# One serial port per attached ST-LINK: /dev/cu.usbmodem* on macOS, and on Linux
# the link udev names after the probe under /dev/serial/by-id -- not
# /dev/ttyACM*, which every CDC-ACM device shares. nullglob, so the pattern for
# the other system adds nothing rather than itself.
shopt -s nullglob
PORTS=(/dev/cu.usbmodem* /dev/serial/by-id/usb-STMicroelectronics_*[Ss][Tt][Ll][Ii][Nn][Kk]*)
shopt -u nullglob
if [[ -z "${STLINK_SERIAL:-}" ]] && (( ${#PORTS[@]} > 1 )); then
    echo "More than one ST-LINK is attached; set STLINK_SERIAL= to choose one." >&2
    exit 2
fi

BOARD="$(sed -n 's/^BOARD:STRING=//p' "$BUILD_DIR/CMakeCache.txt")"
case "$BOARD" in
nucleo_h753zi*) RUNNER=openocd; SERIAL_FLAG=--serial ;;
nucleo_u083rc*) RUNNER=pyocd;   SERIAL_FLAG=--dev-id ;;
nucleo_f072rb*) RUNNER=openocd; SERIAL_FLAG=--serial ;;
*)
    echo "No runner chosen for board '$BOARD'." >&2
    exit 1
    ;;
esac

# shellcheck source=/dev/null
source "$WORKSPACE/.venv/bin/activate"
export ZEPHYR_BASE="$WORKSPACE/zephyr"
cd "$WORKSPACE"

# Two spelled-out exec lines rather than an argument array: macOS ships bash 3.2,
# where expanding an empty array under `set -u` is an error rather than nothing.
if [[ -n "${STLINK_SERIAL:-}" ]]; then
    exec west flash -d "$BUILD_DIR" -r "$RUNNER" "$SERIAL_FLAG" "$STLINK_SERIAL" "$@"
fi
exec west flash -d "$BUILD_DIR" -r "$RUNNER" "$@"
