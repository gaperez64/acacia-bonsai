#!/bin/zsh -f

mkdir -p _bm-logs

opt='-march=native -O3 -flto -fuse-linker-plugin -pipe -DNO_VERBOSE'

declare -A confs

# These all differ from the standard configuration in configuration.hh by /one/
# option.
confs=(
    [kmin5_kinc2]="-DDEFAULT_KMIN=5 -DDEFAULT_KINC=2"
    [kmin5_kinc1]="-DDEFAULT_KMIN=5 -DDEFAULT_KINC=1"
    [kmin2_kinc1]="-DDEFAULT_KMIN=2 -DDEFAULT_KINC=1"
    [kmin2_kinc3]="-DDEFAULT_KMIN=2 -DDEFAULT_KINC=3"
    [x_is_form]="-DDEFAULT_UNREAL_X=UNREAL_X_FORMULA"
    [x_is_aut]="-DDEFAULT_UNREAL_X=UNREAL_X_AUTOMATON"
    [nosimd]="-DNO_SIMD"
    [simdnomax]="-DSIMD_IS_MAX=false"
    [autpreproc_standard]="-DAUT_PREPROCESSOR=aut_preprocessors::standard"
    [autpreproc_nopreproc]="-DAUT_PREPROCESSOR=aut_preprocessors::no_preprocessing"
    [booleanstates_none]="-DBOOLEAN_STATES=boolean_states::no_boolean_states"
    [iosprecom_delegate]="-DIOS_PRECOMPUTER=ios_precomputers::delegate -DACTIONERS='actioners::no_ios_precomputation<typename SetOfStates::value_type>'"
    [iosprecom_fake_vars]="-DIOS_PRECOMPUTER=ios_precomputers::fake_vars"
    [iosprecom_powset]="-DIOS_PRECOMPUTER=ios_precomputers::powset"
    [inputpicker_critical]="-DINPUT_PICKER=input_pickers::critical_pq"
    [inputpicker_critical_rnd]="-DINPUT_PICKER=input_pickers::critical_rnd"
)

for name param in ${(kv)confs}; do
    build=build_$name
    log=_bm-logs/$name.log
    if [[ -e $build ]]; then
        echo "$build exists, not rebuilding."
    else
        echo -n "building $build"
        CXXFLAGS="$opt $param" meson $build --buildtype=release &>> $log
        echo " done."
    fi
done

for name in ${(k)confs}; do
    build=build_$name
    log=_bm-logs/$name.log
    if [[ -e $build/compiled ]]; then
      echo "$name already compiled"
      continue
    fi
    cd $build
    echo -n "compiling $name "
    if meson compile &> ../$log; then
      echo "done"
      touch compiled
    else
      echo "FAILED"
    fi
    cd ..
done
    
for name in ${(k)confs}; do
    build=build_$name
    log=_bm-logs/$name.log
    if [[ -e $build/benchmarked ]]; then
        echo "skipping already benchmarked $name"
        continue
    fi
    if [[ ! -e $build/compiled ]]; then
        echo "skipping uncompiled $name"
        continue
    fi
    cd $build
    echo -n "benchmarking $name"
    meson test --benchmark --suite=ab/syntcomp21/crit -t 1.7 &>> ../$log
    echo "done"
    touch benchmarked
    cd ..
done
