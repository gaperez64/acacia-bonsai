#!/bin/bash
#
# Same options as acacia-bonsai.sh, but always runs the underlying binary
# with synthesis enabled (-s) and, on REALIZABLE, prints the synthesized
# AIG controller (AAG format) to stdout. acacia-bonsai's own diagnostic
# stdout (the REALIZABLE/UNREALIZABLE verdict) is redirected to stderr so
# the AAG is the only thing on stdout — convenient for piping into another
# tool from outside the container, e.g.:
#
#     cat spec.tlsf | docker run -i ... \
#         /opt/acacia-bonsai/scripts/acacia-synthesis.sh \
#         <config> --tlsf > out.aag
#
# Exit code matches the underlying binary:
#   0  REALIZABLE   (controller printed on stdout)
#   1  UNREALIZABLE (no output)
#   2  UNKNOWN
#   3  ERROR

set -e

CONFIGS=(
    best_decomp_kdtree_mona
    best_decomp_kdtree_mona_no_bitsets
    best_decomp_mona_no_bitsets
    best_downset_vector_or_kdtree
)

usage() {
    cat <<EOF
Usage: $0 <config_name> [--tlsf] [acacia-bonsai arguments...]

Available configurations:
EOF
    for c in "${CONFIGS[@]}"; do
        echo "  $c"
    done
    echo "" >&2
    echo "Note: do NOT pass -s yourself; this script supplies it." >&2
    exit 1
}

if [ $# -lt 1 ]; then
    usage
fi

CONFIG="$1"
shift

valid=false
for c in "${CONFIGS[@]}"; do
    if [ "$c" = "$CONFIG" ]; then
        valid=true
        break
    fi
done

if [ "$valid" = false ]; then
    echo "Error: unknown configuration '$CONFIG'" >&2
    echo "" >&2
    usage
fi

# Reject a user-supplied -s; we own that flag.
for arg in "$@"; do
    if [ "$arg" = "-s" ]; then
        echo "Error: -s is supplied by acacia-synthesis.sh; do not pass it yourself." >&2
        exit 3
    fi
done

WRAPPER="$(dirname "$0")/acacia-bonsai.sh"
if [ ! -x "$WRAPPER" ]; then
    echo "Error: acacia-bonsai.sh not found next to acacia-synthesis.sh" >&2
    exit 3
fi

OUT_AAG=$(mktemp --suffix=.aag)
trap 'rm -f "$OUT_AAG"' EXIT

# Run the wrapper with -s pointing at our temp file. Send acacia's own stdout
# (which contains the REALIZABLE/UNREALIZABLE line) to stderr so callers can
# pipe stdout straight into a downstream tool.
#
# `set -e` would kill us on a non-zero exit code, but UNREALIZABLE/UNKNOWN are
# legitimate outcomes we need to propagate, so capture the status manually.
status=0
"$WRAPPER" "$CONFIG" "$@" -s "$OUT_AAG" 1>&2 || status=$?

# Only on REALIZABLE (exit code 0) is an AAG file produced.
if [ "$status" -eq 0 ] && [ -s "$OUT_AAG" ]; then
    cat "$OUT_AAG"
fi

exit "$status"
