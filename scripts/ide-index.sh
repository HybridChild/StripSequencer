#!/usr/bin/env bash
# Merge the host tree's and the samples' compile databases into one at the repo
# root, for clangd.
#
#   scripts/ide-index.sh                 # after scripts/build.sh and scripts/sample-build.sh
#
# clangd needs a compile command for each file it opens, and no single build
# tree has one for all of them: ./build compiles src/ and tests/ with the host
# compiler, and each build-zephyr*/ tree compiles one sample, the library and a
# slice of Zephyr with the cross-compiler. A file missing from the database gets
# a guessed command line with no Zephyr include paths, so <zephyr/kernel.h>
# "does not exist" and every symbol after it is an error.
#
# Run by hand, after the builds and again whenever a file that compiles shows
# missing headers in the editor. A database goes stale only when the set of
# files or their flags changes -- a new source, a Kconfig or devicetree edit --
# never from ordinary editing, so the build scripts deliberately do not call
# this: they would pay for it on every build.
#
# .vscode/settings.json points clangd at the merged file and nowhere else, so
# run this once on a fresh checkout even to work on the host side alone: with
# only ./build built, the merge is just the host database.
#
# The output is generated and gitignored. Nothing but the editor reads it.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

python3 - "$REPO" <<'PY'
import json, pathlib, sys

repo = pathlib.Path(sys.argv[1])

# First entry per file wins, so the order decides which configuration a file
# shared between trees is read under.
#
# The host tree goes first, for src/: the library is OS-agnostic, and the host
# is where it is tested, so clangd should see it as the tests do -- under the
# host compiler, with config.h's defaults. The bench's own configuration
# follows: strip_demo in RGBW, then the other samples, then everything else, so
# the Zephyr sources every sample compiles are read under the bring-up sample's
# Kconfig and the RGBW board.
sources = [
    repo / "build" / "compile_commands.json",
    repo / "build-zephyr" / "compile_commands.json",
    repo / "build-zephyr-showcase" / "compile_commands.json",
    repo / "build-zephyr-frame_cost" / "compile_commands.json",
]
sources += sorted(p for p in repo.glob("build-zephyr*/compile_commands.json")
                  if p not in sources)

merged, seen = [], set()
for db in sources:
    if not db.is_file():
        continue
    try:
        entries = json.loads(db.read_text())
    except json.JSONDecodeError:
        print(f"ide-index: skipping malformed {db.relative_to(repo)}", file=sys.stderr)
        continue
    for entry in entries:
        path = entry.get("file")
        if not path or path in seen:
            continue
        seen.add(path)
        merged.append(entry)

# The led_strip adapter is a header and nothing else, so it has no entry of its
# own anywhere, and clangd would borrow the command of the source whose path
# looks most like it -- which, for zephyr/include/..., is a C file in some
# build-zephyr*/zephyr/ directory: the wrong language, and no CONFIG_LED_STRIP.
# Each header gets the bring-up sample's main.cpp command instead, which is
# C++, the bench's RGBW, and the Kconfig the adapter is written against.
donor = repo / "samples" / "strip_demo" / "src" / "main.cpp"
borrowed = next((e for e in merged if e["file"] == str(donor)), None)
if borrowed is not None:
    for header in sorted((repo / "zephyr" / "include").glob("**/*.h")):
        entry = dict(borrowed, file=str(header))
        entry.pop("output", None)
        if "command" in entry:
            entry["command"] = entry["command"].replace(str(donor), str(header))
        else:
            entry["arguments"] = [str(header) if a == str(donor) else a
                                  for a in entry["arguments"]]
        merged.append(entry)

if not merged:
    print("ide-index: no compile_commands.json found -- run scripts/build.sh "
          "or scripts/sample-build.sh first", file=sys.stderr)
    raise SystemExit(1)

out = repo / "compile_commands.json"
out.write_text(json.dumps(merged, indent=1) + "\n")
print(f"ide-index: {len(merged)} entries -> {out.relative_to(repo)}")
PY
