#!/usr/bin/env bash

# Execute the long-running measurement tail of the ltlsynt-gap plan.
# This script is deliberately sequential.  Every solver invocation is either
# driven by self-benchmark's strict solver scope or run_diag_targets' systemd
# scope, both with 8 GiB MemoryMax and swap disabled.

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

SESSION_NAME=${SESSION_NAME:-gap-plan-overnight-20260804}
CAMPAIGN_ROOT=${CAMPAIGN_ROOT:-_bm-logs.gap-plan-20260804}
LOG="_bm-logs/${SESSION_NAME}.log"
MEMORY_MAX=${MEMORY_MAX:-8G}
SWAP_MAX=${SWAP_MAX:-0}

mkdir -p _bm-logs "$CAMPAIGN_ROOT"
exec > >(tee -a "$LOG") 2>&1

log() {
  printf '[%s] %s\n' "$(date --iso-8601=seconds)" "$*"
}

run() {
  log "START $*"
  "$@"
  log "DONE $*"
}

export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export BENCHMARK_CGROUP=strict
export BENCHMARK_CGROUP_SCOPE=solver
export BENCHMARK_CGROUP_MEMORY_MAX=$MEMORY_MAX
export BENCHMARK_CGROUP_SWAP_MAX=$SWAP_MAX
export BENCHMARK_TEST_JOBS=1
export BENCHMARK_COMPILE_JOBS=1
export BENCHMARK_TOOL_HOST_BUILD=build_best_decomp_mona

{
  echo "session=$SESSION_NAME"
  echo "git_commit=$(git rev-parse HEAD)"
  echo "cgroup_scope=solver"
  echo "cgroup_memory_max=$MEMORY_MAX"
  echo "cgroup_swap_max=$SWAP_MAX"
  echo "test_jobs=1"
  python3 scripts/spot-metadata.py
} >"$CAMPAIGN_ROOT/campaign.meta"

wait_for_rebaseline() {
  local attempt
  local -a markers=(
    build_best_decomp_mona/benchmarked
    build_best_decomp_mona_race/benchmarked
    build_best_decomp_mona/benchmarked-ltlsynt
  )
  for ((attempt = 1; attempt <= 1080; attempt++)); do
    if [[ -f ${markers[0]} && -f ${markers[1]} && -f ${markers[2]} ]]; then
      log "Three-way 2024 rebaseline markers are complete"
      return 0
    fi
    if (( attempt % 15 == 0 )); then
      log "Waiting for the in-progress 2024 rebaseline (${attempt}/1080)"
    fi
    sleep 20
  done
  log "ERROR: timed out waiting six hours for the 2024 rebaseline"
  return 1
}

validate_json_rows() {
  local expected=$1
  shift
  python3 - "$expected" "$@" <<'PY'
import json
import pathlib
import sys

expected = int(sys.argv[1])
for raw in sys.argv[2:]:
    path = pathlib.Path(raw)
    with path.open() as handle:
        rows = [json.loads(line) for line in handle if line.strip()]
    if len(rows) != expected:
        raise SystemExit(f"{path}: expected {expected} rows, found {len(rows)}")
    print(f"validated {path}: {len(rows)} rows")
PY
}

wait_for_rebaseline
validate_json_rows 274 \
  _bm-logs/best_decomp_mona.json \
  _bm-logs/best_decomp_mona_race.json \
  _bm-logs/ltlsynt.json

REBASELINE="$CAMPAIGN_ROOT/syntcomp24-rebaseline"
mkdir -p "$REBASELINE"
for config in best_decomp_mona best_decomp_mona_race ltlsynt; do
  cp "_bm-logs/${config}.json" "$REBASELINE/"
  cp "_bm-logs/${config}.log" "$REBASELINE/"
  cp "_bm-logs/${config}.meta" "$REBASELINE/"
done
cp tests/suites/benchmarks/syntcomp24/panel.list \
  "$REBASELINE/panel.pre-rebaseline.list"
cp tests/suites/benchmarks/syntcomp24/panel.tsv \
  "$REBASELINE/panel.pre-rebaseline.tsv"

run python3 benchmarking/make-panel.py \
  --reference "$REBASELINE" \
  --acacia best_decomp_mona \
  --ltlsynt ltlsynt \
  --corpus tests/ltl/syntcomp24 \
  --output tests/suites/benchmarks/syntcomp24/panel \
  --cap 17 \
  --seed 20260804
cp tests/suites/benchmarks/syntcomp24/panel.list \
  "$REBASELINE/panel.post-rebaseline.list"
cp tests/suites/benchmarks/syntcomp24/panel.tsv \
  "$REBASELINE/panel.post-rebaseline.tsv"

DIAG_CONFIG=best_decomp_mona_diag
DIAG_BUILD="build_${DIAG_CONFIG}"
if [[ -d $DIAG_BUILD ]]; then
  if [[ ! -f $DIAG_BUILD/.acacia-config.json ]] ||
     ! python3 scripts/acacia-config.py show "$DIAG_CONFIG" |
       cmp -s - "$DIAG_BUILD/.acacia-config.json"; then
    log "REMOVE stale build $DIAG_BUILD"
    rm -rf -- "$DIAG_BUILD"
  fi
