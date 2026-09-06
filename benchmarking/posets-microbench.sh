#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
  cat <<EOF
usage: $0 [--diff POSETS-REV] [--target PHASE[,PHASE...] | --guard-only]
          [--calibrate REPETITIONS] [--output DIR]

Run the pinned Posets downset/SIMD microbenchmarks under perf stat.  With
--diff, compare the current submodule working tree against POSETS-REV.  The
target may also be supplied as POSETS_MICROBENCH_TARGET; it is required for a
diff gate unless --guard-only is used for a target measured outside Posets.
POSETS_MICROBENCH_BACKENDS defaults to vector_backed.  Performance thresholds
are advisory; missing measurements and command failures remain hard errors.
EOF
  exit 2
}

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
posets_src="$repo_root/subprojects/posets"
candidate_build="$posets_src/build-hotloop"
base_rev=""
target=${POSETS_MICROBENCH_TARGET:-}
guard_only=0
calibrate=0
repetitions=${POSETS_MICROBENCH_REPETITIONS:-2}
output=""

while (($#)); do
  case $1 in
    --diff)
      [[ $# -ge 2 ]] || usage
      base_rev=$2
      shift 2
      ;;
    --target)
      [[ $# -ge 2 ]] || usage
      target=$2
      shift 2
      ;;
    --guard-only)
      guard_only=1
      shift
      ;;
    --calibrate)
      [[ $# -ge 2 && $2 =~ ^[1-9][0-9]*$ ]] || usage
      calibrate=1
      repetitions=$2
      shift 2
      ;;
    --output)
      [[ $# -ge 2 ]] || usage
      output=$2
      shift 2
      ;;
    -h|--help) usage ;;
    *) usage ;;
  esac
done

if [[ -n $target && $guard_only == 1 ]]; then
  echo "GATE FAIL: --target and --guard-only are mutually exclusive"
  exit 1
fi
if ((calibrate == 1)) && [[ -n $base_rev || -n $target || $guard_only == 1 ]]; then
  echo "GATE FAIL: --calibrate cannot be combined with --diff, --target, or --guard-only"
  exit 1
fi
if [[ -n $base_rev && -z $target && $guard_only == 0 ]]; then
  echo "GATE FAIL: --diff requires --target, POSETS_MICROBENCH_TARGET, or --guard-only"
  exit 1
fi
command -v perf >/dev/null || { echo "GATE FAIL: perf is not installed"; exit 1; }
command -v systemd-run >/dev/null || { echo "GATE FAIL: systemd-run is not installed"; exit 1; }

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
output=${output:-/tmp/posets-microbench-results.$timestamp}
mkdir -p "$output"
output=$(realpath "$output")
results="$output/results.tsv"
samples="$output/samples.tsv"
printf 'revision\tbinary\tbackend\tphase\tcycles\tperf_file\n' >"$results"
printf 'revision\tbinary\tbackend\tphase\trepetition\tcycles\tperf_file\n' >"$samples"

scratch=""
base_src=""
gate_reported=0
cleanup() {
  local status=$?
  if [[ -n $base_src && -e $base_src ]]; then
    git -C "$posets_src" worktree remove --force "$base_src" >/dev/null 2>&1 || true
  fi
  if [[ -n $scratch && -d $scratch ]]; then
    rm -rf "$scratch"
  fi
  if ((status != 0 && gate_reported == 0)); then
    echo "GATE FAIL: microbenchmark command failed (exit $status)"
  fi
}
trap cleanup EXIT

cgroup_run_seq=0

cgroup_run() {
  # A fresh unit per invocation: a scope left behind in failed state would
  # make the next systemd-run refuse the name.
  cgroup_run_seq=$((cgroup_run_seq + 1))
  systemd-run --user --scope --quiet \
    --unit=acacia-posets-microbench-$$-$cgroup_run_seq \
    --property=MemoryMax=8G --property=MemorySwapMax=0 "$@"
}

configure_build() {
  local source=$1 build=$2
  if [[ -d $build/meson-private ]]; then
    cgroup_run meson setup --reconfigure "$build" "$source" \
      -Dcpp_std=c++23 -Dcpp_args="-march=native -Ofast -DNDEBUG"
  else
    cgroup_run meson setup "$build" "$source" \
      -Dcpp_std=c++23 -Dcpp_args="-march=native -Ofast -DNDEBUG"
  fi
  cgroup_run meson compile -C "$build" -j 1 downset-bm downset-bm-d128 simd-bm
}

counter_value() {
  local file=$1 event=$2
  awk -F '\t' -v event="$event" '
    $3 ~ ("^" event "(:u)?$") {
      value=$1
      gsub(/[ ,]/, "", value)
      if (value ~ /^[0-9]+$/) { print value; exit }
    }
  ' "$file"
}

case_counter=0
run_perf() {
  local perf_file=$1 stdout_file=$2
  shift 2
  case_counter=$((case_counter + 1))
  cgroup_run perf stat --no-big-num -x $'\t' -o "$perf_file" \
    -e cycles,instructions,cache-references,cache-misses,LLC-load-misses,branch-misses \
    -- "$@" >"$stdout_file" 2>&1
}

record_case() {
  local revision=$1 build=$2 binary=$3 backend=$4 phase=$5
  shift 5
  local best_cycles="" best_perf=""
  for ((repetition = 1; repetition <= repetitions; ++repetition)); do
    local stem="$output/${revision}-${binary}-${backend}-${phase}-r${repetition}"
    local perf_file="${stem}.perf" stdout_file="${stem}.stdout"
    run_perf "$perf_file" "$stdout_file" "$@"
    local cycles
    cycles=$(counter_value "$perf_file" cycles)
    if [[ -z $cycles ]]; then
      echo "GATE FAIL: cycles were not counted for $revision/$binary/$backend/$phase"
      gate_reported=1
      exit 1
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$revision" "$binary" "$backend" "$phase" "$repetition" "$cycles" "$perf_file" \
      >>"$samples"
    if [[ -z $best_cycles || $cycles -lt $best_cycles ]]; then
      best_cycles=$cycles
      best_perf=$perf_file
    fi
  done
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$revision" "$binary" "$backend" "$phase" "$best_cycles" "$best_perf" >>"$results"
}

phase_params() {
  local binary=$1 phase=$2 tail
  if [[ $binary == downset-bm-d128 ]]; then
    tail=32
  else
    tail=2
  fi
  local build=0 query=0 transfer=0 intersection=0 union=0 cpre=0
  case $phase in
    build) build=4096 ;;
    query) build=1024; query=8192 ;;
    transfer) build=512; transfer=200000 ;;
    intersection)
      [[ $binary == downset-bm-d128 ]] && intersection=64 || intersection=192
      ;;
    union)
      [[ $binary == downset-bm-d128 ]] && union=512 || union=2048
      ;;
    cpre)
      [[ $binary == downset-bm-d128 ]] && cpre=64 || cpre=192
      ;;
    *) echo "unknown phase: $phase" >&2; return 2 ;;
  esac
  printf 'maxval=12,build=%s,query=%s,transfer=%s,intersection=%s,union=%s,cpre=%s,cpre_actions=8,bitset_tail=%s,rounds=1' \
    "$build" "$query" "$transfer" "$intersection" "$union" "$cpre" "$tail"
}

