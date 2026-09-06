#!/usr/bin/env bash
# Generate SyFCo pairs for the FULL syntcomp26 selection, both routes:
#   adapted/ : semantics overwritten to the declared target, for Acacia v1,
#              which predates the TLSF frontend and has no --semantics flag.
#              Verified to reproduce _bm-logs.three-way-20260828/syfco-adapted
#              byte-for-byte on both a differing and an identical instance.
#   plain/   : unadapted, for ltlsynt, which takes --semantics itself.
cd "$(dirname "$0")/.." || exit 1
OUT=${1:?usage: make-syfco-pairs.sh OUTPUT_DIR [TLSF_CORPUS]}
C=${2:-/tmp/acacia-syntcomp26-tlsf}
A=$OUT/syfco-adapted; P=$OUT/syfco-plain
mkdir -p "$A" "$P"
n=0; skipped=0
while IFS=$'\t' read -r inst tlsf; do
  [ "$inst" = "instance" ] && continue
  case "$inst" in \#*) continue;; esac
  b=${inst%.ltl}
  T=$C/$tlsf
  [ -f "$T" ] || { skipped=$((skipped+1)); continue; }
  if [ ! -s "$A/$b.ltl" ]; then
    tgt=$(syfco --print-target "$T" 2>/dev/null)
    [ -n "$tgt" ] && syfco -os "$tgt" --format ltlxba --mode fully \
        --part-file "$A/$b.part" "$T" > "$A/$b.ltl" 2>/dev/null || rm -f "$A/$b.ltl" "$A/$b.part"
  fi
  if [ ! -s "$P/$b.ltl" ]; then
    syfco --format ltlxba --mode fully --part-file "$P/$b.part" "$T" > "$P/$b.ltl" 2>/dev/null \
      || rm -f "$P/$b.ltl" "$P/$b.part"
  fi
  n=$((n+1))
done < tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv
echo "processed=$n missing_tlsf=$skipped"
echo "adapted pairs: $(ls $A/*.ltl 2>/dev/null | wc -l)   plain pairs: $(ls $P/*.ltl 2>/dev/null | wc -l)"
echo "SYFCO-FULL-DONE"
