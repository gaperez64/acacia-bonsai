#!/bin/bash
set -e

NPROC=$(nproc)

# --- Compile and install Spot ---
if pkg-config --exists libspot 2>/dev/null; then
    echo "Spot already installed, skipping."
else
    echo "Compiling Spot (using $NPROC cores)..."
    cd /opt/spot/spot-2.14.4
    ./configure --enable-max-accsets=64 --disable-python
    make -j"$NPROC"
    make install
    ldconfig
    cd /opt/acacia-bonsai
    echo "Spot installed successfully."
fi

# --- Compile acacia-bonsai configurations ---
OPT='-march=native -Ofast -flto -fuse-linker-plugin -pipe -DNO_VERBOSE -DNDEBUG'

BEST_BASE='-DDEFAULT_KMIN=2 -DDEFAULT_KINC=3 -DDEFAULT_UNREAL_X=UNREAL_X_BOTH
-DAUT_PREPROCESSOR=aut_preprocessors::standard
-DBOOLEAN_STATES=boolean_states::forward_saturation
-DIOS_PRECOMPUTER=ios_precomputers::powset
-DINPUT_PICKER=input_pickers::critical
-DSIMD_IS_MAX=false
-DARRAY_AND_BITSET_DOWNSET_IMPL=vector_backed
-DVECTOR_AND_BITSET_DOWNSET_IMPL=vector_backed
-DDECOMPOSE_SPEC=0'

declare -A CONFIGS
CONFIGS[best_decomp_kdtree_mona]="$BEST_BASE -DARRAY_AND_BITSET_DOWNSET_IMPL=kdtree_backed -DVECTOR_AND_BITSET_DOWNSET_IMPL=kdtree_backed -DIOS_PRECOMPUTER=ios_precomputers::mona -DDECOMPOSE_SPEC=1"
CONFIGS[best_decomp_kdtree_mona_no_bitsets]="$BEST_BASE -DARRAY_AND_BITSET_DOWNSET_IMPL=kdtree_backed -DVECTOR_AND_BITSET_DOWNSET_IMPL=kdtree_backed -DIOS_PRECOMPUTER=ios_precomputers::mona -DDECOMPOSE_SPEC=1 -DNO_ARRAY_CAP_MAX -DUSE_BOOLVEC_OVER_BITSET"
CONFIGS[best_decomp_mona_no_bitsets]="$BEST_BASE -DDECOMPOSE_SPEC=1 -DIOS_PRECOMPUTER=ios_precomputers::mona -DNO_ARRAY_CAP_MAX -DUSE_BOOLVEC_OVER_BITSET"
CONFIGS[best_downset_vector_or_kdtree]="$BEST_BASE -DARRAY_AND_BITSET_DOWNSET_IMPL=vector_or_kdtree_backed -DVECTOR_AND_BITSET_DOWNSET_IMPL=vector_or_kdtree_backed"

cd /opt/acacia-bonsai

for name in "${!CONFIGS[@]}"; do
    build="build_$name"
    if [ -d "$build" ]; then
        echo "$build already exists, skipping (remove to rebuild)."
        continue
    fi
    flags="${CONFIGS[$name]}"
    echo "Building $name..."
    CXXFLAGS="$OPT $flags" meson setup "$build" --buildtype=release
    meson compile -C "$build"
    echo "$name compiled successfully."
done

echo ""
echo "All configurations compiled. Use ./acacia-bonsai.sh <config> to run."
