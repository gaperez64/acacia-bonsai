#!/usr/bin/env bash
set -uo pipefail
until grep -q EXEC-DONE /home/gperez/acacia-worktree-retirement-20260923/removal-log.txt 2>/dev/null; do sleep 30; done
sleep 60
cd /home/gperez/GIT-repos/acacia-gr1-par2-timing
ROOT=/home/gperez/GIT-repos/acacia-gr1-par2-timing
CAMPAIGN="$ROOT/benchmarking/gr1-par2-20260923/campaign"
LIST="$CAMPAIGN/eligible.list"
MAP="$ROOT/tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv"
CORPUS="/home/gperez/GIT-repos/acacia-bonsai/tlsf-corpus"
TOOLS=/home/gperez/GIT-repos/tlsf-tools/build-P5-9212e2b
B="/home/gperez/GIT-repos/acacia-bonsai/build_w1_B/src/acacia-bonsai"
ADAPTER=/home/gperez/GIT-repos/acacia-gr1-par2-timing/build_gr1par2-adapter-ee37c727/libbuddy_veccompose_adapter.so
echo '398a420bfa939a7c80c67a0357022ed6f09279a1c2878116c0602abd14392e4a  '"$B" | sha256sum -c - || exit 3
echo "START $(date -Is) head=$(git rev-parse HEAD)" > /home/gperez/GIT-repos/acacia-bonsai/build_scratch/seq8/progress.txt
for cap in 60 17; do
  for variant in N-a N-b; do
    if [ "$variant" = N-a ]; then fraction=0.3333333333333333; else fraction=0.6666666666666666; fi
    out="$CAMPAIGN/selection/${cap}s/$variant"
    mkdir -p "$out"
    flags="--lift-budget-fraction $fraction --cap $cap --tlsf-tools-build $TOOLS --bindings-python /usr/bin/python3.13 --bindings-site /usr/local/lib64/python3.13/site-packages --buddy-adapter $ADAPTER -- $B"
    python3 benchmarking/run-syntcomp26-coverage.py \
      --bin "$ROOT/scripts/acacia-lift-portfolio.py" --flags "$flags" \
      --solver-label "$variant" --preset otf_sparse_formula \
      --acacia-sha ee37c727 --list "$LIST" \
      --tlsf-map "$MAP" --tlsf-corpus "$CORPUS" \
      --caps "$cap" --memory-max 8G --memory-swap-max 0 \
      --collect-rusage --route-records "$out/route-records" \
      --output "$out/$variant-cap$cap.tsv" > "$out/runner.log" 2>&1
    echo "LEG $variant cap$cap rc=$? $(date -Is)" >> /home/gperez/GIT-repos/acacia-bonsai/build_scratch/seq8/progress.txt
    python3 "$CAMPAIGN/compare-selection.py" --cap "$cap" --variant "$variant" > "$out/compare.log" 2>&1
    sleep 20
  done
done
echo "DONE $(date -Is)" >> /home/gperez/GIT-repos/acacia-bonsai/build_scratch/seq8/progress.txt
