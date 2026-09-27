#!/usr/bin/env bash
# Phase 1: verify pins, then the stratified plain-corpus smoke run of the chosen portfolio.
set -euo pipefail
ROOT=/home/gperez/GIT-repos/acacia-bonsai; cd "$ROOT"
P=$ROOT/build_scratch/final-runs/progress.txt
TIMING=/home/gperez/GIT-repos/acacia-gr1-par2-timing
CAMPAIGN="$TIMING/benchmarking/gr1-par2-20260923/campaign"
POST="$ROOT/build_scratch/postleg"
BIN="$ROOT/build_final_f74ad9d1/src/acacia-bonsai"
LIST="$ROOT/tests/suites/benchmarks/syntcomp26/all.list"
MAP="$ROOT/tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv"
CORPUS="$ROOT/tlsf-corpus"
OBF_LIST="$CAMPAIGN/generic-selection/all-obfuscated.list"
OBF_MAP="$CAMPAIGN/generic-selection/tlsf-sources-obfuscated.tsv"
OBF_CORPUS="$ROOT/tlsf-corpus-obf"
MANIFEST="$POST/obfuscated-corpus.sha256"
CHOSEN=real:small:backward,real:small:forward,unreal:formula:spot-guarded-sparse,unreal:automaton:forward,real:gr1:oxidd
echo "PHASE1 start $(date -Is) chosen=$CHOSEN" >> $P
echo '65530fb4ab03245c2f8d9d63f09b6a1bf22bde1599b118755098ed14495c5e96  '"$BIN" | sha256sum -c -
echo 'c38d1346f37e5fb93f22210721a54e27c929f0cb93be3b9cf658a7f221ad805e  '"$MANIFEST" | sha256sum -c -
python3 -s "$CAMPAIGN/verify-obfuscated-corpus.py" --corpus "$OBF_CORPUS" --manifest "$MANIFEST" --list "$OBF_LIST" --tlsf-map "$OBF_MAP"
echo "PINS OK $(date -Is)" >> $P
SMOKE="$CAMPAIGN/perarm-m1/smoke/final-portfolio"; mkdir -p "$SMOKE"
python3 -s "$CAMPAIGN/perarm-select.py" --list "$LIST" --smoke-sample 50 --arms "$CHOSEN" --smoke-output "$SMOKE/stratified-50.list"
echo "SAMPLE $(wc -l < "$SMOKE/stratified-50.list") ids $(date -Is)" >> $P
printf 'smoke-60\n' > "$ROOT/build_scratch/thermal/current-leg"
python3 -s "$ROOT/benchmarking/run-syntcomp26-coverage.py" --bin "$BIN" --flags "--arms $CHOSEN" \
  --solver-label Acacia-smoke --preset otf_sparse_formula --acacia-sha f74ad9d14bb8879550d1af0126d371ebfd4e6253 \
  --list "$SMOKE/stratified-50.list" --tlsf-map "$MAP" --tlsf-corpus "$CORPUS" \
  --caps 60 --memory-max 8G --memory-swap-max 0 --collect-rusage --output "$SMOKE/Acacia-smoke.tsv"
rm -f "$ROOT/build_scratch/thermal/current-leg"
python3 -s "$ROOT/benchmarking/run-syntcomp26-coverage.py" export-cactus --summary "$SMOKE/Acacia-smoke-summary.tsv" \
  --list "$SMOKE/stratified-50.list" --cap 60 --output "$SMOKE/Acacia-smoke.csv"
set +e
python3 -s "$CAMPAIGN/smoke-compare.py" --list "$SMOKE/stratified-50.list" --all-list "$LIST" --arms "$CHOSEN" \
  --portfolio "$SMOKE/Acacia-smoke.csv" > "$ROOT/build_scratch/final-runs/smoke-compare.txt" 2>&1
echo "SMOKE-COMPARE rc=$? $(date -Is)" >> $P
