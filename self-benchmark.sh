#!/bin/zsh -f

mkdir -p _bm-logs

BENCHMARK_SUITE=ab/syntcomp21/crit
TIMEOUT_FACTOR=1.7
BENCHMARK_CGROUP=${BENCHMARK_CGROUP:-auto}
BENCHMARK_CGROUP_MEMORY_MAX=${BENCHMARK_CGROUP_MEMORY_MAX:-}
BENCHMARK_CGROUP_SWAP_MAX=${BENCHMARK_CGROUP_SWAP_MAX:-0}
BENCHMARK_CGROUP_ENABLED=false
BENCHMARK_CGROUP_SCOPE=${BENCHMARK_CGROUP_SCOPE:-solver}
BENCHMARK_COMPILE_PROFILE=${BENCHMARK_COMPILE_PROFILE:-normal}
BENCHMARK_COMPILE_JOBS=${BENCHMARK_COMPILE_JOBS:-1}
BENCHMARK_TEST_JOBS=${BENCHMARK_TEST_JOBS:-1}

# -fuse-linker-plugin requires the gold linker, which is unavailable on macOS
if [[ "$(uname)" == Darwin ]]; then
    opt='-march=native -Ofast -flto -pipe -DNO_VERBOSE -DNDEBUG'
else
    opt='-march=native -Ofast -flto -fuse-linker-plugin -pipe -DNO_VERBOSE -DNDEBUG'
fi
opt_justtest=''
compile_args=()

declare -A confs
declare -A meson_confs
declare -A tool_descriptions
declare -A tool_envs
declare -A tool_suite_prefixes
config_helper=${ACACIA_CONFIG_HELPER:-./scripts/acacia-config.py}
for name in ${(f)"$(python3 "$config_helper" list-presets)"}; do
    confs[$name]="$(python3 "$config_helper" cxxflags "$name")"
    meson_confs[$name]="$(python3 "$config_helper" meson-args "$name")"
done
for name in ${(f)"$(python3 "$config_helper" list-tools)"}; do
    confs[$name]="__external_tool__"
    tool_descriptions[$name]="$(python3 "$config_helper" tool-description "$name")"
    tool_envs[$name]="$(python3 "$config_helper" tool-env "$name")"
    tool_suite_prefixes[$name]="$(python3 "$config_helper" tool-suite-prefix "$name")"
done

mode= # print, list
force=false
justtest=false
native_file=
donot=()
conflist=(${(k)confs})
benchsuites=(--suite=$BENCHMARK_SUITE)
meson_slice=()

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

while getopts "hplBCLjRfb:t:c:m:n:s:" option; do
    case $option; in
        h) cat <<EOF
usage: $0 [-hplBCLjR] [-b BENCHMARK[,BENCHMARK]] [-c CONF[,CONF,...]] [-m MEMORY] [-n NATIVE_FILE] [-s SLICE/N]
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
  -s SLICE/N: Pass a Meson test slice, useful for smoke runs on large suites.
Environment:
  BENCHMARK_CGROUP=auto|strict|off       Memory-limit benchmark runs with systemd cgroups (default: auto).
  BENCHMARK_CGROUP_SCOPE=solver|meson    Cgroup each solver process or the whole Meson run (default: solver).
  BENCHMARK_CGROUP_MEMORY_MAX=MEMORY     Same as -m.
  BENCHMARK_CGROUP_SWAP_MAX=MEMORY       systemd MemorySwapMax value (default: 0).
  BENCHMARK_COMPILE_PROFILE=normal|lowmem
  BENCHMARK_COMPILE_JOBS=N               Meson compile parallelism (default: 1).
  BENCHMARK_TEST_JOBS=N                  Meson benchmark/test parallelism (default: 1).
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
        s) meson_slice=(--slice="$OPTARG");;
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
        compile_args=(-j "$BENCHMARK_COMPILE_JOBS")
        echo "Using low-memory compile profile: opt $opt, compile jobs $BENCHMARK_COMPILE_JOBS"
        ;;
    *)
        print -u2 "ERROR: BENCHMARK_COMPILE_PROFILE must be 'normal' or 'lowmem' (got '$BENCHMARK_COMPILE_PROFILE')."
        exit 2
        ;;
