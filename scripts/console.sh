#!/usr/bin/env bash
# Open the Nucleo's serial console (the ST-LINK virtual COM port) at 115200 8N1,
# where the bring-up sample names each stage and how many frames the last one
# rendered.
#
#   scripts/console.sh                   # the only /dev/cu.usbmodem* attached
#   scripts/console.sh /dev/cu.usbmodemXYZ
#
# The port's trailing digits come from the USB topology, so none is hardcoded.
# Quit with Ctrl-A then K: Ctrl-A then D only detaches, and the port stays busy.
set -euo pipefail

BAUD="${BAUD:-115200}"

if [[ $# -gt 0 ]]; then
    PORT="$1"
else
    PORTS=(/dev/cu.usbmodem*)
    if [[ ! -e "${PORTS[0]}" ]]; then
        echo "No /dev/cu.usbmodem* found -- is the board plugged in?" >&2
        exit 1
    fi
    if (( ${#PORTS[@]} > 1 )); then
        echo "More than one board is attached; name the port:" >&2
        printf '  %s\n' "${PORTS[@]}" >&2
        exit 2
    fi
    PORT="${PORTS[0]}"
fi

exec screen "$PORT" "$BAUD"
