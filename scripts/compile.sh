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

# --- Compile acacia-bonsai configurations ---
OPT='-march=native -Ofast -flto -fuse-linker-plugin -pipe -DNO_VERBOSE -DNDEBUG'

BEST_BASE='-DDEFAULT_KMIN=2 -DDEFAULT_KINC=3 -DDEFAULT_UNREAL_X=UNREAL_X_BOTH
-DAUT_PREPROCESSOR=aut_preprocessors::standard
-DACACIA_ENABLE_AUT_PREPROCESSOR_SURELY_LOSING=0
-DACACIA_ENABLE_AUT_PREPROCESSOR_STANDARD=1
-DBOOLEAN_STATES=boolean_states::forward_saturation
-DIOS_PRECOMPUTER=ios_precomputers::powset
-DACACIA_ENABLE_IOS_PRECOMPUTER_STANDARD=0
-DACACIA_ENABLE_IOS_PRECOMPUTER_POWSET=1
-DINPUT_PICKER=input_pickers::critical
-DACACIA_ENABLE_INPUT_PICKER_CRITICAL_PQ=0
-DACACIA_ENABLE_INPUT_PICKER_CRITICAL=1
-DSIMD_IS_MAX=false
-DARRAY_AND_BITSET_DOWNSET_IMPL=vector_backed
-DVECTOR_AND_BITSET_DOWNSET_IMPL=vector_backed
-DDECOMPOSE_SPEC=0'

declare -A CONFIGS
MONA='-DIOS_PRECOMPUTER=ios_precomputers::mona -DACACIA_ENABLE_IOS_PRECOMPUTER_POWSET=0 -DACACIA_ENABLE_IOS_PRECOMPUTER_STANDARD=0 -DACACIA_ENABLE_IOS_PRECOMPUTER_MONA=1'
CONFIGS[best_decomp_mona]="$BEST_BASE $MONA -DDECOMPOSE_SPEC=1"
CONFIGS[best_decomp_kdtree_mona]="$BEST_BASE -DARRAY_AND_BITSET_DOWNSET_IMPL=kdtree_backed -DVECTOR_AND_BITSET_DOWNSET_IMPL=kdtree_backed $MONA -DDECOMPOSE_SPEC=1"
CONFIGS[base_iosprecom_mona]="$MONA"
CONFIGS[best_decomp_sharingtrie_mona]="$BEST_BASE -DARRAY_AND_BITSET_DOWNSET_IMPL=sharingtrie_backed -DVECTOR_AND_BITSET_DOWNSET_IMPL=sharingtrie_backed $MONA -DDECOMPOSE_SPEC=1"

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
echo "All configurations compiled. Use ./scripts/acacia-bonsai.sh <config> to run."
echo "Tip: snapshot this container to reuse the compiled binaries later, e.g.:"
echo "  docker commit <container> ghcr.io/gaperez64/acacia-bonsai:compiled"