esac
if (( ${#compile_args} == 0 )); then
    compile_args=(-j "$BENCHMARK_COMPILE_JOBS")
fi

if [[ $BENCHMARK_TEST_JOBS != <-> || $BENCHMARK_TEST_JOBS -lt 1 ]]; then
    print -u2 "ERROR: BENCHMARK_TEST_JOBS must be a positive integer (got '$BENCHMARK_TEST_JOBS')."
    exit 2
fi
if [[ $BENCHMARK_COMPILE_JOBS != <-> || $BENCHMARK_COMPILE_JOBS -lt 1 ]]; then
    print -u2 "ERROR: BENCHMARK_COMPILE_JOBS must be a positive integer (got '$BENCHMARK_COMPILE_JOBS')."
    exit 2
fi
case $BENCHMARK_CGROUP_SCOPE in
    solver|meson) ;;
    *)
        print -u2 "ERROR: BENCHMARK_CGROUP_SCOPE must be 'solver' or 'meson' (got '$BENCHMARK_CGROUP_SCOPE')."
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
        echo "Benchmark cgroup enabled: scope=$BENCHMARK_CGROUP_SCOPE MemoryMax=$BENCHMARK_CGROUP_MEMORY_MAX MemorySwapMax=$BENCHMARK_CGROUP_SWAP_MAX"
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
        if [[ $BENCHMARK_CGROUP_SCOPE == meson ]]; then
            props=("--property=MemoryMax=$BENCHMARK_CGROUP_MEMORY_MAX")
            [[ -n $BENCHMARK_CGROUP_SWAP_MAX ]] && props+=("--property=MemorySwapMax=$BENCHMARK_CGROUP_SWAP_MAX")
            systemd-run --user --scope --quiet "--unit=acacia-bonsai-bench-$scope-$$" $props "$@"
        else
            env ACACIA_TEST_CGROUP=1 \
                ACACIA_TEST_CGROUP_MEMORY_MAX="$BENCHMARK_CGROUP_MEMORY_MAX" \
                ACACIA_TEST_CGROUP_SWAP_MAX="$BENCHMARK_CGROUP_SWAP_MAX" \
                ACACIA_TEST_RESOURCE_UNKNOWN=1 \
                "$@"
        fi
    else
        "$@"
    fi
}

is_tool_config() {
    local name=$1
    [[ ${confs[$name]} == __external_tool__ ]]
}

is_compiled_acacia_build() {
    local build=$1
    [[ -e $build/compiled && -x $build/src/acacia-bonsai && -e $build/.acacia-config.json ]]
}

build_config_matches() {
    local name=$1
    local build=$2
    local current_config

    [[ -e $build/.acacia-config.json ]] || return 1
    current_config="$(python3 "$config_helper" show "$name")"
    cmp -s <(print -r -- "$current_config") $build/.acacia-config.json
}

benchmark_build_for() {
    local name=$1
    if ! is_tool_config $name; then
        echo "build_$name"
        return
    fi

    local candidate
    for candidate in build_ltlsynt build_base build_best_decomp_mona build_best; do
        if is_compiled_acacia_build $candidate; then
            echo "$candidate"
            return
        fi
    done
    for candidate in build_*(N); do
        if is_compiled_acacia_build $candidate; then
            echo "$candidate"
            return
        fi
    done
}

## Print and list mode
if [[ $mode == print || $mode == list ]]; then
    for name in $conflist; do
        if (( ! $+confs[$name] )); then
            echo "error: $name, unknown configuration."
            $force || exit 2
            continue
        fi
        echo -n "- $name"
        if [[ $mode == print ]]; then
            if is_tool_config $name; then
                echo -n ": ${tool_descriptions[$name]}"
                if [[ -n ${tool_envs[$name]} ]]; then
                    echo -n " env: ${tool_envs[$name]//$'\n'/ }"
                fi
            else
                echo -n ": CXXFLAGS='$opt' ${meson_confs[$name]}" | tr '\n' ' '
            fi
        fi
        echo
    done
    exit
