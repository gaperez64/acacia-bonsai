#!/bin/bash

CONFIGS=(
    best_decomp_kdtree_mona
    best_decomp_kdtree_mona_no_bitsets
    best_decomp_mona_no_bitsets
    best_downset_vector_or_kdtree
)

usage() {
    echo "Usage: $0 <config_name> [acacia-bonsai arguments...]"
    echo ""
    echo "Available configurations:"
    for c in "${CONFIGS[@]}"; do
        echo "  $c"
    done
    exit 1
}

if [ $# -lt 1 ]; then
    usage
fi

CONFIG="$1"
shift

# Validate config name
valid=false
for c in "${CONFIGS[@]}"; do
    if [ "$c" = "$CONFIG" ]; then
        valid=true
        break
    fi
done

if [ "$valid" = false ]; then
    echo "Error: unknown configuration '$CONFIG'"
    echo ""
    usage
fi

BINARY="/opt/acacia-bonsai/build_${CONFIG}/src/acacia-bonsai"

if [ ! -x "$BINARY" ]; then
    echo "Error: binary not found at $BINARY"
    echo "Did you run ./compile.sh first?"
    exit 2
fi

# If any argument is a .tlsf file, translate it via syfco into the LTL
# formula and the input/output partition that acacia-bonsai expects.
# We also strip a preceding "-F" so callers can use either
#     acacia-bonsai.sh <cfg> spec.tlsf
# or  acacia-bonsai.sh <cfg> -F spec.tlsf [other flags]
TLSF_FILE=""
PASS_ARGS=()
i=1
while [ $i -le $# ]; do
    arg="${!i}"
    next_i=$((i + 1))
    next_arg="${!next_i:-}"
    if [ "$arg" = "-F" ] && [[ "$next_arg" == *.tlsf ]]; then
        TLSF_FILE="$next_arg"
        i=$((i + 2))
        continue
    fi
    if [[ "$arg" == *.tlsf ]]; then
        TLSF_FILE="$arg"
        i=$((i + 1))
        continue
    fi
    PASS_ARGS+=("$arg")
    i=$((i + 1))
done

if [ -n "$TLSF_FILE" ]; then
    SYFCO=$(command -v syfco)
    if [ -z "$SYFCO" ]; then
        echo "Error: '$TLSF_FILE' is a TLSF file but syfco was not found in PATH."
        echo "Install syfco (https://github.com/reactive-systems/syfco) or pass an LTL spec instead."
        exit 3
    fi

    PART=$(mktemp)
    trap 'rm -f "$PART"' EXIT

    LTL=$("$SYFCO" "$TLSF_FILE" -f ltlxba -m fully -pf "$PART") || {
        echo "Error: syfco failed to translate '$TLSF_FILE'"
        exit 4
    }

    ins=""
    outs=""
    while IFS= read -r line; do
        line=$(echo "$line" | sed 's/[[:space:]]*$//;s/[[:space:]]\+/,/g')
        head=${line/,*/}
        args=${line/$head,/}
        case "$head" in
            .inputs)  ins=$args ;;
            .outputs) outs=$args ;;
        esac
    done < "$PART"

    exec "$BINARY" "${PASS_ARGS[@]}" -f "$LTL" -i "$ins" -o "$outs"
fi

exec "$BINARY" "$@"
