#!/usr/bin/env bash
# Re-run, on the quiet machine, the instances that decide the top two 5-arm portfolios:
# arm-6 timeouts outside the shared base {1,2,3,5}, and arm-5 instances where arm-4 and arm-6 differ.
set -uo pipefail
M=/home/gperez/GIT-repos/acacia-bonsai; W=/home/gperez/GIT-repos/acacia-gr1-par2-timing; C=$M/build_scratch/calibration
BIN=$M/build_perarm_m1/src/acacia-bonsai
echo '22455c945576d28d86ac98a3b512327740b12f0cc3a3a6006ad1c59dfe9c9584  '"$BIN" | sha256sum -c - || exit 3
cd $W
for spec in "arm-6 unreal:gr1:oxidd" "arm-5 real:gr1:oxidd"; do
  set -- $spec; label=$1; arm=$2
  python3 benchmarking/run-syntcomp26-coverage.py --bin "$BIN" --flags "--arms $arm" \
    --solver-label decide-$label --preset otf_sparse_formula --acacia-sha 3dc9fdf38b30dbc8fc85609a32ca7280ae206529 \
    --list "$C/decide-$label.list" --tlsf-map "$W/tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv" --tlsf-corpus "$M/tlsf-corpus" \
    --caps 60 --memory-max 8G --memory-swap-max 0 --collect-rusage --output "$C/decide-$label.tsv" > "$C/decide-$label.log" 2>&1
  echo "DECIDE $label rc=$? $(date -Is)" >> $C/progress.txt
done
echo "DECIDE-DONE $(date -Is)" >> $C/progress.txt
