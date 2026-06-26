#!/bin/zsh -f

mkdir -p _bm-logs

BENCHMARK_SUITE=ab/syntcomp21/crit
TIMEOUT_FACTOR=1.7
BENCHMARK_CGROUP=${BENCHMARK_CGROUP:-auto}
BENCHMARK_CGROUP_MEMORY_MAX=${BENCHMARK_CGROUP_MEMORY_MAX:-}
BENCHMARK_CGROUP_SWAP_MAX=${BENCHMARK_CGROUP_SWAP_MAX:-0}
BENCHMARK_CGROUP_ENABLED=false
BENCHMARK_COMPILE_PROFILE=${BENCHMARK_COMPILE_PROFILE:-normal}

# -fuse-linker-plugin requires the gold linker, which is unavailable on macOS
if [[ "$(uname)" == Darwin ]]; then
    opt='-march=native -Ofast -flto -pipe -DNO_VERBOSE -DNDEBUG'
else
    opt='-march=native -Ofast -flto -fuse-linker-plugin -pipe -DNO_VERBOSE -DNDEBUG'
fi
opt_justtest=''
compile_args=()

declare -A confs

# WARNING: The actual defaults are set in configuration.hh
# defaults=$(<<EOF 
# -DDEFAULT_K=255
# -DDEFAULT_KMIN=2
# -DDEFAULT_KINC=3
# -DDEFAULT_UNREAL_X='UNREAL_X_BOTH'
# -DVECTOR_ELT_T='char'
# -DSTATIC_ARRAY_MAX='300'
# -DSTATIC_MAX_BITSETS='8ul'
# -DSIMD_IS_MAX='true'
# -DAUT_PREPROCESSOR='aut_preprocessors::surely_losing'
# -DBOOLEAN_STATES='boolean_states::forward_saturation'
# -DIOS_PRECOMPUTER='ios_precomputers::standard'
# -DACTIONER='actioners::standard'
# -DINPUT_PICKER='input_pickers::critical_pq'
# -DARRAY_AND_BITSET_DOWNSET_IMPL='vector_backed'
# -DVECTOR_AND_BITSET_DOWNSET_IMPL='vector_backed'
# EOF
#         )

