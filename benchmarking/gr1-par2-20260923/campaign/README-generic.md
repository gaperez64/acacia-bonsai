# Archived generic N campaign: preparation and execution

These commands document the withdrawn Python route. Current solver runs use
native arms in `acacia-bonsai`.

Run commands from the repository root, serially on the quiet timing host. Stage C
is the frozen generic route. This preparation generated the corpus and ran the
admission census; it did not build executables or run a timed solver leg. Never combine
N-a and N-b rows into a synthetic series. The old `README.md` records the
withdrawn family route and archival baseline provenance.

## Corpus and admission evidence

`generic-selection/master-seed.txt` records the single master seed. The
`prepare-obfuscated-corpus.py` command generated 1,524 TLSF files with random
basenames and alpha-renamed signals in ignored `tlsf-corpus-obf/`. Each file was
checked against the lowered LTL of its original with `verify-obfuscation.py`'s
`check_pair`; `generic-selection/obfuscation-verification.tsv` records every
equal pair. The corpus has 14,378 renamed signal declarations across 1,524
distinct original and obfuscated file hashes. `generic-selection/all-obfuscated.list` and
`tlsf-sources-obfuscated.tsv` have the runner's usual list and map formats.
`generic-selection/sealed-mapping.jsonl` contains original and obfuscated IDs,
signal maps, per-file seeds and file hashes. It is outside the corpus, and its
SHA-256 is in `sealed-mapping.sha256`. Keep the mapping away from the solver
until **after** the final N runs. To reproduce from an empty corpus directory:

Mapping SHA-256: `70f9339504d3fe6ed5deef47268914c08cb74c79c60059e254599e95597b91b9`.

```bash
python3 benchmarking/gr1-par2-20260923/campaign/prepare-obfuscated-corpus.py \
  --seed 20260924 \
  --build /home/gperez/GIT-repos/tlsf-tools/build-SB-8b158d7
```

`generic-route-admission.py` ran exact target reduction and, if needed, strict
target reduction under the same `min(1 s, 5% of cap)` eligibility gate as the
wrapper. It used original TLSF inputs and the pinned Stage B tools. The two
`generic-selection/admitted-{17,60}.list` files are the exact development
selection sets; `admission-census.tsv` records both results and reasons.
Admission means the generic route will attempt direct solving and, when the
input has frontend parameters, possibly lifting. It does not claim a verdict.
The census yielded 495 admitted inputs at 17 s (0.85 s gate) and 504 at 60 s
(1.00 s gate). The 17 s set is contained in the 60 s set; nine inputs enter
only at 60 s. All admitted reductions were exact. The two serial census
passes consumed 536.2 s and 597.5 s of measured reduction time, respectively.

```bash
python3 benchmarking/gr1-par2-20260923/campaign/generic-route-admission.py
```

## Common setup and frozen baselines

```bash
set -euo pipefail
ROOT=$(pwd)
CAMPAIGN="$ROOT/benchmarking/gr1-par2-20260923/campaign"
GENERIC="$CAMPAIGN/generic-selection"
ORIG_LIST="$ROOT/tests/suites/benchmarks/syntcomp26/all.list"
ORIG_MAP="$ROOT/tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv"
OBF_LIST="$GENERIC/all-obfuscated.list"
OBF_MAP="$GENERIC/tlsf-sources-obfuscated.tsv"
OBF_CORPUS="$ROOT/tlsf-corpus-obf"
TOOLS=/home/gperez/GIT-repos/tlsf-tools/build-SB-8b158d7
B="$ROOT/build_w1_B/src/acacia-bonsai"
ADAPTER="$ROOT/build_scratch/buddy-adapter/libbuddy_veccompose_adapter.so"
test -f "$ADAPTER"
sha256sum -c <<EOF
398a420bfa939a7c80c67a0357022ed6f09279a1c2878116c0602abd14392e4a  $B
EOF
(cd "$GENERIC" && sha256sum -c sealed-mapping.sha256)
```

The B 60 s observations are **derived by censoring** uniform 120 s epochs;
the derived CSVs already exist in `derived-60s/epoch-{1,2}`. The 60 s ltlsynt
CSV is likewise derived from its archived 120 s leg. The 17 s B and ltlsynt
legs are archived. Their source paths and censoring evidence are documented in
`README.md`.

