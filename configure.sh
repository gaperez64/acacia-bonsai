#!/bin/zsh -f

PROG=$0

declare -A targets
targets=([starexec]='CXXFLAGS="-march=sandybridge -O3 -flto -fuse-linker-plugin -pipe" meson --buildtype=release'
         [debug]='meson'
         [optimized]='CXXFLAGS="-march=native -O3 -flto -fuse-linker-plugin -pipe -DNO_VERBOSE" meson --buildtype=release')


usage () {
    echo "usage: $PROG TARGET BUILDDIR [moreopts...]"
    echo "TARGET is one of:"
    for t in ${(k)targets}; do
        echo "- $t"
    done
    exit 1
}

(( $# < 2 )) && usage
target=$1
[[ $targets[$target] ]] || usage
shift
eval $targets[$target] "$@"