autpreproc_no_preprocessing="-DAUT_PREPROCESSOR='aut_preprocessors::no_preprocessing' -DACACIA_ENABLE_AUT_PREPROCESSOR_SURELY_LOSING=0 -DACACIA_ENABLE_AUT_PREPROCESSOR_NO_PREPROCESSING=1"
autpreproc_standard="-DAUT_PREPROCESSOR='aut_preprocessors::standard' -DACACIA_ENABLE_AUT_PREPROCESSOR_SURELY_LOSING=0 -DACACIA_ENABLE_AUT_PREPROCESSOR_STANDARD=1"
autpreproc_elevator="-DAUT_PREPROCESSOR='aut_preprocessors::elevator' -DACACIA_ENABLE_AUT_PREPROCESSOR_SURELY_LOSING=0 -DACACIA_ENABLE_AUT_PREPROCESSOR_ELEVATOR=1"
boolean_forward_saturation="-DBOOLEAN_STATES='boolean_states::forward_saturation' -DACACIA_ENABLE_BOOLEAN_STATES_FORWARD_SATURATION=1"
boolean_no_boolean_states="-DBOOLEAN_STATES='boolean_states::no_boolean_states' -DACACIA_ENABLE_BOOLEAN_STATES_FORWARD_SATURATION=0 -DACACIA_ENABLE_BOOLEAN_STATES_NO_BOOLEAN_STATES=1"
ios_delegate="-DIOS_PRECOMPUTER='ios_precomputers::delegate' -DACACIA_ENABLE_IOS_PRECOMPUTER_STANDARD=0 -DACACIA_ENABLE_IOS_PRECOMPUTER_DELEGATE=1"
ios_fake_vars="-DIOS_PRECOMPUTER='ios_precomputers::fake_vars' -DACACIA_ENABLE_IOS_PRECOMPUTER_STANDARD=0 -DACACIA_ENABLE_IOS_PRECOMPUTER_FAKE_VARS=1"
ios_powset="-DIOS_PRECOMPUTER='ios_precomputers::powset' -DACACIA_ENABLE_IOS_PRECOMPUTER_STANDARD=0 -DACACIA_ENABLE_IOS_PRECOMPUTER_POWSET=1"
ios_mona="-DIOS_PRECOMPUTER='ios_precomputers::mona' -DACACIA_ENABLE_IOS_PRECOMPUTER_STANDARD=0 -DACACIA_ENABLE_IOS_PRECOMPUTER_MONA=1"
actioner_no_ios="-DACTIONER='actioners::no_ios_precomputation' -DACACIA_ENABLE_ACTIONER_STANDARD=0 -DACACIA_ENABLE_ACTIONER_NO_IOS_PRECOMPUTATION=1"
input_critical="-DINPUT_PICKER='input_pickers::critical' -DACACIA_ENABLE_INPUT_PICKER_CRITICAL_PQ=0 -DACACIA_ENABLE_INPUT_PICKER_CRITICAL=1"
input_critical_pq="-DINPUT_PICKER='input_pickers::critical_pq' -DACACIA_ENABLE_INPUT_PICKER_CRITICAL_PQ=1"
input_critical_rnd="-DINPUT_PICKER='input_pickers::critical_rnd' -DACACIA_ENABLE_INPUT_PICKER_CRITICAL_PQ=0 -DACACIA_ENABLE_INPUT_PICKER_CRITICAL_RND=1"
input_critical_fullrnd="-DINPUT_PICKER='input_pickers::critical_fullrnd' -DACACIA_ENABLE_INPUT_PICKER_CRITICAL_PQ=0 -DACACIA_ENABLE_INPUT_PICKER_CRITICAL_FULLRND=1"

# Experimentally determined
best_common=$(<<EOF
-DDEFAULT_KMIN=2
-DDEFAULT_KINC=3
-DDEFAULT_UNREAL_X='UNREAL_X_BOTH'
-DSIMD_IS_MAX='false'
-DARRAY_AND_BITSET_DOWNSET_IMPL='vector_backed'
-DVECTOR_AND_BITSET_DOWNSET_IMPL='vector_backed'
EOF
    )
best_powset="$best_common $autpreproc_standard $boolean_forward_saturation $ios_powset $input_critical"
best_mona_base="$best_common $autpreproc_standard $boolean_forward_saturation $ios_mona $input_critical"
best="$best_powset -DDECOMPOSE_SPEC=0"

