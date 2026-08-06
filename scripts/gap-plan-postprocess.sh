#!/usr/bin/env bash

# Finish the evidence products that depend on the long 2025 campaign.  This is
# designed to be started while gap-plan-overnight.sh is still running: it waits
# for that service to stop, requires the completion marker, and only then runs
# the small landing-gate repeat panel.

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

UPSTREAM_UNIT=${UPSTREAM_UNIT:-acacia-gap-plan-overnight-20260805-r4.service}
CAMPAIGN_ROOT=${CAMPAIGN_ROOT:-_bm-logs.gap-plan-20260804}
SESSION_NAME=${SESSION_NAME:-gap-plan-postprocess-20260805}
MEMORY_MAX=${MEMORY_MAX:-8G}
SWAP_MAX=${SWAP_MAX:-0}
REPEATS=${REPEATS:-7}
LOG="_bm-logs/${SESSION_NAME}.log"

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

while systemctl --user is-active --quiet "$UPSTREAM_UNIT"; do
  sleep 60
done

if [[ ! -f "$CAMPAIGN_ROOT/campaign.complete" ]]; then
  log "ERROR: $UPSTREAM_UNIT stopped without $CAMPAIGN_ROOT/campaign.complete"
  touch "$CAMPAIGN_ROOT/postprocess.failed"
  exit 1
fi

DIAG_CSV="$CAMPAIGN_ROOT/syntcomp24-gap-diagnostics.csv"
PHASE_TSV="$CAMPAIGN_ROOT/syntcomp24-gap-phases.tsv"
run python3 benchmarking/summarize-diag-phases.py \
  "$DIAG_CSV" --output "$PHASE_TSV"

