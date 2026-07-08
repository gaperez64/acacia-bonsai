#!/bin/bash
#
# Wrapper around the compiled acacia-bonsai binary for the CLI Docker image.
# Selects one of the precompiled configurations and forwards the rest of the
# arguments to the underlying binary. Optionally reads a TLSF spec from stdin
# (--tlsf), translates it via tlsf-tools, and feeds the resulting LTL formula
# and input/output partition to acacia-bonsai.

set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
mapfile -t CONFIGS < <(python3 "$REPO_ROOT/scripts/acacia-config.py" list-group docker_default)

usage() {
    cat <<EOF
Usage: $0 <config_name> [--tlsf] [acacia-bonsai arguments...]

  --tlsf            Read a TLSF spec from stdin, translate it via tlsf-tools,
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
    TLSF2LTL=$(command -v tlsf2ltl || true)
    TLSFINFO=$(command -v tlsfinfo || true)
    if [ -z "$TLSF2LTL" ] || [ -z "$TLSFINFO" ]; then
        echo "Error: --tlsf requires tlsf-tools in PATH (tlsf2ltl and tlsfinfo)." >&2
        exit 3
    fi

    SPEC=$(mktemp)
    LTL=$(mktemp)
    trap 'rm -f "$SPEC" "$LTL"' EXIT

    # tlsf-tools accepts a file path; slurp stdin into a temp file so the
    # converter and metadata query see the same input.
    cat > "$SPEC"

    # Write the LTL to a file and pass it via -F: some TLSF specs translate
    # to formulas too long to fit on a command line.
    TLSF2LTL_ARGS=(--format ltlxba --parenthesize --overwrite-target Mealy)
    TLSF2LTL_ARGS+=(--output "$LTL" "$SPEC")

    "$TLSF2LTL" "${TLSF2LTL_ARGS[@]}" || {
        echo "Error: tlsf2ltl failed to translate the TLSF input from stdin" >&2
        exit 4
    }

    ins=$("$TLSFINFO" --expanded-ins "$SPEC") || {
        echo "Error: tlsfinfo failed to extract TLSF inputs" >&2
        exit 4
    }
    outs=$("$TLSFINFO" --expanded-outs "$SPEC") || {
        echo "Error: tlsfinfo failed to extract TLSF outputs" >&2
        exit 4
    }
    ins=${ins//$'\n'/}
    ins=${ins//$'\r'/}
    ins=${ins//$'\t'/}
    ins=${ins// /}
    outs=${outs//$'\n'/}
    outs=${outs//$'\r'/}
    outs=${outs//$'\t'/}
    outs=${outs// /}

    exec "$BINARY" "${PASS_ARGS[@]}" -F "$LTL" -i "$ins" -o "$outs"
fi

exec "$BINARY" "${PASS_ARGS[@]}"
