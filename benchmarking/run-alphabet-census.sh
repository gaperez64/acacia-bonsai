#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-build_best_decomp_mona_diag}
output_dir=${2:-_bm-logs.alphabet-census-20260814}
timeout_seconds=${ALPHABET_CENSUS_TIMEOUT:-120}

cd "$repo_root"
mkdir -p "$output_dir"
status_file="$output_dir/status.txt"
rm -f \
  "$output_dir/syntcomp24-action-construction.csv" \
  "$output_dir/syntcomp25-fixpoint.csv" \
  "$output_dir/descents.tsv" \
  "$output_dir/summary.txt"
printf 'RUNNING\n' > "$status_file"
trap 'rc=$?; if (( rc == 0 )); then printf "COMPLETE\n" > "$status_file"; else printf "FAILED exit=%d\n" "$rc" > "$status_file"; fi' EXIT

mapfile -t targets24 < <(
  awk -F '\t' 'NR > 1 && $2 == "action-construction-bound" {print "tests/ltl/syntcomp24/" $1}' \
    _bm-logs.gap-plan-20260804/syntcomp24-gap-phases.tsv
)
mapfile -t targets25 < <(
  awk -F '\t' 'NR > 1 && $2 == "fixpoint-bound" {print "tests/ltl/syntcomp25/" $1}' \
    _bm-logs.gap-plan-20260804/syntcomp25-gap-phases.tsv
)

if [[ ${#targets24[@]} -ne 26 || ${#targets25[@]} -ne 32 ]]; then
  echo "unexpected cohort sizes: syntcomp24=${#targets24[@]} syntcomp25=${#targets25[@]}" >&2
  exit 2
fi

python3 benchmarking/run_diag_targets.py \
  --build "$build" \
  --timeout "$timeout_seconds" \
  --stream-diagnostics \
  --alphabet-census-only \
  --flags='-r small -u formula' \
  --csv "$output_dir/syntcomp24-action-construction.csv" \
  "${targets24[@]}"

python3 benchmarking/run_diag_targets.py \
  --build "$build" \
  --timeout "$timeout_seconds" \
  --stream-diagnostics \
  --alphabet-census-only \
  --flags='-r small -u formula' \
  --csv "$output_dir/syntcomp25-fixpoint.csv" \
  "${targets25[@]}"

python3 benchmarking/alphabet-census-summary.py \
  --tsv "$output_dir/descents.tsv" \
  "$output_dir/syntcomp24-action-construction.csv" \
  "$output_dir/syntcomp25-fixpoint.csv" \
  > "$output_dir/summary.txt"
cat "$output_dir/summary.txt"
