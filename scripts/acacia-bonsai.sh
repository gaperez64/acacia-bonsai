#!/bin/bash
#
# Wrapper around the compiled acacia-bonsai binary for the CLI Docker image.
# Selects one of the precompiled configurations and forwards the rest of the
# arguments to the underlying binary. Optionally reads a TLSF spec from stdin
# (--tlsf), translates it via syfco, and feeds the resulting LTL formula and
# input/output partition to acacia-bonsai.

set -e

CONFIGS=(
    best_mona
    best_decomp_mona
    base_iosprecom_mona
    best_decomp_kdtree_mona
)

usage() {
    cat <<EOF
Usage: $0 <config_name> [--tlsf] [acacia-bonsai arguments...]

  --tlsf            Read a TLSF spec from stdin, translate it via syfco,
                    and pass the resulting LTL formula plus input/output
                    partition to acacia-bonsai. Suitable for piping a
                    spec into the container, e.g.:
                      cat spec.tlsf | docker run -i ... \\
                          /opt/acacia-bonsai/scripts/acacia-bonsai.sh \\
                          <config> --tlsf

Available configurations:
EOF
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
    echo "Error: unknown configuration '$CONFIG'" >&2
    echo "" >&2
    usage
fi

BINARY="/opt/acacia-bonsai/build_${CONFIG}/src/acacia-bonsai"

if [ ! -x "$BINARY" ]; then
    echo "Error: binary not found at $BINARY" >&2
    echo "Did you run scripts/compile.sh first?" >&2
    exit 2
fi

# Strip a single --tlsf flag (anywhere in the argument list) and remember
# whether we saw it. Everything else is passed through to the binary.
USE_TLSF=false
PASS_ARGS=()
for arg in "$@"; do
    if [ "$arg" = "--tlsf" ]; then
        USE_TLSF=true
    else
        PASS_ARGS+=("$arg")
    fi
done

if [ "$USE_TLSF" = true ]; then
    SYFCO=$(command -v syfco || true)
    if [ -z "$SYFCO" ]; then
        echo "Error: --tlsf requires syfco in PATH." >&2
        exit 3
    fi

    PART=$(mktemp)
    SPEC=$(mktemp)
    trap 'rm -f "$PART" "$SPEC"' EXIT

    # syfco wants a file path; slurp stdin into a temp file so we can also
    # extract the input/output partition with -pf.
    cat > "$SPEC"

    LTL=$("$SYFCO" "$SPEC" -f ltlxba -m fully -pf "$PART") || {
        echo "Error: syfco failed to translate the TLSF input from stdin" >&2
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

exec "$BINARY" "${PASS_ARGS[@]}"
