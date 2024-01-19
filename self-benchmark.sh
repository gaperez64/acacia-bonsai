#!/bin/zsh -f

mkdir -p _bm-logs

BENCHMARK_SUITE=ab/syntcomp21/crit
TIMEOUT_FACTOR=1.7

opt='-march=native -O3 -flto -fuse-linker-plugin -pipe -DNO_VERBOSE -DNDEBUG'

declare -A confs

defaults=$(<<EOF 
-DDEFAULT_K='11'
-DDEFAULT_KMIN='-1u'
-DDEFAULT_KINC='0'
-DDEFAULT_UNREAL_X='UNREAL_X_BOTH'
-DVECTOR_ELT_T='char'
-DK_BOUNDED_SAFETY_AUT_IMPL='k_bounded_safety_aut'
-DSTATIC_ARRAY_MAX='300'
-DSTATIC_MAX_BITSETS='8ul'
-DSIMD_IS_MAX='true'
-DAUT_PREPROCESSOR='aut_preprocessors::surely_losing'
-DBOOLEAN_STATES='boolean_states::forward_saturation'
-DIOS_PRECOMPUTER='ios_precomputers::standard'
-DACTIONER='actioners::standard<typename SetOfStates::value_type>'
-DINPUT_PICKER='input_pickers::critical_pq'
-DARRAY_AND_BITSET_DOWNSET_IMPL='vector_backed'
-DVECTOR_AND_BITSET_DOWNSET_IMPL='vector_backed'
EOF
        )

# Experimentally determined
best=$(<<EOF
-DDEFAULT_KMIN=2 -DDEFAULT_KINC=3
-DDEFAULT_UNREAL_X='UNREAL_X_BOTH'
-DAUT_PREPROCESSOR='aut_preprocessors::standard'
-DBOOLEAN_STATES='boolean_states::forward_saturation'
-DIOS_PRECOMPUTER='ios_precomputers::powset'
-DINPUT_PICKER='input_pickers::critical'
-DSIMD_IS_MAX='false'
-DARRAY_AND_BITSET_DOWNSET_IMPL='vector_backed'
-DVECTOR_AND_BITSET_DOWNSET_IMPL='vector_backed'
EOF
    )

# These all differ from the base configuration by /one/ option.
confs=(
    [base]=" "
    [best]="$best"
    [best_nosimd]="$best -DNO_SIMD"
#    [best_noiosprecom]="$best -DIOS_PRECOMPUTER=ios_precomputers::delegate -DACTIONER='actioners::no_ios_precomputation<typename SetOfStates::value_type>'"
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
    [inputpicker_critical_pq]="-DINPUT_PICKER=input_pickers::critical_pq"
    [inputpicker_critical_rnd]="-DINPUT_PICKER=input_pickers::critical_rnd"
    [inputpicker_critical_fullrnd]="-DINPUT_PICKER=input_pickers::critical_fullrnd"
    [downset_vector_or_kdtree]="-DARRAY_AND_BITSET_DOWNSET_IMPL='vector_or_kdtree_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='vector_or_kdtree_backed'"
    [best_downset_vector_or_kdtree]="$best -DARRAY_AND_BITSET_DOWNSET_IMPL='vector_or_kdtree_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='vector_or_kdtree_backed' -DNO_SIMD"
    [best_downset_vector_or_kdtree_simd]="$best -DARRAY_AND_BITSET_DOWNSET_IMPL='vector_or_kdtree_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='vector_or_kdtree_backed'"
    [downset_kdtree]="-DARRAY_AND_BITSET_DOWNSET_IMPL='kdtree_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='kdtree_backed'"
    [best_downset_kdtree]="$best -DARRAY_AND_BITSET_DOWNSET_IMPL='kdtree_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='kdtree_backed' -DNO_SIMD"
    [best_downset_kdtree_simd]="$best -DARRAY_AND_BITSET_DOWNSET_IMPL='kdtree_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='kdtree_backed'"
    [downset_vector]="-DARRAY_AND_BITSET_DOWNSET_IMPL=vector_backed -DVECTOR_AND_BITSET_DOWNSET_IMPL=vector_backed"
    [best_downset_vector]="$best -DARRAY_AND_BITSET_DOWNSET_IMPL=vector_backed -DVECTOR_AND_BITSET_DOWNSET_IMPL=vector_backed -DNO_SIMD"
    [best_downset_vector_simd]="$best -DARRAY_AND_BITSET_DOWNSET_IMPL=vector_backed -DVECTOR_AND_BITSET_DOWNSET_IMPL=vector_backed"
    [downset_vectorbin]="-DARRAY_AND_BITSET_DOWNSET_IMPL=vector_backed_bin -DVECTOR_AND_BITSET_DOWNSET_IMPL=vector_backed_bin -DARRAY_IMPL=simd_array_backed_sum -DVECTOR_IMPL=simd_vector_backed"
    [downset_v1ds]="-DARRAY_AND_BITSET_DOWNSET_IMPL=vector_backed_one_dim_split -DVECTOR_AND_BITSET_DOWNSET_IMPL=vector_backed_one_dim_split"
#    [downset_v1dsio]="-DARRAY_AND_BITSET_DOWNSET_IMPL=vector_backed_one_dim_split_intersection_only -DVECTOR_AND_BITSET_DOWNSET_IMPL=vector_backed_one_dim_split_intersection_only"
)

