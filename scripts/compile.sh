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

for name in "${CONFIG_NAMES[@]}"; do
    build="build_$name"
    if [ -d "$build" ]; then
        echo "$build already exists, skipping (remove to rebuild)."
        continue
    fi
    if ! meson_args=$(meson_args_for_config "$name"); then
        failures+=("$name (configuration arguments)")
        continue
    fi
    echo "Building $name..."
    if ! meson setup "$build" --buildtype=release -Doptimization=3 -Db_lto=true -Ddebug=false -Db_ndebug=true -Dacacia_compiler_profile=release $meson_args; then
        failures+=("$name (Meson setup)")
        rm -rf "$build"  # so a rerun retries instead of skipping it
        continue
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