benchmark_revision() {
  local revision=$1 build=$2
  local backends=${POSETS_MICROBENCH_BACKENDS:-vector_backed}
  local phases=${POSETS_MICROBENCH_PHASES:-"build query transfer intersection union cpre"}
  local vector='simd_array_sum_and_bitset_fixed<test_value_type>'
  IFS=',' read -r -a backend_list <<<"$backends"
  for binary in downset-bm downset-bm-d128; do
    for backend in "${backend_list[@]}"; do
      for phase in $phases; do
        local params
        params=$(phase_params "$binary" "$phase")
        record_case "$revision" "$build" "$binary" "$backend" "$phase" \
          "$build/tests/$binary" --params="$params" "$backend" "$vector"
      done
    done
  done
  record_case "$revision" "$build" simd-bm vector_backed simd \
    "$build/tests/simd-bm" vector_backed 'simd_array_backed_sum_fixed<test_value_type>'
}

configure_build "$posets_src" "$candidate_build"

if [[ -n $base_rev ]]; then
  scratch=$(mktemp -d /tmp/posets-microbench-base.XXXXXX)
  base_src="$scratch/src"
  base_build="$scratch/build-hotloop"
  git -C "$posets_src" worktree add --detach "$base_src" "$base_rev" >/dev/null
  configure_build "$base_src" "$base_build"
  benchmark_revision base "$base_build"
