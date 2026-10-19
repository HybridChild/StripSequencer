#!/usr/bin/env bash
# Write the machine-specific half of the VS Code setup: .vscode/settings.json
# from .vscode/settings.template.jsonc, and .vscode/devicetree.json, with the
# absolute paths of this machine's Zephyr workspace.
#
#   scripts/ide-setup.sh                 # the workspace at ~/zephyr-workspace
#   ZEPHYR_WORKSPACE=/opt/zws scripts/ide-setup.sh
#
# Run once per checkout, and again after editing the template or moving the
# workspace. Both outputs are gitignored: the DeviceTree and Kconfig extensions
# take only absolute paths, as the template explains, so a committed copy would
# describe one machine.
#
# Every sample with an overlay for the board gets a DeviceTree context of its
# own. A sample added later needs this run again.
#
# The workspace, board and arch follow scripts/sample-build.sh: ZEPHYR_WORKSPACE
# and BOARD override them, and ARCH is the board's, which hwmv2 no longer puts
# in its path.
set -euo pipefail

WORKSPACE="${ZEPHYR_WORKSPACE:-$HOME/zephyr-workspace}"
BOARD="${BOARD:-nucleo_h753zi}"
ARCH="${ARCH:-arm}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

ZEPHYR_BASE="$WORKSPACE/zephyr"
if [[ ! -d "$ZEPHYR_BASE" ]]; then
    echo "No Zephyr at $ZEPHYR_BASE -- set ZEPHYR_WORKSPACE to the west workspace." >&2
    exit 1
fi

# The workspace's own virtualenv first, as scripts/sample-build.sh activates it;
# a workspace without one has west on the PATH.
WEST="$WORKSPACE/.venv/bin/west"
if [[ ! -x "$WEST" ]]; then
    WEST="$(command -v west || true)"
    if [[ -z "$WEST" ]]; then
        echo "No west in $WORKSPACE/.venv or on the PATH." >&2
        exit 1
    fi
fi

# boards/<vendor>/<board>/, with the vendor found rather than assumed.
shopt -s nullglob
BOARD_FILES=("$ZEPHYR_BASE"/boards/*/"$BOARD"/"$BOARD".dts)
shopt -u nullglob
if (( ${#BOARD_FILES[@]} != 1 )); then
    echo "No single $BOARD.dts under $ZEPHYR_BASE/boards -- check BOARD." >&2
    exit 1
fi
BOARD_FILE="${BOARD_FILES[0]}"

python3 - "$REPO" "$ZEPHYR_BASE" "$WEST" "$BOARD" "$ARCH" "$BOARD_FILE" <<'PY'
import json, pathlib, sys

repo, zephyr_base, west, board, arch, board_file = sys.argv[1:]
repo = pathlib.Path(repo)
vscode = repo / ".vscode"
ctx_file = vscode / "devicetree.json"

# The bring-up sample first, as it leads everywhere else in the tooling, then
# the rest by name.
overlays = sorted(repo.glob(f"samples/*/boards/{board}.overlay"),
                  key=lambda o: (o.parent.parent.name != "strip_demo", o))
contexts = [
    {
        "name": overlay.parent.parent.name,
        "boardFile": board_file,
        "board": {"name": board, "path": board_file, "arch": arch},
        "overlays": [str(overlay)],
    }
    for overlay in overlays
]
ctx_file.write_text(json.dumps(contexts, indent="\t") + "\n")

# Plain substitution rather than a JSON round trip, which would drop the
# template's comments. json.dumps quotes each value, so a path with a backslash
# or a quote in it stays a valid string.
values = {
    "ZEPHYR_BASE": zephyr_base,
    "WEST": west,
    "CTX_FILE": str(ctx_file),
    "BOARD": board,
    "ARCH": arch,
    "BOARD_DIR": str(pathlib.Path(board_file).parent),
}
text = (vscode / "settings.template.jsonc").read_text()
for key, value in values.items():
    text = text.replace(f'"@{key}@"', json.dumps(value))
if "@" in "".join(line.split("//")[0] for line in text.splitlines()):
    print("ide-setup: the template has a placeholder this script does not fill", file=sys.stderr)
    raise SystemExit(1)
(vscode / "settings.json").write_text(text)

print(f"ide-setup: {len(contexts)} DeviceTree contexts, Zephyr at {zephyr_base}")
PY
