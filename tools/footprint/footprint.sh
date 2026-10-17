#!/usr/bin/env bash
# Measure what the library costs on a bare-metal Cortex-M: the size of each of
# its objects on the target, and the flash and RAM it adds to an application,
# in each of the four configurations that change compiled code, on a Cortex-M0
# and a Cortex-M4.
#
#   tools/footprint/footprint.sh              # tables, into ./build-footprint
#   tools/footprint/footprint.sh --symbols    # and every symbol behind them
#
# Each build links tools/footprint/app.cpp against the library with
# --gc-sections, then reads the symbol table of the result. Attribution is by
# symbol rather than by input file, because an inline variable such as
# stripseq::no_sequence is emitted into every object that uses it and kept from
# whichever the linker reads first -- usually the application's.
#
#   library     anything in namespace stripseq, code and constants alike
#   helpers     the compiler's run-time routines from libgcc (__aeabi_*,
#               __udivmoddi4 ...), which the library's arithmetic calls
#   libc        memcpy and memset, which the compiler emits for copies and
#               clearing loops
#   build()     SequenceBuilder at run time, which is header-only and so
#               counted where app.cpp instantiates it
#
# Needs CMake 3.24 or later for --fresh, like scripts/cross-check.sh.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT="$REPO/build-footprint"

SYMBOLS=0
[[ "${1:-}" == "--symbols" ]] && SYMBOLS=1

CORES="cortex-m0 cortex-m4"

# name:white:gamma, named as the host variants are.
CONFIGS="default:0:1 nogamma:0:0 rgbw:1:1 rgbw_nogamma:1:0"

# build <dir> <cpu> <white> <gamma>. --fresh for the same reason as in
# cross-check.sh: the toolchain's flags are read on the first configure only.
build() {
    cmake --fresh -S "$SCRIPT_DIR" -B "$1" -G Ninja \
          -DCMAKE_TOOLCHAIN_FILE="$REPO/cmake/arm-none-eabi.cmake" \
          -DSTRIPSEQ_CROSS_CPU="$2" \
          -DSTRIPSEQ_WHITE_CHANNEL="$3" \
          -DSTRIPSEQ_GAMMA="$4" > /dev/null
    cmake --build "$1" > /dev/null
}

# symbols <elf>: one line per symbol with a size, as "class section size name".
#
# --size-sort, because it is what makes nm work out a size for a symbol the
# object does not record one for: libgcc's hand-written assembly, such as
# __aeabi_uidivmod, has none, and would otherwise count as nothing. The
# section comes from the address, against the ranges `size -A` reports.
# Functions sharing an address are aliases -- a constructor's complete and
# base-object entries, __aeabi_lmul and __muldi3 -- and are counted once.
# Data symbols beginning __ are the linker's markers, such as __data_start,
# which share an address with a real object; they are dropped.
symbols() {
    { arm-none-eabi-size -A -d "$1"; echo --; arm-none-eabi-nm -C -S --size-sort "$1"; } | awk '
        function hex(s,   n, i) {
            for (i = 1; i <= length(s); ++i)
                n = n * 16 + index("0123456789abcdef", tolower(substr(s, i, 1))) - 1
            return n
        }
        $0 == "--" { in_nm = 1; next }
        !in_nm {
            if ($1 ~ /^\.(text|rodata|data|bss)$/) {
                ++sections; sec[sections] = $1; lo[sections] = $3; hi[sections] = $3 + $2
            }
            next
        }
        {
            addr = hex($1); size = hex($2)
            name = $0; sub(/^[^ ]+ [^ ]+ [^ ]+ /, "", name)
            section = ""
            for (i = 1; i <= sections; ++i)
                if (addr >= lo[i] && addr < hi[i]) section = sec[i]
            if (section == "" || size == 0) next
            if (section == ".text" && seen[addr]++) next
            if (section != ".text" && name ~ /^__/) next
            if (name ~ /^stripseq::/)                class = "library"
            else if (name ~ /^(memcpy|memset)$/)     class = "libc"
            else if (name ~ /^__/)                   class = "helpers"
            else if (name ~ /^build_from_input\(/)   class = "build"
            else                                     class = "app"
            print class, section, size, name
        }'
}

# sum <class> <section regex> on symbols() output.
sum() {
    awk -v c="$1" -v s="$2" '$1 == c && $2 ~ s { n += $3 } END { print n + 0 }'
}

# size_of <object name> on symbols() output.
size_of() {
    awk -v o="$1" '$1 == "app" && $4 == o { print $3 }'
}

mkdir -p "$OUT"
FLASH_ROWS=""
OBJECT_ROWS=""

for cpu in $CORES; do
    for config in $CONFIGS; do
        IFS=: read -r name white gamma <<< "$config"
        dir="$OUT/$cpu-$name"
        echo "building $cpu $name ..." >&2
        build "$dir" "$cpu" "$white" "$gamma"
        syms="$(symbols "$dir/footprint")"

        code=$(sum library '^\.text$' <<< "$syms")
        consts=$(sum library '^\.rodata$' <<< "$syms")
        ram=$(sum library '^\.(data|bss)$' <<< "$syms")
        helpers=$(sum helpers '^\.text$' <<< "$syms")
        libc=$(sum libc '^\.text$' <<< "$syms")
        builder=$(sum build '^\.text$' <<< "$syms")
        FLASH_ROWS+="| $cpu | $name | $code | $consts | $ram | $helpers | $libc | $builder |"$'\n'

        # Object sizes depend on the white channel only, and are the same on
        # both cores, which are both 32-bit; one row per channel setting.
        if [[ "$cpu" == cortex-m4 && "$gamma" == 1 ]]; then
            OBJECT_ROWS+="| $([[ $white == 1 ]] && echo RGBW || echo RGB)"
            OBJECT_ROWS+=" | $(size_of kComet <<< "$syms")"
            OBJECT_ROWS+=" | $(size_of sequencer <<< "$syms")"
            OBJECT_ROWS+=" | $(size_of segment <<< "$syms")"
            OBJECT_ROWS+=" | $(size_of strip <<< "$syms") |"$'\n'
        fi

        if (( SYMBOLS )); then
            echo
            echo "### $cpu $name"
            echo
            echo '```'
            awk '$1 != "app"' <<< "$syms" | sort -k3,3nr \
                | awk '{ printf "%-8s %-8s %6d  ", $1, $2, $3; $1 = $2 = $3 = ""; sub(/^ +/, ""); print }'
            echo '```'
        fi
    done
done

cat <<EOF

## Objects, in bytes, at the default capacities

| Build | Sequence | Sequencer | Segment | Strip |
| --- | --: | --: | --: | --: |
$OBJECT_ROWS
## Flash and RAM, in bytes

Library code and constants are flash; library RAM is static data of the library's own, outside any object the application declares. The libgcc helpers are those reached from the library or from build().

| Core | Configuration | Library code | Library constants | Library RAM | libgcc helpers | memcpy, memset | build() at run time |
| --- | --- | --: | --: | --: | --: | --: | --: |
$FLASH_ROWS
EOF