fi
benchmark_revision candidate "$candidate_build"

echo "results: $results"
echo "samples: $samples"
if ((calibrate == 1)); then
  noise_floor="$output/noise-floor.tsv"
  awk -F '\t' '
    BEGIN {
      OFS="\t"
      print "binary", "backend", "phase", "repetitions", "min_cycles", "max_cycles", \
            "spread_percent"
    }
    NR == 1 { next }
    {
      key=$2 SUBSEP $3 SUBSEP $4
      binary[key]=$2
      backend[key]=$3
      phase[key]=$4
      count[key]++
      if (!(key in min) || $6 < min[key]) min[key]=$6
      if (!(key in max) || $6 > max[key]) max[key]=$6
    }
    END {
      maximum=0
      for (key in count) {
        spread=100.0*(max[key]-min[key])/min[key]
        print binary[key], backend[key], phase[key], count[key], min[key], max[key], \
              sprintf("%.2f", spread)
        if (spread > maximum) maximum=spread
      }
      printf "maximum_observed_spread_percent\t%.2f\n", maximum > "/dev/stderr"
    }
  ' "$samples" >"$noise_floor"
  echo "noise floor: $noise_floor"
  gate_reported=1
  echo "GATE PASS"
  exit 0
fi
if [[ -z $base_rev ]]; then
  gate_reported=1
  echo "GATE PASS"
  exit 0
fi

set +e
awk -F '\t' -v targets="$target" -v target_floor=8.0 -v guard_ceiling=8.0 '
  BEGIN {
    if (targets != "") {
      split(targets, target_names, ",")
      for (i in target_names) target[target_names[i]]=1
    }
  }
  NR == 1 { next }
  $1 == "base" { baseline[$4]+=$5; phases[$4]=1 }
  $1 == "candidate" { candidate[$4]+=$5; phases[$4]=1 }
  END {
    failures=0
    for (phase in phases) {
      if (!(phase in baseline) || !(phase in candidate) || baseline[phase] == 0) {
        printf "- %s: missing comparable cycle count\n", phase
        failures++
        continue
      }
      improvement=100.0*(baseline[phase]-candidate[phase])/baseline[phase]
      printf "%s: base=%.0f candidate=%.0f change=%+.2f%%\n", \
             phase, baseline[phase], candidate[phase], improvement
      if (phase in target && improvement < target_floor) {
        printf "ADVISORY: target %s improved only %.2f%% (< %.1f%% noise floor)\n", \
               phase, improvement, target_floor
      } else if (!(phase in target) && improvement < -guard_ceiling) {
        printf "ADVISORY: non-target %s regressed %.2f%% (> %.1f%% noise floor)\n", \
               phase, -improvement, guard_ceiling
      }
    }
    for (phase in target)
      if (!(phase in phases)) {
        printf "- target %s was not measured\n", phase
        failures++
      }
    exit failures ? 1 : 0
  }
' "$results"
compare_status=$?
set -e

gate_reported=1
if ((compare_status != 0)); then
  echo "GATE FAIL: microbenchmark comparison incomplete"
  exit 1
fi
echo "GATE PASS (performance thresholds advisory)"