mode= # print, list
force=false
donot=()
conflist=(${(k)confs})
benchsuites=(--suite=$BENCHMARK_SUITE)

if (( $# == 0 )); then
  secs=10

  cat <<EOF
No option given; this will build, compile, and benchmark ${#confs} different configurations.
To build/compile/benchmark with default (debug) options, run:

  $ meson build
  $ cd build
  $ meson compile
  $ meson test --benchmark --suite=$BENCHMARK_SUITE -t $TIMEOUT_FACTOR

EOF
  echo -n "Waiting $secs seconds before starting; hit Ctrl-C to cancel: "
  for i in {$secs..1}; do
    echo -n "$i "
    sleep 1
  done
  echo "."
fi

while getopts "hplBCRfb:t:c:" option; do
    case $option; in
        h) cat <<EOF
usage: $0 [-hplBCR] [-b BENCHMARK[,BENCHMARK]] [-c CONF[,CONF,...]]
  -h: Print this message.
  -p: Do not build/compile/benchmark, instead, print the CXXFLAGS.
  -l: Do not build/compile/benchmark, instead, list configurations.
  -B: Do not build.
  -C: Do not compile.
  -R: Do not benchmark.
  -f: Do not fail when a benchmark does, continue as if they passed.
  -b BENCHMARK: Run a specific benchmark suite (default: $BENCHMARK_SUITE).
  -t TIMEOUT: Use timeout factor TIMEOUT (default: $TIMEOUT_FACTOR).  Actual time is multiplied by 10.
  -c CONF,...: Only consider configurations listed.
EOF
           exit 1;;
        p) mode=print;;
        l) mode=list;;
        B) donot+=build;;
        C) donot+=compile;;
        R) donot+=benchmark;;
        f) force=true;;
        c) conflist=(${(@s:,:)OPTARG});;
	t) TIMEOUT_FACTOR=$OPTARG;;
	b) benchsuites=()
	   for b in ${(@s:,:)OPTARG}; do
	     benchsuites+=(--suite="$b")
	   done
    esac
done

## Print and list mode
if [[ $mode == print || $mode == list ]]; then
    for name in $conflist; do
        echo -n "- $name"
        if [[ $mode == print ]]; then
            echo -n ": $opt $defaults $confs[$name]" | tr '\n' ' '
        fi
        echo
    done
    exit
fi

## Build
if ! (( $donot[(Ie)build] )); then
    for name in $conflist; do
        param=$confs[$name]
        [[ $param == "" ]] && { echo "error: $name, unknown configuration."; exit 2 }
        build=build_$name
        log=_bm-logs/$name.log
        rm -f $log
        if [[ -e $build ]]; then
            echo "$build exists, not rebuilding, remove folder to rebuild."
        else
            echo -n "building $build (logfile: $log)... "
            if CXXFLAGS="$opt $defaults $param $CXXFLAGS" meson $build --buildtype=release &>> $log; then
                echo "done."
            else
                echo "FAILED; exciting readily, please remove $build."
                cat $log
                exit 2
            fi
        fi
    done
fi

## Compile
if ! (( $donot[(Ie)compile] )); then
    for name in $conflist; do
        build=build_$name
        log=_bm-logs/$name.log
        if [[ -e $build/compiled ]]; then
            echo "$name already compiled, remove $build/compiled to recompile"
            continue
        fi
        if ! [[ -e $build ]]; then
	    echo "$name isn't built, skipping."
	    continue
        fi
        cd $build
        echo -n "compiling $name (logfile: $log)... "
        if meson compile &>> ../$log; then
            echo "done"
            touch compiled
        else
            echo "FAILED; stopping..."
            cat ../$log
            exit 3
        fi
        cd ..
    done
fi

## Benchmark
if ! (( $donot[(Ie)benchmark] )); then
    for name in $conflist; do
        build=build_$name
        log=_bm-logs/$name.log
        if [[ -e $build/benchmarked ]]; then
            echo "skipping already benchmarked $name, remove $build/benchmarked to rebenchmark"
            continue
        fi
        if [[ ! -e $build/compiled ]]; then
            echo "skipping uncompiled $name, create $build/compiled if compiled by hand"
            continue
        fi
        cd $build
        echo -n "benchmarking $name (logfile: $log)... "
        meson test --benchmark $benchsuites -t $TIMEOUT_FACTOR &>> ../$log
        if ! $force && grep -q '^Fail:[[:space:]]*[1-9]' ../$log; then
            echo "FAILED; testlog stored at $log, _bm-logs/$name.json left untouched"
            exit 5
        else
            echo "done; testlog stored at $log"
            touch benchmarked
        fi
        cd ..
        cp $build/meson-logs/testlog.json _bm-logs/$name.json
    done
fi