confs=(
    # Dummy configuration: does not actually change any acacia-bonsai build
    # flags, but tells the benchmark step below to run the `ltlsynt/…`
    # meson suites (i.e. ltlsynt on the same inputs) instead of `ab/…`.
    [ltlsynt]=" "
    # These are variations on the default configuration
    [base]=" "
    # [kmin5_kinc2]="-DDEFAULT_KMIN=5 -DDEFAULT_KINC=2"
    # [kmin5_kinc1]="-DDEFAULT_KMIN=5 -DDEFAULT_KINC=1"
    # [kmin2_kinc1]="-DDEFAULT_KMIN=2 -DDEFAULT_KINC=1"
    # [kmin2_kinc3]="-DDEFAULT_KMIN=2 -DDEFAULT_KINC=3"
    # [x_is_form]="-DDEFAULT_UNREAL_X=UNREAL_X_FORMULA"
    # [x_is_aut]="-DDEFAULT_UNREAL_X=UNREAL_X_AUTOMATON"
    [base_nosimd]="-DNO_SIMD"
    [base_simdnomax]="-DSIMD_IS_MAX=false"
    [base_autpreproc_standard]="$autpreproc_standard"
    [base_autpreproc_nopreproc]="$autpreproc_no_preprocessing"
    [base_booleanstates_none]="$boolean_no_boolean_states"
    [base_noiosprecom_delegate]="$ios_delegate $actioner_no_ios"
    [base_iosprecom_fake_vars]="$ios_fake_vars"
    [base_iosprecom_powset]="$ios_powset"
    [base_iosprecom_mona]="$ios_mona"
    [base_inputpicker_critical_pq]="$input_critical_pq"
    [base_inputpicker_critical_rnd]="$input_critical_rnd"
    [base_inputpicker_critical_fullrnd]="$input_critical_fullrnd"
    [base_downset_vector_or_kdtree]="-DARRAY_AND_BITSET_DOWNSET_IMPL='vector_or_kdtree_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='vector_or_kdtree_backed'"
    [base_downset_kdtree]="-DARRAY_AND_BITSET_DOWNSET_IMPL='kdtree_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='kdtree_backed'"
    [base_downset_vector]="-DARRAY_AND_BITSET_DOWNSET_IMPL=vector_backed -DVECTOR_AND_BITSET_DOWNSET_IMPL=vector_backed"
    [base_downset_vectorbin]="-DARRAY_AND_BITSET_DOWNSET_IMPL=vector_backed_bin -DVECTOR_AND_BITSET_DOWNSET_IMPL=vector_backed_bin -DARRAY_IMPL=simd_array_backed_sum -DVECTOR_IMPL=simd_vector_backed"
    # These are variations on the best configuration
    [best]="$best"
    [best_decomp]="$best_powset -DDECOMPOSE_SPEC=1"
    [best_no_array_cap_max]="$best -DNO_ARRAY_CAP_MAX"  # STATIC_ARRAY_CAP_MAX will be set to 0
    [best_no_bitsets]="$best -DNO_ARRAY_CAP_MAX -DUSE_BOOLVEC_OVER_BITSET"  # same, and x_and_boolvec used instead of x_and_bitset
    [best_mona]="$best_mona_base -DDECOMPOSE_SPEC=0"
    [best_noiosprecom_delegate]="$best_common $autpreproc_standard $boolean_forward_saturation $ios_delegate $actioner_no_ios $input_critical -DDECOMPOSE_SPEC=0"
    [best_downset_vector_or_kdtree]="$best -DARRAY_AND_BITSET_DOWNSET_IMPL='vector_or_kdtree_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='vector_or_kdtree_backed'"
    [best_downset_kdtree]="$best -DARRAY_AND_BITSET_DOWNSET_IMPL='kdtree_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='kdtree_backed'"
    [best_downset_sharingtree]="$best -DARRAY_AND_BITSET_DOWNSET_IMPL='sharingtree_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='sharingtree_backed'"
    [best_downset_simple_sharingtree]="$best -DARRAY_AND_BITSET_DOWNSET_IMPL='simple_sharingtree_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='simple_sharingtree_backed'"
    [best_downset_sharingtrie]="$best -DARRAY_AND_BITSET_DOWNSET_IMPL='sharingtrie_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='sharingtrie_backed'"
    [best_decomp_kdtree_mona]="$best_mona_base -DARRAY_AND_BITSET_DOWNSET_IMPL='kdtree_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='kdtree_backed' -DDECOMPOSE_SPEC=1"
    [best_decomp_kdtree_mona_no_bitsets]="$best_mona_base -DARRAY_AND_BITSET_DOWNSET_IMPL='kdtree_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='kdtree_backed' -DDECOMPOSE_SPEC=1 -DNO_ARRAY_CAP_MAX -DUSE_BOOLVEC_OVER_BITSET"
    [best_decomp_sharingtrie_mona]="$best_mona_base -DARRAY_AND_BITSET_DOWNSET_IMPL='sharingtrie_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='sharingtrie_backed' -DDECOMPOSE_SPEC=1"
    [best_decomp_simpsharingtree_mona]="$best_mona_base -DARRAY_AND_BITSET_DOWNSET_IMPL='simple_sharingtree_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='simple_sharingtree_backed' -DDECOMPOSE_SPEC=1"
    [best_decomp_sharingtree_mona]="$best_mona_base -DARRAY_AND_BITSET_DOWNSET_IMPL='sharingtree_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='sharingtree_backed' -DDECOMPOSE_SPEC=1"
    [best_decomp_sharingtrie_mona_no_bitsets]="$best_mona_base -DARRAY_AND_BITSET_DOWNSET_IMPL='sharingtrie_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='sharingtrie_backed' -DDECOMPOSE_SPEC=1 -DNO_ARRAY_CAP_MAX -DUSE_BOOLVEC_OVER_BITSET"
    [best_decomp_mona_no_bitsets]="$best_mona_base -DDECOMPOSE_SPEC=1 -DNO_ARRAY_CAP_MAX -DUSE_BOOLVEC_OVER_BITSET"
    [best_decomp_mona]="$best_mona_base -DDECOMPOSE_SPEC=1"
    [best_decomp_mona_elevator]="$best_common $autpreproc_elevator $boolean_forward_saturation $ios_mona $input_critical -DDECOMPOSE_SPEC=1"
    [best_decomp_mona_spotfast_det_and_gfg]="$best_mona_base -DDECOMPOSE_SPEC=1 -DDEFAULT_SPOT_FAST=SPOT_FAST_DET_AND_GFG"
    [best_decomp_skiplist_mona]="$best_mona_base -DARRAY_AND_BITSET_DOWNSET_IMPL='skiplist_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='skiplist_backed' -DDECOMPOSE_SPEC=1"
    [best_decomp_cst_mona]="$best_mona_base -DARRAY_AND_BITSET_DOWNSET_IMPL='cst_backed' -DVECTOR_AND_BITSET_DOWNSET_IMPL='cst_backed' -DDECOMPOSE_SPEC=1"
)

