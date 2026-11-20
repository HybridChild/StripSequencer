# Sourced, not run: the arguments scripts/sample-build.sh and
# scripts/sample-flash.sh share, and the build tree they name. Both read it from
# here so that a flash always finds the tree the matching build wrote.
#
#   [sample] [-b board] [--rgb] [-p] [anything else]
#
# The sample is the one word that is not a flag, and may come anywhere among
# the flags. -b names the board as west build's own -b does. They default to
# the bring-up sample on the bench board. The first argument that is none of
# these stops the parse and is left in "$@" for the caller, -- and all after it
# included.
#
# Sets SAMPLE, BOARD, WHITE (y or n), PRISTINE (auto or always) and BUILD_DIR,
# and expects REPO to be set. The tree is build-zephyr-<sample>-<board>, with
# -rgb appended for the white channel off, for every sample alike, so a tree's
# name says exactly what is in it. A board's qualifiers keep their slashes out
# of the name as underscores.

SAMPLE=
BOARD=nucleo_h753zi
WHITE=y
PRISTINE=auto

while [[ $# -gt 0 ]]; do
    case "$1" in
    -b)
        if [[ $# -lt 2 || "$2" == -* ]]; then
            echo "-b needs a board." >&2
            exit 1
        fi
        # Not checked here: west knows every board, and says so.
        BOARD="$2"
        shift 2
        ;;
    -p)    PRISTINE=always; shift ;;
    --rgb) WHITE=n; shift ;;
    -*)    break ;;
    *)
        # A word that names no sample is a typo, never something for west,
        # whose own arguments here are all flags.
        if [[ -n "$SAMPLE" ]]; then
            echo "Two samples named: '$SAMPLE' and '$1'." >&2
            exit 1
        fi
        if [[ ! -d "$REPO/samples/$1" ]]; then
            echo "No sample '$1' under samples/." >&2
            exit 1
        fi
        SAMPLE="$1"
        shift
        ;;
    esac
done
SAMPLE="${SAMPLE:-strip_demo}"

BUILD_DIR="$REPO/build-zephyr-$SAMPLE-${BOARD//\//_}"
# An if, never `[[ ]] &&`: as the file's last command, a false test would be
# what `source` returns, and the caller's set -e would end it without a word.
if [[ "$WHITE" == n ]]; then
    BUILD_DIR+="-rgb"
fi
