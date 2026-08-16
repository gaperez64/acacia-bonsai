#!/usr/bin/env bash
set -Eeuo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
candidate=${1:?usage: run-stepc-g3.sh CANDIDATE-BIN [OUTPUT-DIR]}
output=${2:-_bm-logs.stepc-g3-20260814}
baseline_bin=${STEP_C_BASELINE_BIN:-$repo_root/build_best_decomp_mona/src/acacia-bonsai}

cd "$repo_root"
mkdir -p "$output"
printf 'RUNNING\n' > "$output/status.txt"
gate_complete=0
trap 'rc=$?; if (( rc == 0 && gate_complete == 1 )); then printf "COMPLETE PASS\n" > "$output/status.txt"; else (( rc != 0 )) || rc=1; printf "COMPLETE FAIL exit=%d\n" "$rc" > "$output/status.txt"; fi' EXIT

run_panel () {
  local suite=$1 list=$2 baseline=$3
  local candidate_csv="$output/candidate-$suite.csv"
  python3 benchmarking/run-subset.py \
    --bin "$candidate" \
    --instances-dir "tests/ltl/$suite" \
    --list "$list" \
    --timeout 17 \
    --csv "$candidate_csv"
  ACACIA_OUTER_CGROUP=1 python3 benchmarking/landing-bar.py \
    "$baseline" "$candidate_csv" \
    --timeout 17 \
    --baseline-bin "$baseline_bin" \
    --candidate-bin "$candidate" \
    --instances-dir "tests/ltl/$suite" \
    > "$output/landing-$suite.txt"
}

run_panel syntcomp24 tests/suites/benchmarks/syntcomp24/panel.list \
  _bm-logs.step1-picker/baseline-syntcomp24-panel.csv
run_panel syntcomp21 tests/suites/benchmarks/syntcomp21/crit.list \
  _bm-logs.step1-picker/baseline-syntcomp21-crit.csv
run_panel syntcomp25 tests/suites/benchmarks/syntcomp25/panel.list \
  _bm-logs.step1-picker/baseline-syntcomp25-panel.csv

printf 'GATE PASS\n' > "$output/summary.txt"
for report in "$output"/landing-*.txt; do
  printf '\n== %s ==\n' "$(basename "$report")" >> "$output/summary.txt"
  cat "$report" >> "$output/summary.txt"
done
gate_complete=1
