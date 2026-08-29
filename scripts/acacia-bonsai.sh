#!/bin/bash
#
# Wrapper around the compiled acacia-bonsai binary for the CLI Docker image.
# Selects one of the precompiled configurations and forwards the rest of the
# arguments to the underlying binary. Optionally reads a TLSF spec from stdin
# (--tlsf) through acacia-bonsai's linked native frontend.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
mapfile -t CONFIGS < <(python3 "$REPO_ROOT/scripts/acacia-config.py" list-group docker_default)

usage() {
    cat <<EOF
Usage: $0 <config_name> [--tlsf] [acacia-bonsai arguments...]

  --tlsf            Read a TLSF spec from stdin through the linked native
                    frontend. Suitable for piping a spec into the container,
                    e.g.:
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

BINARY="$REPO_ROOT/build_${CONFIG}/src/acacia-bonsai"

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
    exec "$BINARY" "${PASS_ARGS[@]}" -T /dev/stdin
fi

exec "$BINARY" "${PASS_ARGS[@]}"
