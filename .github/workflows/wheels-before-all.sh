#!/bin/bash
# Run inside the manylinux_2_28 container by cibuildwheel, once per arch
# before any wheel is built. Installs the build toolchain needed for the
# acacia-boomslang wheel (SWIG, GCC 14 for C++23) and compiles Spot
# from source into /opt/spot_install. The install prefix matches what
# python-test.yml and Dockerfile.boomslang use so PKG_CONFIG_PATH stays
# consistent across the project.
#
# Spot's Python bindings are disabled here: the wheel bundles libspot.so
# via auditwheel and exposes only the acacia-bonsai SWIG module; Spot's
# own pyspot is a separate PyPI package ("spot").

set -euo pipefail

: "${SPOT_VERSION:=2.14.4}"

echo "== Installing build dependencies =="
yum install -y --setopt=install_weak_deps=False \
    autoconf automake libtool bison flex \
    git swig wget \
    gcc-toolset-14 gcc-toolset-14-gcc gcc-toolset-14-gcc-c++

# Use GCC 14 for both Spot and the wheel compilation — the acacia-bonsai
# tree is C++23 and the wheel's extension_module pulls in project headers.
# shellcheck disable=SC1091
source /opt/rh/gcc-toolset-14/enable
export CC=gcc
export CXX=g++

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
make -j"$(nproc)"
make install

echo "/opt/spot_install/lib" > /etc/ld.so.conf.d/spot.conf
/sbin/ldconfig

echo "== Spot installed =="
pkg-config --modversion libspot