mode= # print, list
force=false
justtest=false
native_file=
donot=()
conflist=(${(k)confs})
benchsuites=(--suite=$BENCHMARK_SUITE)

if (( $# == 0 )); then
  secs=10

  cat <<EOF
No option given; this will build, compile, and benchmark ${#confs} different configurations.
To build/compile/benchmark with default (debug) options, run:

  $ meson setup build
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

while getopts "hplBCLjRfb:t:c:m:n:" option; do
    case $option; in
        h) cat <<EOF
usage: $0 [-hplBCLjR] [-b BENCHMARK[,BENCHMARK]] [-c CONF[,CONF,...]] [-m MEMORY] [-n NATIVE_FILE]
  -h: Print this message.
  -p: Do not build/compile/benchmark, instead, print the CXXFLAGS.
  -l: Do not build/compile/benchmark, instead, list configurations.
  -B: Do not build.
  -C: Do not compile.
  -L: Use low-memory non-debug compile flags (-O0 -g0, no LTO) and compile with one job.
  -j: Just test instead of benchmarking.
  -R: Do not benchmark.
  -f: Do not fail when a build, compile, or benchmark does, continue as if they passed.
  -b BENCHMARK: Run a specific benchmark suite (default: $BENCHMARK_SUITE).
  -t TIMEOUT: Use timeout factor TIMEOUT (default: $TIMEOUT_FACTOR).  Actual time is multiplied by 10.
  -c CONF,...: Only consider configurations listed.
  -m MEMORY: Cgroup MemoryMax for benchmark runs (default: 80% of RAM; use 'off' to disable).
  -n NATIVE_FILE: Path to a meson native file passed to \`meson setup\`.
Environment:
  BENCHMARK_CGROUP=auto|strict|off       Wrap benchmark runs in a systemd cgroup (default: auto).
  BENCHMARK_CGROUP_MEMORY_MAX=MEMORY     Same as -m.
  BENCHMARK_CGROUP_SWAP_MAX=MEMORY       systemd MemorySwapMax value (default: 0).
  BENCHMARK_COMPILE_PROFILE=normal|lowmem
EOF
           exit 1;;
        p) mode=print;;
        l) mode=list;;
        B) donot+=build;;
        C) donot+=compile;;
        L) BENCHMARK_COMPILE_PROFILE=lowmem;;
	j) justtest=true;;
        R) donot+=benchmark;;
        f) force=true;;
        c) conflist=(${(@s:,:)OPTARG});;
	t) TIMEOUT_FACTOR=$OPTARG;;
	m) BENCHMARK_CGROUP_MEMORY_MAX=$OPTARG;;
	b) benchsuites=()
	   for b in ${(@s:,:)OPTARG}; do
	     benchsuites+=(--suite="$b")
	   done;;
	n) native_file=$OPTARG;;
    esac
done

## If we're just testing, go for a fast compilation
rel="--buildtype=release"
if $justtest; then
    opt=$opt_justtest
    echo "Using opt $opt for faster compile-to-test times"
    rel=""
fi
case $BENCHMARK_COMPILE_PROFILE in
    normal) ;;
    lowmem)
        opt='-O0 -g0 -pipe -DNO_VERBOSE -DNDEBUG'
        rel='--buildtype=plain'
        compile_args=(-j 1)
        echo "Using low-memory compile profile: opt $opt, compile jobs 1"
        ;;
    *)
        print -u2 "ERROR: BENCHMARK_COMPILE_PROFILE must be 'normal' or 'lowmem' (got '$BENCHMARK_COMPILE_PROFILE')."
        exit 2
        ;;
esac

default_benchmark_cgroup_memory_max() {
    local mem_kib
    mem_kib=$(awk '/^MemTotal:/ { print $2; exit }' /proc/meminfo 2>/dev/null)
    if [[ $mem_kib == <-> ]]; then
        local cap_mib=$(( mem_kib * 8 / 10 / 1024 ))
        if (( cap_mib > 0 )); then
            echo "${cap_mib}M"
            return
        fi
    fi
    echo 12G
}

configure_benchmark_cgroup() {
    local mode=$BENCHMARK_CGROUP
    local memory_max=$BENCHMARK_CGROUP_MEMORY_MAX
    local swap_max=$BENCHMARK_CGROUP_SWAP_MAX

    [[ -z $memory_max ]] && memory_max=$(default_benchmark_cgroup_memory_max)

    case $mode in
        auto|strict) ;;
        off|none|false|0) BENCHMARK_CGROUP_ENABLED=false; return 0;;
        *)
            print -u2 "ERROR: BENCHMARK_CGROUP must be 'auto', 'strict', or 'off' (got '$mode')."
            exit 2
            ;;
    esac

    if [[ $memory_max == off || $memory_max == none || $memory_max == false || $memory_max == 0 ]]; then
        BENCHMARK_CGROUP_ENABLED=false
        return 0
    fi

    if ! command -v systemd-run >/dev/null; then
        if [[ $mode == strict ]]; then
            print -u2 "ERROR: systemd-run is required for BENCHMARK_CGROUP=strict."
            exit 2
        fi
        print -u2 "WARN: systemd-run not found; benchmarks will run without a memory cgroup."
        BENCHMARK_CGROUP_ENABLED=false
        return 0
    fi

    local -a props
    props=("--property=MemoryMax=$memory_max")
    [[ -n $swap_max ]] && props+=("--property=MemorySwapMax=$swap_max")

    if systemd-run --user --scope --quiet "--unit=acacia-bonsai-bench-preflight-$$" $props true >/dev/null 2>&1; then
        BENCHMARK_CGROUP_MEMORY_MAX=$memory_max
        BENCHMARK_CGROUP_SWAP_MAX=$swap_max
        BENCHMARK_CGROUP_ENABLED=true
        echo "Benchmark cgroup enabled: MemoryMax=$BENCHMARK_CGROUP_MEMORY_MAX MemorySwapMax=$BENCHMARK_CGROUP_SWAP_MAX"
        return 0
    fi

    if [[ $mode == strict ]]; then
        print -u2 "ERROR: failed to create a benchmark cgroup with systemd-run --user."
        exit 2
    fi
    print -u2 "WARN: failed to create a benchmark cgroup with systemd-run --user; running benchmarks without a memory cgroup."
    print -u2 "      Set BENCHMARK_CGROUP=off to silence this warning or BENCHMARK_CGROUP=strict to fail instead."
    BENCHMARK_CGROUP_ENABLED=false
}

run_benchmark_command() {
    local scope=$1
    shift

    if $BENCHMARK_CGROUP_ENABLED; then
        local -a props
        props=("--property=MemoryMax=$BENCHMARK_CGROUP_MEMORY_MAX")
        [[ -n $BENCHMARK_CGROUP_SWAP_MAX ]] && props+=("--property=MemorySwapMax=$BENCHMARK_CGROUP_SWAP_MAX")
        systemd-run --user --scope --quiet "--unit=acacia-bonsai-bench-$scope-$$" $props "$@"
    else
        "$@"
    fi
}

## Print and list mode
if [[ $mode == print || $mode == list ]]; then
    for name in $conflist; do
        echo -n "- $name"
        if [[ $mode == print ]]; then
            # echo -n ": $opt $defaults $confs[$name]" | tr '\n' ' '
	    # defaults are set in configuration.hh
            echo -n ": $opt $confs[$name]" | tr '\n' ' '
        fi
        echo
    done
    exit
fi

## Build
if ! (( $donot[(Ie)build] )); then
    for name in $conflist; do
        param=$confs[$name]
        [[ $param == "" ]] && { echo "error: $name, unknown configuration."; $force || exit 2 }
        build=build_$name
        log=_bm-logs/$name.log
        rm -f $log
        if [[ -e $build ]]; then
            echo "$build exists, not rebuilding, remove folder to rebuild."
        else
            echo -n "building $build (logfile: $log)... "
            # if CXXFLAGS="$opt $defaults $param $CXXFLAGS" meson setup $build $rel &>> $log; then
	    # defaults are set in configuration.hh
            native_flag=()
            [[ -n $native_file ]] && native_flag=(--native-file "$native_file")
            if CXXFLAGS="$opt $defaults $param $CXXFLAGS" meson setup $build $rel $native_flag &>> $log; then
                echo "done."
            else
                echo "FAILED; please remove $build to recompile."
                cat $log
                $force || exit 2
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
        if meson compile $compile_args &>> ../$log; then
            echo "done"
            touch compiled
        else
            echo "FAILED."
            cat ../$log
            $force || exit 3
        fi
        cd ..
    done
fi

## Benchmark
if ! (( $donot[(Ie)benchmark] )); then
    configure_benchmark_cgroup
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
	## For the dummy ltlsynt configuration, redirect every --suite=ab/…
	## argument to --suite=ltlsynt/… so that the same meson run now
	## picks up the ltlsynt-backed benchmarks instead of the ab ones.
	if [[ $name == ltlsynt ]]; then
	    this_suites=()
	    for b in $benchsuites; do
	        this_suites+=("${b//ab\//ltlsynt/}")
	    done
	else
	    this_suites=($benchsuites)
	fi
	test_status=0
	if $justtest; then
	    echo -n "testing $name on $this_suites (logfile: $log)... "
	    run_benchmark_command $name meson test $this_suites -t $TIMEOUT_FACTOR &>> ../$log || test_status=$?
	else
	    echo -n "benchmarking $name on $this_suites (logfile: $log)... "
	    run_benchmark_command $name meson test --benchmark $this_suites -t $TIMEOUT_FACTOR &>> ../$log || test_status=$?
	fi
        if (( test_status != 0 )) || grep -q '^Fail:[[:space:]]*[1-9]' ../$log; then
            echo "FAILED; testlog stored at $log, _bm-logs/$name.json left untouched"
            $force || exit 5
        else
            echo "done; testlog stored at $log"
            touch benchmarked
        fi
        cd ..
        cp $build/meson-logs/testlog.json _bm-logs/$name.json
    done
fi
