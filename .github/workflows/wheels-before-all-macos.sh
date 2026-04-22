#!/bin/bash
# Runs directly on the macos-15 runner (no container) by cibuildwheel,
# once per arch before any wheel is built. Installs GCC 14 (needed for the
# experimental/simd header in the posets subproject, which Apple Clang's
# libc++ does not provide) and compiles Spot from source into /opt/spot_install.
#
# MACOSX_DEPLOYMENT_TARGET=15.0 is forced because Homebrew's libstdc++.6.dylib
# on this runner carries a min-target of 15.0; the wheel must declare the same
# floor so delocate will accept the bundled runtime. Spot's dylib is also built
# at 15.0 for consistency.

set -euo pipefail

: "${SPOT_VERSION:=2.14.4}"

echo "== Installing build dependencies =="
brew install autoconf automake libtool bison flex pkg-config git swig wget gcc@14

# bison and flex are keg-only on macOS (avoids shadowing the system tools);
# prepend their Homebrew prefix so Spot's configure uses the current versions.
export PATH="/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/flex/bin:/opt/homebrew/bin:$PATH"
export CC=/opt/homebrew/bin/gcc-14
export CXX=/opt/homebrew/bin/g++-14

# Match Homebrew libstdc++.6.dylib's minimum target so delocate accepts the wheel.
export MACOSX_DEPLOYMENT_TARGET=15.0

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