# The first pass predates the explicit post-actioner checkpoint.  Rebuild only
# the diagnostics configuration and repeat the same gap targets without
# periodic trace lines, so before-solve (action construction) can be separated
# cleanly from after-action-construction (fixed point).
DIAG_CONFIG=best_decomp_mona_diag
DIAG_BUILD="build_${DIAG_CONFIG}"
rm -f "$DIAG_BUILD/compiled"
export BENCHMARK_CGROUP=strict
export BENCHMARK_CGROUP_SCOPE=solver
export BENCHMARK_CGROUP_MEMORY_MAX=$MEMORY_MAX
export BENCHMARK_CGROUP_SWAP_MAX=$SWAP_MAX
export BENCHMARK_TEST_JOBS=1
export BENCHMARK_COMPILE_JOBS=1
run ./self-benchmark.sh -R -c "$DIAG_CONFIG"
mapfile -t GAP_TARGETS < <(
  awk -F '\t' 'NR > 1 && $2 == "gap" { print $1 }' \
    tests/suites/benchmarks/syntcomp24/panel.tsv
)
if (( ${#GAP_TARGETS[@]} == 0 )); then
  log "ERROR: regenerated 2024 panel has no gap targets"
  exit 1
fi
REFINED_DIAG="$CAMPAIGN_ROOT/syntcomp24-gap-diagnostics-refined.csv"
REFINED_PHASES="$CAMPAIGN_ROOT/syntcomp24-gap-phases-refined.tsv"
run python3 benchmarking/run_diag_targets.py \
  --build "$DIAG_BUILD" \
  --suite-dir tests/ltl/syntcomp24 \
  --timeout 120 \
  --memory-max "$MEMORY_MAX" \
  --memory-swap-max "$SWAP_MAX" \
  --progress-every 0 \
  --systemd-scope \
  --csv "$REFINED_DIAG" \
  "${GAP_TARGETS[@]}"
run python3 benchmarking/summarize-diag-phases.py \
  "$REFINED_DIAG" --output "$REFINED_PHASES"

SYN25_REF="$CAMPAIGN_ROOT/syntcomp25-reference"
run python3 benchmarking/rank_bm_logs.py "$SYN25_REF" --timeout 17
python3 benchmarking/rank_bm_logs.py "$SYN25_REF" --timeout 17 \
  >"$SYN25_REF/ranking.txt"

run python3 - "$CAMPAIGN_ROOT" <<'PY'
import csv
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
corpus = pathlib.Path("tests/ltl/syntcomp25")
suite = pathlib.Path("tests/suites/benchmarks/syntcomp25")
converted = sorted(corpus.glob("*.ltl"))
if len(converted) != 1579:
    raise SystemExit(f"expected 1579 converted instances, found {len(converted)}")

skipped = list(csv.DictReader((corpus / "skipped.tsv").open(), delimiter="\t"))
if len(skipped) != 7 or any("strong next" not in row["reason"].lower() for row in skipped):
    raise SystemExit(f"expected exactly seven strong-next skips, found {skipped}")

for config in ("best_decomp_mona", "ltlsynt"):
    path = root / "syntcomp25-reference" / f"{config}.json"
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    if len(rows) != len(converted):
        raise SystemExit(f"{path}: expected {len(converted)} rows, found {len(rows)}")

panel = [
    line.strip()
    for line in (suite / "panel.list").read_text().splitlines()
    if line.strip() and not line.lstrip().startswith("#")
]
provenance = list(csv.DictReader((suite / "panel.tsv").open(), delimiter="\t"))
if len(panel) != len(provenance):
    raise SystemExit(f"2025 panel list/TSV mismatch: {len(panel)} != {len(provenance)}")
if set(panel) != {row["instance"] for row in provenance}:
    raise SystemExit("2025 panel list and provenance name sets differ")
print(
    f"validated converted={len(converted)} skipped={len(skipped)} "
    f"reference_rows={len(converted)} panel={len(panel)}"
)
PY

TMP_DIR=$(mktemp -d --tmpdir acacia-panel-check.XXXXXXXX)
cleanup() {
  rm -rf -- "$TMP_DIR"
}
trap cleanup EXIT
run python3 benchmarking/make-panel.py \
  --reference "$SYN25_REF" \
  --acacia best_decomp_mona \
  --ltlsynt ltlsynt \
  --corpus tests/ltl/syntcomp25 \
  --output "$TMP_DIR/panel" \
  --cap 17 \
  --seed 20260804
cmp tests/suites/benchmarks/syntcomp25/panel.list "$TMP_DIR/panel.list"
cmp tests/suites/benchmarks/syntcomp25/panel.tsv "$TMP_DIR/panel.tsv"
log "2025 panel regeneration is byte-identical"

GATE_ROOT="$CAMPAIGN_ROOT/translation-race-landing-reruns"
mkdir -p "$GATE_ROOT"
printf '%s\n' collector_v215.ltl 05.ltl >"$GATE_ROOT/targets.list"

run_gate() {
  local config=$1
  local repeat=$2
  run python3 benchmarking/run-subset.py \
    --bin "build_${config}/src/acacia-bonsai" \
    --instances-dir tests/ltl/syntcomp24 \
    --list "$GATE_ROOT/targets.list" \
    --timeout 17 \
    --systemd-scope \
    --memory-max "$MEMORY_MAX" \
    --memory-swap-max "$SWAP_MAX" \
    --csv "$GATE_ROOT/${config}-repeat-${repeat}.csv"
}

for ((repeat = 1; repeat <= REPEATS; repeat++)); do
  if (( repeat % 2 == 1 )); then
    run_gate best_decomp_mona_race "$repeat"
    run_gate best_decomp_mona "$repeat"
  else
    run_gate best_decomp_mona "$repeat"
    run_gate best_decomp_mona_race "$repeat"
  fi
done

run python3 - "$GATE_ROOT" "$REPEATS" <<'PY'
import csv
import pathlib
import statistics
import sys

root = pathlib.Path(sys.argv[1])
repeats = int(sys.argv[2])
configs = ("best_decomp_mona", "best_decomp_mona_race")
targets = ("collector_v215.ltl", "05.ltl")
rows = []
for config in configs:
    per_target = {target: [] for target in targets}
    for repeat in range(1, repeats + 1):
        path = root / f"{config}-repeat-{repeat}.csv"
        for row in csv.DictReader(path.open()):
            per_target[row["instance"]].append(row)
    for target in targets:
        values = per_target[target]
        solved = [row for row in values if row["result"] in ("REALIZABLE", "UNREALIZABLE")]
        solved_times = [float(row["seconds"]) for row in solved]
        rows.append(
            {
                "config": config,
                "target": target,
                "solved": len(solved),
                "repeats": len(values),
                "median_solved_s": (
                    f"{statistics.median(solved_times):.3f}" if solved_times else ""
                ),
                "results": ",".join(row["result"] for row in values),
            }
        )

with (root / "summary.tsv").open("w", newline="") as handle:
    writer = csv.DictWriter(handle, fieldnames=list(rows[0]), delimiter="\t", lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)

for row in rows:
    print(
        f"{row['config']} {row['target']}: "
        f"solved={row['solved']}/{row['repeats']} "
        f"median_solved_s={row['median_solved_s'] or '-'}"
    )
PY

touch "$CAMPAIGN_ROOT/postprocess.complete"
log "POSTPROCESS COMPLETE: $CAMPAIGN_ROOT"
