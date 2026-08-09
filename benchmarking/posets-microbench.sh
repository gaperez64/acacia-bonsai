#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
  cat <<EOF
usage: $0 [--diff POSETS-REV] [--target PHASE[,PHASE...]] [--output DIR]

Run the pinned Posets downset/SIMD microbenchmarks under perf stat.  With
--diff, compare the current submodule working tree against POSETS-REV.  The
target may also be supplied as POSETS_MICROBENCH_TARGET; it is required for a
diff gate.  POSETS_MICROBENCH_BACKENDS defaults to vector_backed.
EOF
  exit 2
}

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
posets_src="$repo_root/subprojects/posets"
candidate_build="$posets_src/build-hotloop"
base_rev=""
target=${POSETS_MICROBENCH_TARGET:-}
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
    --output)
      [[ $# -ge 2 ]] || usage
      output=$2
      shift 2
      ;;
    -h|--help) usage ;;
    *) usage ;;
  esac
done

if [[ -n $base_rev && -z $target ]]; then
  echo "GATE FAIL: --diff requires --target or POSETS_MICROBENCH_TARGET"
  exit 1
fi
command -v perf >/dev/null || { echo "GATE FAIL: perf is not installed"; exit 1; }
command -v systemd-run >/dev/null || { echo "GATE FAIL: systemd-run is not installed"; exit 1; }

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
output=${output:-/tmp/posets-microbench-results.$timestamp}
mkdir -p "$output"
output=$(realpath "$output")
results="$output/results.tsv"
printf 'revision\tbinary\tbackend\tphase\tcycles\tperf_file\n' >"$results"

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

cgroup_run() {
  systemd-run --user --scope --quiet \
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
  for repetition in 1 2; do
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
if [[ -z $base_rev ]]; then
  gate_reported=1
  echo "GATE PASS"
  exit 0
fi

set +e
awk -F '\t' -v targets="$target" '
  BEGIN {
    split(targets, target_names, ",")
    for (i in target_names) target[target_names[i]]=1
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
      if (phase in target && improvement < 5.0) {
        printf "- target %s improved only %.2f%% (< 5%%)\n", phase, improvement
        failures++
      } else if (!(phase in target) && improvement < -5.0) {
        printf "- non-target %s regressed %.2f%% (> 5%%)\n", phase, -improvement
        failures++
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
  echo "GATE FAIL: microbenchmark thresholds not met"
  exit 1
fi
echo "GATE PASS"
