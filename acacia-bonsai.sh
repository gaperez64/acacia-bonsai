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

exec "$BINARY" "$@"
