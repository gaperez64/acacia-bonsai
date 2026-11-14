#!/bin/bash
set -euo pipefail

NPROC=$(nproc)
SPOT_VERSION=${SPOT_VERSION:-2.16}

# --- Compile and install Spot ---
if pkg-config --exists libspot 2>/dev/null; then
    echo "Spot already installed, skipping."
else
    echo "Compiling Spot (using $NPROC cores)..."
    cd "/opt/spot/spot-$SPOT_VERSION"
    ./configure --enable-max-accsets=64 --disable-python
    make -j"$NPROC"
    make install
    ldconfig
    cd /opt/acacia-bonsai
    echo "Spot installed successfully."
fi

cd /opt/acacia-bonsai

mapfile -t CONFIG_NAMES < <(python3 scripts/acacia-config.py list-group docker_default)
failures=()

meson_args_for_config() {
    local name=$1
    python3 scripts/acacia-config.py meson-args "$name"
}

profile_args=$(python3 scripts/acacia-build.py profile-args normal)
read -r -a setup_args <<< "$profile_args"

build_identity_for() {
    python3 scripts/acacia-build.py identity "$1" --profile normal --build-dir "$2"
}

for name in "${CONFIG_NAMES[@]}"; do
    build="build_$name"
    if [ -d "$build" ]; then
        if ! identity=$(build_identity_for "$name" "$build") || \
           ! cmp -s <(printf '%s\n' "$identity") "$build/.acacia-config.json"; then
            echo "$build has missing or stale build identity; remove to rebuild." >&2
            failures+=("$name (build identity)")
            continue
        fi
        echo "$build has matching build identity."
    else
        if ! meson_args=$(meson_args_for_config "$name"); then
            failures+=("$name (configuration arguments)")
            continue
        fi
        read -r -a preset_args <<< "$meson_args"
        echo "Building $name..."
        if ! meson setup "$build" "${setup_args[@]}" "${preset_args[@]}"; then
            failures+=("$name (Meson setup)")
            rm -rf "$build"  # so a rerun retries instead of skipping it
            continue
        fi
        if ! identity=$(build_identity_for "$name" "$build"); then
            failures+=("$name (build identity)")
            continue
        fi
        printf '%s\n' "$identity" > "$build/.acacia-config.json"
    fi
    if ! meson compile -C "$build"; then
        failures+=("$name (Meson compile)")
        rm -rf "$build"
        continue
    fi
    echo "$name compiled successfully."
done

if ((${#failures[@]})); then
    echo "Compilation failed: ${failures[*]}" >&2
    exit 1
fi

echo ""
echo "All configurations compiled. Use ./scripts/acacia-bonsai.sh <config> to run."
echo "Tip: snapshot this container to reuse the compiled binaries later, e.g.:"
echo "  docker commit <container> ghcr.io/gaperez64/acacia-bonsai:compiled"