## Development selection: four serial N legs

N-a assigns cap/3 to the generic route; N-b assigns 2·cap/3. Each cap uses
its own admission list. The whole scoped tree has one outer deadline, 8 GiB,
zero swap, and one route record per input. `compare-selection.py` compares
against both frozen B epochs and labels certified direct, certified lifted,
and attempted but declined routes. Investigate verdict conflicts and missing
route records before choosing one variant.

```bash
for cap in 60 17; do
  for variant in N-a N-b; do
    if [ "$variant" = N-a ]; then fraction=0.3333333333333333; else fraction=0.6666666666666666; fi
    out="$GENERIC/selection/${cap}s/$variant"
    mkdir -p "$out"
    flags="--lift-budget-fraction $fraction --cap $cap --tlsf-tools-build $TOOLS --bindings-python /usr/bin/python3.13 --bindings-site /usr/local/lib64/python3.13/site-packages --buddy-adapter $ADAPTER -- $B"
    python3 benchmarking/run-syntcomp26-coverage.py \
      --bin "$ROOT/benchmarking/gr1-par2-20260923/oracle/acacia-lift-portfolio.py" --flags "$flags" \
      --solver-label "$variant" --preset otf_sparse_formula \
      --list "$GENERIC/admitted-$cap.list" \
      --tlsf-map "$ORIG_MAP" --tlsf-corpus "$ROOT/tlsf-corpus" \
      --caps "$cap" --memory-max 8G --memory-swap-max 0 \
      --collect-rusage --route-records "$out/route-records" \
      --output "$out/$variant-cap$cap.tsv"
    python3 "$CAMPAIGN/compare-selection.py" --cap "$cap" --variant "$variant"
  done
done
```

## Final N evaluation on renamed inputs

Set `CHOSEN` to one variant after comparing both caps. This is a fresh run over
all 1,524 obfuscated inputs. The runner receives only the obfuscated list,
source map, and corpus. The sealed mapping is opened by the post hoc script
after both runs finish.

```bash
CHOSEN=${CHOSEN:?set to N-a or N-b after selection}
case "$CHOSEN" in N-a) fraction=0.3333333333333333;; N-b) fraction=0.6666666666666666;; *) exit 2;; esac
for cap in 60 17; do
  out="$GENERIC/final/${cap}s/$CHOSEN"
  mkdir -p "$out"
  flags="--lift-budget-fraction $fraction --cap $cap --tlsf-tools-build $TOOLS --bindings-python /usr/bin/python3.13 --bindings-site /usr/local/lib64/python3.13/site-packages --buddy-adapter $ADAPTER -- $B"
  python3 benchmarking/run-syntcomp26-coverage.py \
    --bin "$ROOT/benchmarking/gr1-par2-20260923/oracle/acacia-lift-portfolio.py" --flags "$flags" \
    --solver-label "$CHOSEN" --preset otf_sparse_formula \
    --list "$OBF_LIST" --tlsf-map "$OBF_MAP" --tlsf-corpus "$OBF_CORPUS" \
    --caps "$cap" --memory-max 8G --memory-swap-max 0 --collect-rusage \
    --route-records "$out/route-records" --output "$out/N-cap$cap.tsv"
  python3 benchmarking/run-syntcomp26-coverage.py export-cactus \
    --summary "$out/N-cap$cap-summary.tsv" --list "$OBF_LIST" --cap "$cap" \
    --output "$out/N-cap$cap.csv"
done
```

The following is **post hoc**. It verifies the sealed map hash, exact 1,524-ID
coverage, each route record's input hash, and decisive-verdict invariance for
every development ID at the same cap and chosen variant. It writes N CSVs
keyed by original ID, per-ID B versus N results, and route attribution tables.
The 60 s B comparison uses the derived epoch-1 series.

