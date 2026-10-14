#!/usr/bin/env bash
# Delete every build tree and the tooling artefacts beside them, leaving the
# checkout as a fresh clone plus whatever is uncommitted.
#
#   scripts/clean.sh                     # delete
#   scripts/clean.sh -n                  # list what would go, delete nothing
#
# Only ignored files are touched, and only those the paths below name. Never
# widen this to a bare `git clean -X`: notes/ideas.md is ignored too, because
# it is private, not because it is disposable.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

if [[ "${1:-}" == "-n" ]]; then
    MODE=-n
else
    MODE=-f
fi

git -C "$REPO" clean "$MODE" -d -X -- 'build*' .cache compile_commands.json
