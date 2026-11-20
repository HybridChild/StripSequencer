#!/usr/bin/env bash
# Build every sample on every board its sample.yaml names, in every scenario it
# lists, with Zephyr's twister, against the shared Zephyr workspace
# (~/zephyr-workspace). This is the samples' half of "every change ends green",
# and CI runs it as it stands.
#
#   scripts/twister.sh                   # the whole matrix, into ./build-twister
#   scripts/twister.sh -p nucleo_u083rc  # anything else goes to twister
#
# -G builds each scenario on its integration_platforms, which the sample.yaml
# files set to the boards they allow: without it twister picks Zephyr's default
# platforms, and filters them all away. Twister makes warnings errors unless
# told otherwise, which is what a green build here means.
#
# scripts/sample-build.sh stays the way to build one image to flash.
#
# ZEPHYR_WORKSPACE= points it elsewhere, which is how CI uses it.
set -euo pipefail

WORKSPACE="${ZEPHYR_WORKSPACE:-$HOME/zephyr-workspace}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

# A workspace with its own virtualenv brings west and its Python into scope; one
# without (CI's) already has them on the PATH.
if [[ -f "$WORKSPACE/.venv/bin/activate" ]]; then
    # shellcheck source=/dev/null
    source "$WORKSPACE/.venv/bin/activate"
fi
export ZEPHYR_BASE="$WORKSPACE/zephyr"

# west finds its workspace from the current directory.
cd "$WORKSPACE"
exec west twister -T "$REPO/samples" -G -b -O "$REPO/build-twister" --inline-logs "$@"
