#!/usr/bin/env bash
# Phase 2: chosen portfolio on the obfuscated corpus (60 s, 17 s), fresh TACAS23 at 60 s,
# then the sealed-map join, TACAS23 audit, three-way tables and thermal annotation. Serial.
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
trap 'rm -f "$ROOT/build_scratch/thermal/current-leg"' EXIT
for cap in 60 17; do
  echo '65530fb4ab03245c2f8d9d63f09b6a1bf22bde1599b118755098ed14495c5e96  '"$BIN" | sha256sum -c -
  echo 'c38d1346f37e5fb93f22210721a54e27c929f0cb93be3b9cf658a7f221ad805e  '"$MANIFEST" | sha256sum -c -
  python3 -s "$CAMPAIGN/verify-obfuscated-corpus.py" --corpus "$OBF_CORPUS" --manifest "$MANIFEST" --list "$OBF_LIST" --tlsf-map "$OBF_MAP"
  out="$CAMPAIGN/full-obfuscated/${cap}s/Acacia"; mkdir -p "$out"
  echo "OBF-$cap start $(date -Is)" >> $P
  printf 'obf-%s\n' "$cap" > "$ROOT/build_scratch/thermal/current-leg"
  python3 -s "$ROOT/benchmarking/run-syntcomp26-coverage.py" --bin "$BIN" --flags "--arms $CHOSEN" --solver-label Acacia \
    --preset otf_sparse_formula --acacia-sha f74ad9d14bb8879550d1af0126d371ebfd4e6253 \
    --list "$OBF_LIST" --tlsf-map "$OBF_MAP" --tlsf-corpus "$OBF_CORPUS" \
    --caps "$cap" --memory-max 8G --memory-swap-max 0 --collect-rusage --status-exceptions "$ROOT/build_scratch/final-runs/no-status-exceptions.tsv" --conflict-policy collect --output "$out/Acacia-cap$cap.tsv" > "$out/runner.log" 2>&1
  rm -f "$ROOT/build_scratch/thermal/current-leg"
  python3 -s "$ROOT/benchmarking/run-syntcomp26-coverage.py" export-cactus --summary "$out/Acacia-cap$cap-summary.tsv" \
    --list "$OBF_LIST" --cap "$cap" --output "$out/Acacia-cap$cap.csv"
  echo "OBF-$cap done $(date -Is)" >> $P
done
V1="$ROOT/_bm-logs.fmcad26-head-6dda2f3b-20260822/build/acacia-v1-best23/src/acacia-bonsai"
PAIRS="$ROOT/_bm-logs.three-way-full-20260905/syfco-adapted"
V1_OUT="$CAMPAIGN/full/60s/TACAS23"; mkdir -p "$V1_OUT"
echo '75fabd3c081030512a0fa5348244aee7382144e0043eb1da2b581651edf84cd8  '"$V1" | sha256sum -c -
echo "TACAS23-60 start $(date -Is)" >> $P
printf 'tacas23-60\n' > "$ROOT/build_scratch/thermal/current-leg"
python3 -s "$ROOT/benchmarking/run-subset.py" --tool acacia1x --bin "$V1" --list "$LIST" --instances-dir "$PAIRS" --timeout 60 \
  --systemd-scope --memory-max 8G --memory-swap-max 0 --csv "$V1_OUT/v1-convertible.csv" > "$V1_OUT/runner.log" 2>&1
rm -f "$ROOT/build_scratch/thermal/current-leg"
python3 -s - "$LIST" "$V1_OUT/v1-convertible.csv" "$V1_OUT/v1-cap60.csv" <<'PY'
import csv, pathlib, sys
ids = [line.strip() for line in pathlib.Path(sys.argv[1]).read_text().splitlines() if line.strip() and not line.startswith('#')]
with open(sys.argv[2], newline='') as stream:
    source_rows = list(csv.DictReader(stream))
rows = {row['instance']: row for row in source_rows}
missing = [name for name in ids if name not in rows]
expected = [f'finding_nemo_pb_{n}_pe_.ltl' for n in range(1, 8)]
if missing != expected or len(source_rows) != 1517 or len(rows) != 1517:
    raise SystemExit(f'unexpected missing/duplicate v1 pairs: {missing}')
with open(sys.argv[3], 'w', newline='') as stream:
    writer = csv.DictWriter(stream, fieldnames=('instance', 'result', 'seconds', 'exit'))
    writer.writeheader()
    for name in ids:
        writer.writerow(rows.get(name, dict(instance=name, result='SYFCO-FAIL', seconds='0', exit='-1')))
PY
echo "TACAS23-60 done $(date -Is)" >> $P
python3 -s "$CAMPAIGN/join-obfuscated-threeway.py" \
  --acacia60 "$CAMPAIGN/full-obfuscated/60s/Acacia/Acacia-cap60.csv" --acacia17 "$CAMPAIGN/full-obfuscated/17s/Acacia/Acacia-cap17.csv" \
  --ltlsynt60 "$CAMPAIGN/derived-60s/ltlsynt-cap60.csv" --ltlsynt17 "$ROOT/benchmarking/witness-lifting-20260918/opening/17s/ltlsynt-cap17.csv" \
  --tacas60 "$CAMPAIGN/full/60s/TACAS23/v1-cap60.csv" --tacas17 "$ROOT/benchmarking/plots/three-way-full-20260905/syntcomp26-full-v1.csv" \
  --sealed-map "$CAMPAIGN/generic-selection/sealed-mapping.jsonl" --obfuscated-list "$OBF_LIST" \
  --original-map "$MAP" --original-corpus "$CORPUS" --status-exceptions "$ROOT/benchmarking/syntcomp26-status-exceptions.tsv" \
  --out "$POST/three-way" > "$ROOT/build_scratch/final-runs/join.log" 2>&1
echo "JOIN done $(date -Is)" >> $P
python3 -s "$CAMPAIGN/thermal-annotate.py" --samples "$ROOT/build_scratch/thermal/samples.tsv" --main-root "$ROOT" --chosen-arms "$CHOSEN" \
  --final60 "$CAMPAIGN/full-obfuscated/60s/Acacia/Acacia-cap60.csv" --final17 "$CAMPAIGN/full-obfuscated/17s/Acacia/Acacia-cap17.csv" \
  --out "$POST/thermal" > "$ROOT/build_scratch/final-runs/thermal.log" 2>&1
echo "PHASE2 DONE $(date -Is)" >> $P
