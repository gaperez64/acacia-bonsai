#!/bin/bash

if [ "$EUID" -ne 0 ]
  then echo "Please run as root, e.g., sudo $0"
  exit
fi

THOME="$PWD" # Hopefully /home/tacas23
LOG="$THOME/install.log"

echo "Logging to $LOG."

run () {
  echo "-> Running $@"
  if ! "$@" &>> $LOG; then
    echo "  FAIL: $LOG will have more."
    exit 1
  fi
}

echo "Install some apt dependencies."
## apt-get install meson zsh libboost-dev libglib2.0-dev python3 python-is-python3 python3-matplotlib
run dpkg --force-all -i $THOME/packages/*.deb

echo "Install extra dependencies that are not found in apt."

echo "Strix (from https://github.com/meyerphi/strix/releases/download/21.0.0/strix-21.0.0-1-amd64.deb)"
cd $THOME/deps/
run dpkg -i --ignore-depends=glibc ./strix-21.0.0-1-amd64.deb
run sed -i 's/^Depends: .*glibc[^,]*,/Depends: /' /var/lib/dpkg/status

echo "Spot (from http://www.lrde.epita.fr/dload/spot/spot-2.10.6.tar.gz)"
cd $THOME/deps/spot/
mkdir -p _build
cd _build
run ../configure --prefix=/usr --disable-devel --disable-debug --enable-optimizations --disable-python --disable-static --disable-doxygen
run make
run make install

echo "Old python-graph for Acacia+ (from https://github.com/Shoobx/python-graph)"
cd $THOME/deps/python-graph
run make install-core

echo "Build & test Acacia-Bonsai"
cd $THOME/acacia-bonsai
rm -fR build
run meson build
cd build
run meson test --suite=ab/tiny
run meson test --suite=strix/tiny
run meson test --suite=aca+/tiny
run meson test --suite=ltlsynt/tiny
cd ..
rm -fR build
echo "All basic tests succeeded; Acacia-Bonsai is at $THOME/acacia-bonsai, ready for building."