fi

## Build
if ! (( $donot[(Ie)build] )); then
    for name in $conflist; do
        if (( ! $+confs[$name] )); then
            echo "error: $name, unknown configuration."
            $force || exit 2
            continue
        fi
        if is_tool_config $name; then
            echo "$name is an external benchmark backend, not building an Acacia config."
            continue
        fi
        meson_param=(${(z)meson_confs[$name]})
        build=build_$name
        log=_bm-logs/$name.log
        rm -f $log
        current_config="$(python3 "$config_helper" show "$name")"
        if [[ -e $build ]]; then
            if [[ ! -e $build/.acacia-config.json ]]; then
                echo "$build exists without Acacia config metadata; remove folder to rebuild."
                $force || exit 2
                continue
            elif ! cmp -s <(print -r -- "$current_config") $build/.acacia-config.json; then
                echo "$build exists with stale Acacia config metadata; remove folder to rebuild."
                $force || exit 2
                continue
            else
                echo "$build exists, not rebuilding, remove folder to rebuild."
            fi
        else
            echo -n "building $build (logfile: $log)... "
            native_flag=()
            [[ -n $native_file ]] && native_flag=(--native-file "$native_file")
            if CXXFLAGS="$opt $CXXFLAGS" meson setup $build $rel $native_flag $meson_param &>> $log; then
                print -r -- "$current_config" > $build/.acacia-config.json
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
        if (( ! $+confs[$name] )); then
            echo "error: $name, unknown configuration."
            $force || exit 2
            continue
        fi
        if is_tool_config $name; then
            echo "$name is an external benchmark backend, not compiling an Acacia config."
            continue
        fi
        build=build_$name
        log=_bm-logs/$name.log
        if [[ -e $build/compiled ]]; then
            if [[ ! -x $build/src/acacia-bonsai ]]; then
                echo "$name has a stale compiled marker, but $build/src/acacia-bonsai is missing or not executable"
                $force || exit 3
                continue
            fi
            echo "$name already compiled, remove $build/compiled to recompile"
            continue
        fi
        if ! [[ -e $build ]]; then
	    echo "$name isn't built, skipping."
	    continue
        fi
        if ! build_config_matches $name $build; then
            echo "$name build config metadata is missing or stale; remove $build to rebuild"
            $force || exit 3
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
        if (( ! $+confs[$name] )); then
            echo "error: $name, unknown configuration."
            $force || exit 2
            continue
        fi
        build=$(benchmark_build_for $name)
        if [[ -z $build ]]; then
            echo "skipping $name, no compiled Acacia build is available to host Meson test metadata"
            $force || exit 4
            continue
        fi
        log_suffix=
        if (( ${#meson_slice} )); then
            slice_label=${meson_slice[1]#--slice=}
            log_suffix=-slice-${slice_label/\//-of-}
        fi
        log=_bm-logs/$name$log_suffix.log
        meta=_bm-logs/$name$log_suffix.meta
        marker=$build/benchmarked$log_suffix
        is_tool_config $name && marker=$build/benchmarked-$name$log_suffix
        if [[ ! -e $build/compiled ]]; then
            echo "skipping uncompiled $name, create $build/compiled if compiled by hand"
            $force || exit 4
            continue
        fi
        if ! is_tool_config $name && ! build_config_matches $name $build; then
            echo "skipping stale-config $name, remove $build to rebuild with current preset metadata"
            $force || exit 4
            continue
        fi
        if ! is_tool_config $name && [[ ! -x $build/src/acacia-bonsai ]]; then
            echo "skipping uncompiled $name, missing executable $build/src/acacia-bonsai"
            $force || exit 4
            continue
        fi
        if [[ -e $marker ]]; then
            echo "skipping already benchmarked $name, remove $marker to rebenchmark"
            continue
        fi
        cd $build
	rm -f ../$log
	## For external benchmark backends, redirect every --suite=ab/…
	## argument to the backend's Meson suite prefix so that the same meson
	## run picks up the tool-backed benchmarks instead of the ab ones.
	run_prefix=()
	if is_tool_config $name; then
	    suite_prefix=${tool_suite_prefixes[$name]:-$name}
	    this_suites=()
	    for b in $benchsuites; do
	        if [[ $b == --suite=ab/* ]]; then
	            this_suites+=(--suite="${suite_prefix}/${b#--suite=ab/}")
	        else
	            this_suites+=("$b")
	        fi
	    done
	    if [[ -n ${tool_envs[$name]} ]]; then
	        env_args=("${(@f)tool_envs[$name]}")
	        run_prefix=(env "${env_args[@]}")
	    fi
	else
	    this_suites=($benchsuites)
	fi
	rm -f ../$meta
	{
	    print -r -- "config=$name"
	    if is_tool_config $name; then
	        print -r -- "kind=external-tool"
	        print -r -- "description=${tool_descriptions[$name]}"
	        print -r -- "suite_prefix=${tool_suite_prefixes[$name]:-$name}"
	        if [[ -n ${tool_envs[$name]} ]]; then
	            print -r -- "tool_env_begin"
	            print -r -- "${tool_envs[$name]}"
	            print -r -- "tool_env_end"
	        fi
	    else
	        print -r -- "kind=acacia"
	        print -r -- "cxxflags=$opt ${confs[$name]}"
	        if [[ -e .acacia-config.json ]]; then
	            print -r -- "normalized_config_json_begin"
	            cat .acacia-config.json
	            print -r -- "normalized_config_json_end"
	        fi
	    fi
	    print -r -- "build=$build"
	    print -r -- "suites=${(j: :)this_suites}"
	    print -r -- "slice=${(j: :)meson_slice}"
	    print -r -- "timeout_factor=$TIMEOUT_FACTOR"
	    print -r -- "justtest=$justtest"
	    print -r -- "benchmark_cgroup_enabled=$BENCHMARK_CGROUP_ENABLED"
	    print -r -- "benchmark_cgroup_scope=$BENCHMARK_CGROUP_SCOPE"
	    print -r -- "benchmark_cgroup_memory_max=$BENCHMARK_CGROUP_MEMORY_MAX"
	    print -r -- "benchmark_cgroup_swap_max=$BENCHMARK_CGROUP_SWAP_MAX"
	    print -r -- "benchmark_compile_profile=$BENCHMARK_COMPILE_PROFILE"
	    print -r -- "benchmark_compile_jobs=$BENCHMARK_COMPILE_JOBS"
	    print -r -- "benchmark_test_jobs=$BENCHMARK_TEST_JOBS"
	} > ../$meta
	rm -f meson-logs/testlog.json
	test_status=0
	meson_jobs=(--num-processes "$BENCHMARK_TEST_JOBS")
	if $justtest; then
	    echo -n "testing $name on $this_suites (logfile: $log)... "
	    run_benchmark_command $name $run_prefix meson test --no-rebuild $meson_jobs $this_suites $meson_slice -t $TIMEOUT_FACTOR &>> ../$log || test_status=$?
	else
	    echo -n "benchmarking $name on $this_suites (logfile: $log)... "
	    run_benchmark_command $name $run_prefix meson test --no-rebuild --benchmark $meson_jobs $this_suites $meson_slice -t $TIMEOUT_FACTOR &>> ../$log || test_status=$?
	fi
        if [[ ! -s meson-logs/testlog.json ]]; then
            echo "FAILED; Meson did not produce meson-logs/testlog.json for $this_suites"
            $force || exit 5
            cd ..
            continue
        fi
        if grep -q '^Fail:[[:space:]]*[1-9]' ../$log ||
           grep -q 'FAILED:' ../$log ||
           { (( test_status != 0 )) && ! grep -q '^Timeout:[[:space:]]*[1-9]' ../$log; }; then
            echo "FAILED; testlog stored at $log, _bm-logs/$name$log_suffix.json left untouched"
            $force || exit 5
        else
            echo "done; testlog stored at $log"
            touch $marker:t
        fi
        cd ..
        cp $build/meson-logs/testlog.json _bm-logs/$name$log_suffix.json
    done
fi
