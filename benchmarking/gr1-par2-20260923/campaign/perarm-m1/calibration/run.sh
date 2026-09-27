#!/usr/bin/env bash
# Re-run sampled solves of flagged legs (arm-1, arm-6, arm-5) and a clean control (arm-2)
# on the quiet machine with the same frozen M1 binary and flags, to measure thermal slowdown.
set -uo pipefail
M=/home/gperez/GIT-repos/acacia-bonsai; W=/home/gperez/GIT-repos/acacia-gr1-par2-timing; C=$M/build_scratch/calibration
BIN=$M/build_perarm_m1/src/acacia-bonsai
echo '22455c945576d28d86ac98a3b512327740b12f0cc3a3a6006ad1c59dfe9c9584  '"$BIN" | sha256sum -c - || exit 3
cd $W
declare -A ARM=([arm-1]=real:small:backward [arm-6]=unreal:gr1:oxidd [arm-5]=real:gr1:oxidd [arm-2]=real:small:forward)
for label in arm-2 arm-1 arm-6 arm-5; do
  python3 benchmarking/run-syntcomp26-coverage.py --bin "$BIN" --flags "--arms ${ARM[$label]}" \
    --solver-label calib-$label --preset otf_sparse_formula --acacia-sha 3dc9fdf38b30dbc8fc85609a32ca7280ae206529 \
    --list "$C/$label.list" --tlsf-map "$W/tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv" --tlsf-corpus "$M/tlsf-corpus" \
    --caps 60 --memory-max 8G --memory-swap-max 0 --collect-rusage --output "$C/$label-calib.tsv" > "$C/$label.log" 2>&1
  echo "CALIB $label rc=$? $(date -Is)" >> $C/progress.txt
done
echo "DONE $(date -Is)" >> $C/progress.txt
