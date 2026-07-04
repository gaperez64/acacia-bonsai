#!/bin/bash
set -e

NPROC=$(nproc)
SPOT_VERSION=${SPOT_VERSION:-2.15.1}

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

OPT='-march=native -Ofast -flto -fuse-linker-plugin -pipe -DNO_VERBOSE -DNDEBUG'
mapfile -t CONFIG_NAMES < <(python3 scripts/acacia-config.py list-group docker_default)

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
    meson_args=$(meson_args_for_config "$name")
    echo "Building $name..."
    CXXFLAGS="$OPT $CXXFLAGS" meson setup "$build" --buildtype=release $meson_args
    meson compile -C "$build"
    echo "$name compiled successfully."
done

echo ""
echo "All configurations compiled. Use ./scripts/acacia-bonsai.sh <config> to run."
echo "Tip: snapshot this container to reuse the compiled binaries later, e.g.:"
echo "  docker commit <container> ghcr.io/gaperez64/acacia-bonsai:compiled"
