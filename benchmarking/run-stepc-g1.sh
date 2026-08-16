#!/usr/bin/env bash
set -Eeuo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
candidate_build=${1:?usage: run-stepc-g1.sh CANDIDATE-BUILD [OUTPUT-DIR]}
output=${2:-_bm-logs.stepc-g1-20260814}
baseline_bin=${STEP_C_BASELINE_BIN:-$repo_root/build_best_decomp_mona/src/acacia-bonsai}

cd "$repo_root"
mkdir -p "$output"
printf 'RUNNING\n' > "$output/status.txt"
gate_complete=0
trap 'rc=$?; if (( rc == 0 && gate_complete == 1 )); then printf "COMPLETE PASS\n" > "$output/status.txt"; else (( rc != 0 )) || rc=1; printf "COMPLETE FAIL exit=%d\n" "$rc" > "$output/status.txt"; fi' EXIT

REGRESSION_OUTER_CGROUP=1 \
  benchmarking/regression-gate.sh --baseline-bin "$baseline_bin" "$candidate_build" \
  > "$output/g1.txt" 2>&1

tail -24 "$output/g1.txt" > "$output/summary.txt"
gate_complete=1
