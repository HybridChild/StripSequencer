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
# to be 64-bit, arithmetic the core cannot do in an instruction, and anything
# initialised at run time rather than constant-initialised. Nothing is linked
# and nothing is run: on a target with no startup file and no linker script,
# compiling and reading the objects is the whole signal.
#
# Needs CMake 3.24 or later, for --fresh, which is newer than the 3.21 the
# library asks of a consumer: the script is a developer's tool, not part of
# what a consumer builds.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${1:-build-cross}"

# The helpers the design accepts. 32-bit division is a call on a Cortex-M0 and
# fine anywhere; memcpy and memset are the compiler's own, for copies and
# clearing loops. A signed division, any 64-bit division, a float routine,
# operator new or a libc call fails the check, by name.
ALLOWED="__aeabi_uidiv __aeabi_uidivmod memcpy memset"

status=0

# check_helpers <build dir> <cpu>: every symbol an object leaves undefined is either
# defined by another of the library's objects or on the allowlist.
check_helpers() {
    local objs=("$1"/CMakeFiles/stripseq.dir/src/*.obj)
    local defined
    defined="$(arm-none-eabi-nm --defined-only "${objs[@]}" | awk 'NF == 3 { print $3 }')"
    for obj in "${objs[@]}"; do
        for sym in $(arm-none-eabi-nm -u "$obj" | awk '{ print $NF }'); do
            grep -qxF "$sym" <<< "$defined" && continue
            [[ " $ALLOWED " == *" $sym "* ]] && continue
            echo "error: $2: $(basename "$obj") calls $(arm-none-eabi-c++filt "$sym"), which is not on the allowlist" >&2
            status=1
        done
    done
}

# check_init <build dir> <cpu>: no object initialises anything at run time.
# The allowlist cannot see either form. A file-scope initialiser that calls
# nothing, or calls one of the library's own functions, leaves no undefined
# symbol; only its section in .init_array shows it -- or in .preinit_array or
# .ctors, which other toolchains emit for the same thing. A function-local
# static leaves __cxa_guard_acquire undefined only under thread-safe statics,
# which a consumer may turn off; its guard variable, a _ZGV symbol, is there
# either way.
check_init() {
    local obj
    for obj in "$1"/CMakeFiles/stripseq.dir/src/*.obj; do
        while read -r section size; do
            echo "error: $2: $(basename "$obj") has a $((16#$size))-byte $section, code that runs before main" >&2
            status=1
        done < <(arm-none-eabi-objdump -h "$obj" |
                 awk '$2 ~ /^\.(init_array|preinit_array|ctors)/ && $3 !~ /^0+$/ { print $2, $3 }')
        for sym in $(arm-none-eabi-nm "$obj" | awk '$NF ~ /^_ZGV/ { print $NF }'); do
            echo "error: $2: $(basename "$obj") has a $(arm-none-eabi-c++filt "$sym"), a static initialised on first use" >&2
            status=1
        done
    done
}

# build <build dir> <preset> <cpu>: the preset in CMakePresets.json that
# configures the library alone for <cpu>, into <build dir> rather than its own.
# --fresh because the toolchain's flags are cache defaults, read on the first
# configure only: a tree configured before a flag changed would otherwise keep
# checking without it.
build() {
    (cd "$REPO" && cmake --fresh --preset "$2" -B "$REPO/$1")
    cmake --build "$REPO/$1"

    # Listing the objects is what makes the check readable in CI output.
    echo "== $3"
    arm-none-eabi-size "$REPO/$1"/CMakeFiles/stripseq.dir/src/*.obj
    check_helpers "$REPO/$1" "$3"
    check_init "$REPO/$1" "$3"
}

build "$BUILD_DIR" cross-m4 cortex-m4
build "$BUILD_DIR-m0" cross-m0 cortex-m0

exit "$status"