fi
run ./self-benchmark.sh -R -c "$DIAG_CONFIG"

mapfile -t GAP_TARGETS < <(
  awk -F '\t' 'NR > 1 && $2 == "gap" { print $1 }' \
    tests/suites/benchmarks/syntcomp24/panel.tsv
)
if (( ${#GAP_TARGETS[@]} == 0 )); then
  log "ERROR: regenerated 2024 panel has no gap targets"
  exit 1
fi
run python3 benchmarking/run_diag_targets.py \
  --build "$DIAG_BUILD" \
  --suite-dir tests/ltl/syntcomp24 \
  --timeout 120 \
  --memory-max "$MEMORY_MAX" \
  --memory-swap-max "$SWAP_MAX" \
  --progress-every 1024 \
  --systemd-scope \
  --csv "$CAMPAIGN_ROOT/syntcomp24-gap-diagnostics.csv" \
  "${GAP_TARGETS[@]}"

TLSF_SOURCE=selection-ltl-2025v2/selection-ltl-2025
TLSF_OUTPUT=tests/ltl/syntcomp25
TLSF_LIST=tests/suites/benchmarks/syntcomp25/all.list
run python3 benchmarking/convert-tlsf-corpus.py \
  "$TLSF_SOURCE" "$TLSF_OUTPUT" --list-output "$TLSF_LIST"
python3 - "$TLSF_OUTPUT/skipped.tsv" <<'PY'
import csv
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
rows = list(csv.DictReader(path.open(), delimiter="\t"))
if len(rows) != 7:
    raise SystemExit(f"{path}: expected 7 strong-next skips, found {len(rows)}")
bad = [row for row in rows if "strong next" not in row["reason"].lower()]
if bad:
    raise SystemExit(f"{path}: non-strong-next reasons: {bad}")
print(f"validated {len(rows)} strong-next skips")
PY
run python3 benchmarking/check-tlsf-conversion.py \
  "$TLSF_SOURCE" "$TLSF_OUTPUT" --count 10 --seed 20260804

run meson setup --reconfigure build_best_decomp_mona
for slice in 1 2 3 4; do
  rm -f \
    "build_best_decomp_mona/benchmarked-slice-${slice}-of-4" \
    "build_best_decomp_mona/benchmarked-ltlsynt-slice-${slice}-of-4" \
    "_bm-logs/best_decomp_mona-slice-${slice}-of-4.json" \
    "_bm-logs/best_decomp_mona-slice-${slice}-of-4.log" \
    "_bm-logs/best_decomp_mona-slice-${slice}-of-4.meta" \
    "_bm-logs/ltlsynt-slice-${slice}-of-4.json" \
    "_bm-logs/ltlsynt-slice-${slice}-of-4.log" \
    "_bm-logs/ltlsynt-slice-${slice}-of-4.meta"
done

run env \
  SESSION_NAME="${SESSION_NAME}-syntcomp25" \
  SUITE=ab/syntcomp25/all \
  TIMEOUT_FACTOR=1.7 \
  MEMORY_MAX="$MEMORY_MAX" \
  SWAP_MAX="$SWAP_MAX" \
  SLICES=4 \
  TOOL_SLICES=4 \
  ACACIA_CONFIGS=best_decomp_mona \
  TOOL_CONFIGS=ltlsynt \
  scripts/overnight-benchmark-session.sh

SYN25_REF="$CAMPAIGN_ROOT/syntcomp25-reference"
run python3 benchmarking/aggregate_bm_slices.py _bm-logs \
  --out "$SYN25_REF" \
  --config best_decomp_mona \
  --config ltlsynt \
  --min-slices 4
cp "_bm-logs/${SESSION_NAME}-syntcomp25.campaign.meta" \
  "$SYN25_REF/campaign.meta"
cp "_bm-logs/${SESSION_NAME}-syntcomp25.summary" \
  "$SYN25_REF/campaign.summary"
validate_json_rows "$(find "$TLSF_OUTPUT" -maxdepth 1 -type f -name '*.ltl' | wc -l)" \
  "$SYN25_REF/best_decomp_mona.json" \
  "$SYN25_REF/ltlsynt.json"

run python3 benchmarking/make-panel.py \
  --reference "$SYN25_REF" \
  --acacia best_decomp_mona \
  --ltlsynt ltlsynt \
  --corpus "$TLSF_OUTPUT" \
  --output tests/suites/benchmarks/syntcomp25/panel \
  --cap 17 \
  --seed 20260804
run meson setup --reconfigure build_best_decomp_mona
meson test -C build_best_decomp_mona --benchmark --list \
  --suite=ab/syntcomp25/panel >"$SYN25_REF/panel.meson-list.txt"

touch "$CAMPAIGN_ROOT/campaign.complete"
log "CAMPAIGN COMPLETE: $CAMPAIGN_ROOT"
