#!/bin/zsh -f

mkdir -p _bm-logs

opt='-march=native -O3 -flto -fuse-linker-plugin -pipe -DNO_VERBOSE'

declare -A confs

defaults=$(<<EOF 
-DDEFAULT_K='11'
-DDEFAULT_KMIN='-1u'
-DDEFAULT_KINC='0'
-DDEFAULT_UNREAL_X='UNREAL_X_BOTH'
-DVECTOR_ELT_T='char'
-DK_BOUNDED_SAFETY_AUT_IMPL='k_bounded_safety_aut'
-DSTATIC_ARRAY_MAX='300'
-DSTATIC_MAX_BITSETS='3ul'
-DARRAY_AND_BITSET_DOWNSET_IMPL='kdtree_backed'
-DVECTOR_AND_BITSET_DOWNSET_IMPL='kdtree_backed'
-DSIMD_IS_MAX='true'
-DAUT_PREPROCESSOR='aut_preprocessors::surely_losing'
-DBOOLEAN_STATES='boolean_states::forward_saturation'
-DIOS_PRECOMPUTER='ios_precomputers::standard'
-DACTIONER='actioners::standard<typename SetOfStates::value_type>'
-DINPUT_PICKER='input_pickers::critical_pq'
EOF
        )

# Experimentally determined
best=$(<<EOF
-DDEFAULT_KMIN=5 -DDEFAULT_KINC=2
-DDEFAULT_UNREAL_X='UNREAL_X_BOTH'
-DAUT_PREPROCESSOR='aut_preprocessors::surely_losing'
-DBOOLEAN_STATES='boolean_states::forward_saturation'
-DIOS_PRECOMPUTER='ios_precomputers::powset'
-DINPUT_PICKER='input_pickers::critical'
EOF
     )
     
# These all differ from the base configuration by /one/ option.
confs=(
    [base]=" "
    [best]="$best"
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
    [iosprecom_delegate]="-DIOS_PRECOMPUTER=ios_precomputers::delegate -DACTIONER='actioners::no_ios_precomputation<typename SetOfStates::value_type>'"
    [iosprecom_fake_vars]="-DIOS_PRECOMPUTER=ios_precomputers::fake_vars"
    [iosprecom_powset]="-DIOS_PRECOMPUTER=ios_precomputers::powset"
    [inputpicker_critical]="-DINPUT_PICKER=input_pickers::critical"
    [inputpicker_critical_rnd]="-DINPUT_PICKER=input_pickers::critical_rnd"
    [inputpicker_critical_fullrnd]="-DINPUT_PICKER=input_pickers::critical_fullrnd"
    [downset_vector]="-DARRAY_AND_BITSET_DOWNSET_IMPL=vector_backed -DVECTOR_AND_BITSET_DOWNSET_IMPL=vector_backed"
    [downset_vectorbin]="-DARRAY_AND_BITSET_DOWNSET_IMPL=vector_backed_bin -DVECTOR_AND_BITSET_DOWNSET_IMPL=vector_backed_bin"
    [downset_v1ds]="-DARRAY_AND_BITSET_DOWNSET_IMPL=vector_backed_one_dim_split -DVECTOR_AND_BITSET_DOWNSET_IMPL=vector_backed_one_dim_split"
    [downset_v1dsio]="-DARRAY_AND_BITSET_DOWNSET_IMPL=vector_backed_one_dim_split_intersection_only -DVECTOR_AND_BITSET_DOWNSET_IMPL=vector_backed_one_dim_split_intersection_only"
)

for name param in ${(kv)confs}; do
    build=build_$name
    log=_bm-logs/$name.log
    rm -f $log
    if [[ -e $build ]]; then
        echo "$build exists, not rebuilding."
    else
        echo -n "building $build... "
        if CXXFLAGS="$opt $defaults $param" meson $build --buildtype=release &>> $log; then
            echo "done."
        else
            echo "FAILED; exciting readily, please remove $build."
            cat $log
            exit 2
        fi
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
    echo -n "compiling $name... "
    if meson compile &>> ../$log; then
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
    echo -n "benchmarking $name... "
    meson test --benchmark --suite=ab/syntcomp21/crit -t 1.7 &>> ../$log
    echo "done"
    touch benchmarked
    cd ..
done
