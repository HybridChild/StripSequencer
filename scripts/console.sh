#!/usr/bin/env bash
# Open the Nucleo's serial console (the ST-LINK virtual COM port) at 115200 8N1,
# where the bring-up sample names each stage and how many frames the last one
# rendered.
#
#   scripts/console.sh                   # the only ST-LINK attached
#   scripts/console.sh /dev/cu.usbmodemXYZ
#   scripts/console.sh /dev/ttyACM0
#
# The port is found rather than hardcoded: /dev/cu.usbmodem* on macOS, whose
# trailing digits come from the USB topology, and on Linux the link udev names
# after the probe under /dev/serial/by-id -- not /dev/ttyACM*, which every
# CDC-ACM device shares. Without udev, as in a WSL that does not run systemd,
# there is no such link and the port has to be named.
#
# Quit with Ctrl-A then K: Ctrl-A then D only detaches, and the port stays busy.
set -euo pipefail

BAUD="${BAUD:-115200}"

if [[ $# -gt 0 ]]; then
    PORT="$1"
else
    # nullglob, so the pattern for the other system adds nothing rather than
    # itself.
    shopt -s nullglob
    PORTS=(/dev/cu.usbmodem* /dev/serial/by-id/usb-STMicroelectronics_*[Ss][Tt][Ll][Ii][Nn][Kk]*)
    shopt -u nullglob
    if (( ${#PORTS[@]} == 0 )); then
        echo "No ST-LINK serial port found -- is the board plugged in? Name the port to skip the search." >&2
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