```bash
for cap in 60 17; do
  out="$GENERIC/final/${cap}s/$CHOSEN"
  if [ "$cap" = 60 ]; then
    B_CSV="$CAMPAIGN/derived-60s/epoch-1/B-cap60-epoch1.csv"
  else
    B_CSV="$ROOT/benchmarking/witness-lifting-20260918/opening/17s/epoch-1/B-cap17-epoch1.csv"
  fi
  python3 "$CAMPAIGN/posthoc-generic.py" \
    --cap "$cap" --variant "$CHOSEN" \
    --obfuscated-csv "$out/N-cap$cap.csv" \
    --final-tsv "$out/N-cap$cap.tsv" \
    --route-records "$out/route-records/$CHOSEN/$cap" \
    --selection-tsv "$GENERIC/selection/${cap}s/$CHOSEN/$CHOSEN-cap$cap.tsv" \
    --selection-list "$GENERIC/admitted-$cap.list" \
    --b-csv "$B_CSV" --out-prefix "$out/posthoc"
done
```

## Fresh TACAS23 60 s leg on original pairs

TACAS23 uses the existing 1,517 SyFCo-adapted pairs and the frozen v1 binary,
as documented in `README.md`. Seven `finding_nemo` inputs have no pair; append
their `SYFCO-FAIL` rows. Pair generation is outside the solver timing.

```bash
V1="$ROOT/_bm-logs.fmcad26-head-6dda2f3b-20260822/build/acacia-v1-best23/src/acacia-bonsai"
PAIRS="$ROOT/_bm-logs.three-way-full-20260905/syfco-adapted"
V1_OUT="$GENERIC/final/60s/TACAS23"
mkdir -p "$V1_OUT"
sha256sum -c <<EOF
75fabd3c081030512a0fa5348244aee7382144e0043eb1da2b581651edf84cd8  $V1
EOF
python3 benchmarking/run-subset.py --tool acacia1x --bin "$V1" \
  --list "$ORIG_LIST" --instances-dir "$PAIRS" --timeout 60 \
  --systemd-scope --memory-max 8G --memory-swap-max 0 \
  --csv "$V1_OUT/v1-convertible.csv"
python3 - "$ORIG_LIST" "$V1_OUT/v1-convertible.csv" "$V1_OUT/v1-cap60.csv" <<'PY'
import csv, pathlib, sys
ids = [s.strip() for s in pathlib.Path(sys.argv[1]).read_text().splitlines()
       if s.strip() and not s.startswith('#')]
with open(sys.argv[2], newline='') as stream:
    rows = {row['instance']: row for row in csv.DictReader(stream)}
missing = [name for name in ids if name not in rows]
expected = [f'finding_nemo_pb_{n}_pe_.ltl' for n in range(1, 8)]
if missing != expected or len(rows) != 1517:
    raise SystemExit(f'unexpected v1 pair coverage: {missing}')
with open(sys.argv[3], 'w', newline='') as stream:
    writer = csv.DictWriter(stream, fieldnames=('instance', 'result', 'seconds', 'exit'))
    writer.writeheader()
    for name in ids:
        writer.writerow(rows.get(name, dict(instance=name, result='SYFCO-FAIL', seconds='0', exit='-1')))
PY
```

## Three-way report at both caps

All three series now use original IDs. The 17 s TACAS23 CSV is archived, the
60 s one above is fresh, and 60 s ltlsynt is labelled as derived.

```bash
for CAP in 60 17; do
  REPORT="$GENERIC/final/${CAP}s/report-$CHOSEN"
  mkdir -p "$REPORT"
  NEW="$GENERIC/final/${CAP}s/$CHOSEN/posthoc-N-original.csv"
  if [ "$CAP" = 60 ]; then
    LTLSYNT="$CAMPAIGN/derived-60s/ltlsynt-cap60.csv"
    LTLSYNT_LABEL="ltlsynt (derived from 120 s)"
    TACAS23="$GENERIC/final/60s/TACAS23/v1-cap60.csv"
    TITLE_NOTE="; ltlsynt derived from 120 s"
  else
    LTLSYNT="$ROOT/benchmarking/witness-lifting-20260918/opening/17s/ltlsynt-cap17.csv"
    LTLSYNT_LABEL="ltlsynt"
    TACAS23="$ROOT/benchmarking/plots/three-way-full-20260905/syntcomp26-full-v1.csv"
    TITLE_NOTE=""
  fi
  python3 benchmarking/cactus-report.py \
    --csv "$LTLSYNT_LABEL=$LTLSYNT" --csv "Acacia TACAS23=$TACAS23" \
    --csv "New Acacia=$NEW" \
    --title "SYNTCOMP26 — ltlsynt / TACAS23 / generic N ($CAP s, 8 GiB$TITLE_NOTE)" \
    --timeout "$CAP" --out-prefix "$REPORT/three-way" \
    --markdown "$REPORT/three-way-par2.md"
  if [ "$CAP" = 60 ]; then
    printf '\nltlsynt: derived by censoring archived uniform 120 s observations; not a 60 s run.\n' >> "$REPORT/three-way-par2.md"
  fi
done
```

