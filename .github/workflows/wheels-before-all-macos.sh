#!/bin/bash
# Runs directly on the macos-15 runner (no container) by cibuildwheel,
# once per arch before any wheel is built. Installs SWIG and compiles Spot
# from source into /opt/spot_install using Apple Clang (the system default).
#
# We intentionally avoid Homebrew GCC: its libstdc++ is compiled against the
# runner OS (macOS 15) and delocate rejects it when repairing a wheel tagged
# macosx_11_0_arm64. Apple Clang's runtime is a system library that delocate
# always excludes, so only libspot.dylib / libbddx.dylib end up in the wheel.
#
# MACOSX_DEPLOYMENT_TARGET=11.0 is set explicitly so libspot.dylib carries
# the same minimum-OS guarantee as the wheel itself.

set -euo pipefail

: "${SPOT_VERSION:=2.14.4}"

echo "== Installing build dependencies =="
brew install autoconf automake libtool bison flex pkg-config git swig wget

# bison and flex are keg-only on macOS (avoids shadowing the system tools);
# prepend their Homebrew prefix so Spot's configure uses the current versions.
export PATH="/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/flex/bin:/opt/homebrew/bin:$PATH"

# Build libspot.dylib targeting the same minimum OS as the arm64 wheel tag.
export MACOSX_DEPLOYMENT_TARGET=11.0

echo "== Building Spot ${SPOT_VERSION} =="
workdir=$(mktemp -d)
cd "$workdir"
wget -q "http://www.lrde.epita.fr/dload/spot/spot-${SPOT_VERSION}.tar.gz"
tar -xzf "spot-${SPOT_VERSION}.tar.gz"
cd "spot-${SPOT_VERSION}"

./configure \
    --prefix=/opt/spot_install \
    --enable-max-accsets=64 \
    --disable-python \
    --disable-static \
    --disable-doxygen
make -j"$(sysctl -n hw.logicalcpu)"
sudo make install

echo "== Spot installed =="
PKG_CONFIG_PATH=/opt/spot_install/lib/pkgconfig pkg-config --modversion libspot
