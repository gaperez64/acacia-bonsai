#!/bin/bash
# W1 opening campaign: full SYNTCOMP26 corpus (1,524 instances), B and S,
# at both the 120s and 17s caps, two balanced epochs each, plus the pinned
# ltlsynt external leg at each cap. Strictly sequential (one solver
# invocation at a time). See ../../CLAUDE.md and manifest.json for the exact
# identities. Long-budget campaign first per plan section 4.1.
set -euo pipefail

cd "$(dirname "$0")/../.."   # repo root
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:${PKG_CONFIG_PATH:-}

ROOT="$(pwd)"
EVIDENCE="benchmarking/witness-lifting-20260918"
SHARDS="$EVIDENCE/opening/shards"
TLSF_MAP="tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv"
TLSF_CORPUS="$ROOT/tlsf-corpus"
SYFCO_CACHE="$ROOT/$EVIDENCE/opening/syfco-cache"
ACACIA_SHA="50384cf69a733199fa17c9c571da1761d8451991"
PRESET="otf_sparse_formula"
B_BIN="$ROOT/build_w1_B/src/acacia-bonsai"
S_BIN="$ROOT/build_w1_S/src/acacia-bonsai"
LTLSYNT_BIN="/usr/local/sbin/ltlsynt"

log() { echo "[$(date '+%Y-%m-%dT%H:%M:%S')] $*"; }

run_acacia_leg() {
  local cap="$1" epoch="$2"
  local outdir="$EVIDENCE/opening/${cap}s/epoch-${epoch}"
  mkdir -p "$outdir"
  log "=== Acacia leg: cap=${cap}s epoch=${epoch} ==="
  python3 benchmarking/run-witness-sprint.py campaign \
    --cap "$cap" --epoch "$epoch" \
    --shards-dir "$SHARDS" \
    --candidates "B=$B_BIN,S=$S_BIN" \
    --tlsf-map "$TLSF_MAP" --tlsf-corpus "$TLSF_CORPUS" \
    --preset "$PRESET" --acacia-sha "$ACACIA_SHA" \
    --out-dir "$outdir"
  log "=== Acacia leg done: cap=${cap}s epoch=${epoch} ==="
}

run_ltlsynt_leg() {
  local cap="$1"
  local outdir="$EVIDENCE/opening/${cap}s"
  mkdir -p "$outdir/ltlsynt-shards"
  log "=== ltlsynt leg: cap=${cap}s ==="
  for shard in "$SHARDS"/shard_*.list; do
    name=$(basename "$shard" .list)
    csv="$outdir/ltlsynt-shards/${name}.csv"
    if [[ -s "$csv" ]]; then
      log "  $name already done, skipping"
      continue
    fi
    log "  $name -> $csv"
    python3 benchmarking/run-subset.py \
      --tool ltlsynt --bin "$LTLSYNT_BIN" \
      --list "$shard" \
      --tlsf-map "$TLSF_MAP" --tlsf-corpus "$TLSF_CORPUS" \
      --syfco-cache "$SYFCO_CACHE" \
      --systemd-scope --memory-max 8G --memory-swap-max 0 \
      --timeout "$cap" --csv "$csv"
  done
  # Concatenate shard CSVs into one series CSV (header from the first, then
  # every data row; run-subset.py's own header is repeated per shard).
  out="$outdir/ltlsynt-cap${cap}.csv"
  first=1
  : > "$out"
  for shard_csv in "$outdir"/ltlsynt-shards/shard_*.csv; do
    if [[ "$first" -eq 1 ]]; then
      cat "$shard_csv" >> "$out"
      first=0
    else
      tail -n +2 "$shard_csv" >> "$out"
    fi
  done
  log "=== ltlsynt leg done: cap=${cap}s -> $out ==="
}

log "W1 OPENING CAMPAIGN START"

# Long-budget campaign first (plan section 4.1: "Begin with the long-budget
# campaign").
run_acacia_leg 120 1
run_acacia_leg 120 2
run_ltlsynt_leg 120

# Historical-budget campaign.
run_acacia_leg 17 1
run_acacia_leg 17 2
run_ltlsynt_leg 17

log "W1 OPENING CAMPAIGN COMPLETE"