## B on 30 renamed inputs

This extra 17 s B leg tests the name-independence of B's verdicts. Its seeded
sample is selected from obfuscated IDs, and original IDs are consulted only
after the run. The archived original B epoch-1 rows are the comparator.

```bash
python3 "$CAMPAIGN/check-obfuscated-b.py" choose --seed 20260924
B_OBF="$GENERIC/B-obfuscated-30"
mkdir -p "$B_OBF"
python3 benchmarking/run-syntcomp26-coverage.py \
  --bin "$B" --solver-label B-obf --preset otf_sparse_formula \
  --list "$GENERIC/B-obfuscated-30.list" --tlsf-map "$OBF_MAP" \
  --tlsf-corpus "$OBF_CORPUS" --caps 17 --memory-max 8G \
  --memory-swap-max 0 --collect-rusage --output "$B_OBF/B-obf-cap17.tsv"
python3 benchmarking/run-syntcomp26-coverage.py export-cactus \
  --summary "$B_OBF/B-obf-cap17-summary.tsv" \
  --list "$GENERIC/B-obfuscated-30.list" --cap 17 \
  --output "$B_OBF/B-obf-cap17.csv"
python3 "$CAMPAIGN/check-obfuscated-b.py" compare \
  --renamed-csv "$B_OBF/B-obf-cap17.csv" \
  --original-csv "$ROOT/benchmarking/witness-lifting-20260918/opening/17s/epoch-1/B-cap17-epoch1.csv"
```

## Wall-time planning

Use the observed admission counts above and the frozen B epoch-1 rows. A
planning proxy for N charges each admitted row its whole lift slice plus its B
time, capped at the outer deadline; a declined row is charged two eligibility
gates plus B time because the direct route can repeat the reduction attempt.
This is solver-row time, so systemd startup and cooldown
add to wall time. B's **charged** times below use the full cap for unsolved
rows. The old campaign's observed B full-corpus elapsed sums were 20,754 s at
derived 60 s and 6,691 s at 17 s. This planning proxy assumes no generic
certificate saves time; it is not a predicted score.

| Serial leg | 17 s cap | 60 s cap |
|---|---:|---:|
| B epoch 1, full corpus charged time | 6,718 s (1.87 h) | 20,912 s (5.81 h; derived) |
| N-a selection (495 / 504 IDs) | 3,933 s (1.09 h) | 13,794 s (3.83 h) |
| N-b selection (495 / 504 IDs) | 6,201 s (1.72 h) | 22,095 s (6.14 h) |
| N-a final (1,524 IDs) | 10,310 s (2.86 h) | 30,824 s (8.56 h) |
| N-b final (1,524 IDs) | 12,579 s (3.49 h) | 39,125 s (10.87 h) |

All four selection legs total about 12.8 h of proxy solver-row time. After
selection, both final N-a caps total about 11.4 h, or both N-b caps about
14.4 h. TACAS23 at 60 s adds its archived-outcome proxy of 38,104 s
(10.58 h). The extra B sample is bounded by 510 s (8.5 min). Systemd startup,
cooldown, exports, and report generation add wall time to these figures.

## VERDICT

The obfuscated corpus is complete and all 1,524 lowered LTL comparisons are
equal. The generic admission census produced exact 495-ID and 504-ID
development lists at 17 s and 60 s. Selection and final solver results remain
unmeasured in this preparation.
Run the four selection legs, choose a single variant, and then run the fresh
final and TACAS23 legs before making a performance claim.
