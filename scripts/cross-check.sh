#!/usr/bin/env bash
# The freestanding cross-compile check: build the library -- and only the
# library -- for two bare-metal Cortex-M cores, with no host libc and no C++
# headers beyond the two Zephyr's minimal C++ library ships, then check every
# run-time helper the objects call against an allowlist.
#
#   scripts/cross-check.sh               # into ./build-cross and ./build-cross-m0
#   scripts/cross-check.sh build-arm     # into ./build-arm and ./build-arm-m0
#
# It catches host-libc dependencies, a third standard header, accidental heap
# use through operator new, code that only compiles because the host happens
# to be 64-bit, and arithmetic the core cannot do in an instruction. Nothing is
# linked and nothing is run: on a target with no startup file and no linker
# script, compiling and reading the objects is the whole signal.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${1:-build-cross}"

# The helpers the design accepts, by object. 32-bit division is a call on a
# Cortex-M0 and fine anywhere; memcpy and memset are the compiler's own, for
# copies and clearing loops. The one 64-bit remainder is Sequencer::playhead's,
# taken once per segment per frame, so __aeabi_uldivmod is allowed there and
# nowhere else. A signed division, a 64-bit division anywhere else, a float
# routine, operator new or a libc call fails the check, by name.
ALLOWED="__aeabi_uidiv __aeabi_uidivmod memcpy memset"
ALLOWED_SEQUENCER="__aeabi_uldivmod"

status=0

# check_helpers <build dir> <cpu>: every symbol an object leaves undefined is either
# defined by another of the library's objects or on the allowlist.
check_helpers() {
    local objs=("$1"/CMakeFiles/stripseq.dir/src/*.obj)
    local defined
    defined="$(arm-none-eabi-nm --defined-only "${objs[@]}" | awk 'NF == 3 { print $3 }')"
    for obj in "${objs[@]}"; do
        local allowed="$ALLOWED"
        [[ "$(basename "$obj")" == sequencer.cpp.obj ]] && allowed+=" $ALLOWED_SEQUENCER"
        for sym in $(arm-none-eabi-nm -u "$obj" | awk '{ print $NF }'); do
            grep -qxF "$sym" <<< "$defined" && continue
            [[ " $allowed " == *" $sym "* ]] && continue
            echo "error: $2: $(basename "$obj") calls $(arm-none-eabi-c++filt "$sym"), which is not on the allowlist" >&2
            status=1
        done
    done
}

# build <build dir> <cpu>. --fresh because the toolchain's flags are cache
# defaults, read on the first configure only: a tree configured before a flag
# changed would otherwise keep checking without it.
build() {
    cmake --fresh -S "$REPO" -B "$REPO/$1" -G Ninja \
          -DCMAKE_TOOLCHAIN_FILE="$REPO/cmake/arm-none-eabi.cmake" \
          -DSTRIPSEQ_CROSS_CPU="$2" \
          -DSTRIPSEQ_BUILD_TESTS=OFF
    cmake --build "$REPO/$1"

    # Listing the objects is what makes the check readable in CI output.
    echo "== $2"
    arm-none-eabi-size "$REPO/$1"/CMakeFiles/stripseq.dir/src/*.obj
    check_helpers "$REPO/$1" "$2"
}

build "$BUILD_DIR" cortex-m4
build "$BUILD_DIR-m0" cortex-m0

exit "$status"
