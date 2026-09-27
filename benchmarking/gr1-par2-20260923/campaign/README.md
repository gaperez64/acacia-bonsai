# Archived GR(1) lifting campaign commands

These commands document the withdrawn Python route and are retained for
reproduction. Current solver runs use native arms in `acacia-bonsai`.

Run these commands from the repository root, **serially** on the quiet timing
host. The commands below prepare observations; this prep did not run a timed
solver leg. `eligibility.tsv` gives 91 eligible IDs, and `eligible.list` is the
exact subset used for selection. The source matcher reads no historical answer.

Verified executable SHA-256 values:

| Executable | SHA-256 |
|---|---|
| `build_w1_B/src/acacia-bonsai` | `398a420bfa939a7c80c67a0357022ed6f09279a1c2878116c0602abd14392e4a` |
| `_bm-logs.fmcad26-head-6dda2f3b-20260822/build/acacia-v1-best23/src/acacia-bonsai` | `75fabd3c081030512a0fa5348244aee7382144e0043eb1da2b581651edf84cd8` |
| `/usr/local/sbin/ltlsynt` | `ea761a1c0594278bd4b525369977677520c8b9d3dcc2f5a45dab91d961663900` |

The tlsf-tools submodule pins c956847, the merged commit with the same tree as
9212e2b. Use its frozen `build-P5-9212e2b` outside this checkout. Supply the
native adapter built for the committed package as an **absolute** path in
`ADAPTER`. The wrapper uses the capability's frozen `real_check` attribute;
passing a global `--real-check` would override it.

## Selection: N-a and N-b, 60 s and 17 s

The coverage runner adds `-T` to the flag string, passes one outer monotonic
deadline and one route-record path, and scopes the whole tree at 8 GiB with no
swap. Each call below uses one uniform cap and exactly one row per eligible ID.
The production default-seed guard declines 33 additional source-verified
capability members; these are outside selection but remain in the full corpus.
`--collect-rusage` matches B's recorded setting. Run one command at a time;
the loop itself is sequential. N-a gets one third of the outer deadline for
lifting (20 s at 60 s; 5.67 s at 17 s). N-b gets two thirds (40 s at 60 s;
11.33 s at 17 s).

```bash
set -euo pipefail
ROOT=$(pwd)
CAMPAIGN="$ROOT/benchmarking/gr1-par2-20260923/campaign"
LIST="$ROOT/tests/suites/benchmarks/syntcomp26/all.list"
MAP="$ROOT/tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv"
CORPUS="$ROOT/tlsf-corpus"
TOOLS=/home/gperez/GIT-repos/tlsf-tools/build-P5-9212e2b
B="$ROOT/build_w1_B/src/acacia-bonsai"
ADAPTER=${ADAPTER:?set ADAPTER to the absolute buddy_veccompose.so path}
case "$ADAPTER" in /*) test -f "$ADAPTER";; *) echo 'ADAPTER must be absolute' >&2; exit 2;; esac
echo '398a420bfa939a7c80c67a0357022ed6f09279a1c2878116c0602abd14392e4a  '"$B" | sha256sum -c -
for cap in 60 17; do
  for variant in N-a N-b; do
    if [ "$variant" = N-a ]; then fraction=0.3333333333333333; else fraction=0.6666666666666666; fi
    out="$CAMPAIGN/selection/${cap}s/$variant"
    mkdir -p "$out"
    flags="--lift-budget-fraction $fraction --cap $cap --tlsf-tools-build $TOOLS --bindings-python /usr/bin/python3.13 --bindings-site /usr/local/lib64/python3.13/site-packages --buddy-adapter $ADAPTER -- $B"
    python3 benchmarking/run-syntcomp26-coverage.py \
      --bin "$ROOT/benchmarking/gr1-par2-20260923/oracle/acacia-lift-portfolio.py" --flags "$flags" \
      --solver-label "$variant" --preset otf_sparse_formula \
      --acacia-sha 3ee43ec8 --list "$CAMPAIGN/eligible.list" \
      --tlsf-map "$MAP" --tlsf-corpus "$CORPUS" \
      --caps "$cap" --memory-max 8G --memory-swap-max 0 \
      --collect-rusage --route-records "$out/route-records" \
      --output "$out/$variant-cap$cap.tsv"
    python3 "$CAMPAIGN/compare-selection.py" --cap "$cap" --variant "$variant"
  done
done
```

`compare-selection.py` validates each uniform-cap N row through the existing
coverage export normalisation, joins the same 91 IDs to B epoch 1 and epoch 2
from `derived-60s/epoch-{1,2}/` at 60 s and
`benchmarking/witness-lifting-20260918/opening/17s/` at 17 s, uses
`benchlib.par2`, and writes `comparison.tsv` and `comparison.md` next to each
N run. The table records every gain/loss or verdict conflict against B epoch 1;
the summary includes solved and PAR-2 on the eligible subset, B's own epoch
repeatability, and route winners read from the JSON route records. Investigate
every verdict conflict and missing record before choosing one variant. Do not
combine the best rows of N-a and N-b into a synthetic series. The 60 s B rows
are derived by censoring uniform 120 s observations; they are not 60 s runs.

## Derive archived 60 s B and ltlsynt legs

Run this once before the 60 s selection comparison or full report. The tool
requires each B source's matching summary and validates every raw cap against
120 s. The four-column ltlsynt CSV has no cap column; its archived `120s` path
and TIMEOUT durations supply cap evidence. Each output has a provenance JSON
file with the source SHA-256 and a TSV with every original result, time, and
exit. All rows past 60 s or without a decisive verdict become TIMEOUT at 60 s.
These commands only transform files; they do not run solvers.

```bash
ROOT=$(pwd)
CAMPAIGN="$ROOT/benchmarking/gr1-par2-20260923/campaign"
LIST="$ROOT/tests/suites/benchmarks/syntcomp26/all.list"
OPENING="$ROOT/benchmarking/witness-lifting-20260918/opening/120s"
for epoch in 1 2; do
  out="$CAMPAIGN/derived-60s/epoch-$epoch"
  python3 "$CAMPAIGN/censor-to-cap.py" \
    --input "$OPENING/epoch-$epoch/B-cap120-epoch$epoch.tsv" \
    --output "$out/B-cap60-epoch$epoch.tsv" \
    --high-cap 120 --low-cap 60 --list "$LIST"
  python3 benchmarking/run-syntcomp26-coverage.py export-cactus \
    --summary "$out/B-cap60-epoch$epoch-summary.tsv" --list "$LIST" --cap 60 \
    --output "$out/B-cap60-epoch$epoch.csv"
done
python3 "$CAMPAIGN/censor-to-cap.py" \
  --input "$OPENING/ltlsynt-cap120.csv" \
  --output "$CAMPAIGN/derived-60s/ltlsynt-cap60.csv" \
  --high-cap 120 --low-cap 60
```

## Full corpus: chosen N and TACAS23

Set `CHOSEN` to the selected single variant after reviewing **both** caps and
the per-ID regressions. This runs N once per cap over all 1,524 IDs and keeps
route records for attribution. It uses the variables from the setup block above.

```bash
CHOSEN=${CHOSEN:?set CHOSEN to N-a or N-b after selection}
case "$CHOSEN" in N-a) fraction=0.3333333333333333;; N-b) fraction=0.6666666666666666;; *) exit 2;; esac
for cap in 60 17; do
  out="$CAMPAIGN/full/${cap}s/$CHOSEN"
  mkdir -p "$out"
  flags="--lift-budget-fraction $fraction --cap $cap --tlsf-tools-build $TOOLS --bindings-python /usr/bin/python3.13 --bindings-site /usr/local/lib64/python3.13/site-packages --buddy-adapter $ADAPTER -- $B"
  python3 benchmarking/run-syntcomp26-coverage.py \
    --bin "$ROOT/benchmarking/gr1-par2-20260923/oracle/acacia-lift-portfolio.py" --flags "$flags" \
    --solver-label "$CHOSEN" --preset otf_sparse_formula \
    --acacia-sha 3ee43ec8 --list "$LIST" --tlsf-map "$MAP" --tlsf-corpus "$CORPUS" \
    --caps "$cap" --memory-max 8G --memory-swap-max 0 --collect-rusage \
    --route-records "$out/route-records" --output "$out/N-cap$cap.tsv"
  python3 benchmarking/run-syntcomp26-coverage.py export-cactus \
    --summary "$out/N-cap$cap-summary.tsv" --list "$LIST" --cap "$cap" \
    --output "$out/N-cap$cap.csv"
done
```

TACAS23 at 60 s uses the existing `run-subset.py` `acacia1x` adapter and
the 1,517 SyFCo pairs in `_bm-logs.three-way-full-20260905/syfco-adapted`.
Those pairs overwrite semantics to the declared TLSF target, matching the
historical v1 route. The seven `finding_nemo` conversion failures have no
pair; append their `SYFCO-FAIL` rows after the solver run so the CSV retains all
1,524 IDs. Pair generation is outside each solver timing, as for the archived
external legs.

```bash
V1="$ROOT/_bm-logs.fmcad26-head-6dda2f3b-20260822/build/acacia-v1-best23/src/acacia-bonsai"
PAIRS="$ROOT/_bm-logs.three-way-full-20260905/syfco-adapted"
V1_OUT="$CAMPAIGN/full/60s/TACAS23"
mkdir -p "$V1_OUT"
echo '75fabd3c081030512a0fa5348244aee7382144e0043eb1da2b581651edf84cd8  '"$V1" | sha256sum -c -
python3 benchmarking/run-subset.py --tool acacia1x --bin "$V1" \
  --list "$LIST" --instances-dir "$PAIRS" --timeout 60 \
  --systemd-scope --memory-max 8G --memory-swap-max 0 \
  --csv "$V1_OUT/v1-convertible.csv"
python3 - "$LIST" "$V1_OUT/v1-convertible.csv" "$V1_OUT/v1-cap60.csv" <<'PY'
import csv, pathlib, sys
ids = [line.strip() for line in pathlib.Path(sys.argv[1]).read_text().splitlines()
       if line.strip() and not line.startswith('#')]
with open(sys.argv[2], newline='') as stream:
    rows = {row['instance']: row for row in csv.DictReader(stream)}
missing = [name for name in ids if name not in rows]
expected = [f'finding_nemo_pb_{n}_pe_.ltl' for n in range(1, 8)]
if missing != expected or len(rows) != 1517:
    raise SystemExit(f'unexpected missing/duplicate v1 pairs: {missing}')
with open(sys.argv[3], 'w', newline='') as stream:
    writer = csv.DictWriter(stream, fieldnames=('instance', 'result', 'seconds', 'exit'))
    writer.writeheader()
    for name in ids:
        writer.writerow(rows.get(name, dict(instance=name, result='SYFCO-FAIL', seconds='0', exit='-1')))
PY
```

The archived 17 s TACAS23 CSV is
`benchmarking/plots/three-way-full-20260905/syntcomp26-full-v1.csv`.
Its [campaign provenance](../../plots/three-way-full-20260905/README.md)
and [follow-up provenance](../../plots/spot-otf-threeway-20260909/PROVENANCE.txt)
identify the same `5ffd8f99` frozen binary, empty runtime flags, adapted
SyFCo pairs, serial 17 s scopes, 8 GiB and no swap. The preserved executable
hash above matches the frozen provenance file. The 17 s CSV has 1,524 rows,
including seven `SYFCO-FAIL` rows, and is byte-identical (SHA-256
`169a76e6c926ab1fb2d1f5edd1a03844e7bffe640c1135fb477091e9ecac0ea3`)
to the archived `_bm-logs.three-way-full-20260905/v1.csv`; it can be reused.
The 60 s ltlsynt series is derived from the archived 120 s leg above. The
17 s leg is `benchmarking/witness-lifting-20260918/opening/17s/ltlsynt-cap17.csv`;
the installed binary hash matches the prior committed pin above. Their timings
exclude conversion and cache-hit metadata work, as in the legacy protocol.

## Export and three-way report

The full N loop exports its own CSV and `.raw.tsv` sidecar. B's 60 s epoch-1
CSV is generated above from the censored TSV; its archived 17 s epoch-1 CSV is
`benchmarking/witness-lifting-20260918/opening/17s/epoch-1/B-cap17-epoch1.csv`.

The external `run-subset.py` legs are already four-column cactus CSVs: the
new `v1-cap60.csv` above, the archived 17 s v1 CSV, the derived 60 s ltlsynt
CSV and the archived 17 s ltlsynt CSV. No `export-cactus` transformation applies
to those legs. Run this
three-way report separately at each cap after N and TACAS23 observations exist:

```bash
for CAP in 60 17; do
  REPORT="$CAMPAIGN/full/${CAP}s/report-$CHOSEN"
  mkdir -p "$REPORT"
  NEW="$CAMPAIGN/full/${CAP}s/$CHOSEN/N-cap$CAP.csv"
  if [ "$CAP" = 60 ]; then
    LTLSYNT="$CAMPAIGN/derived-60s/ltlsynt-cap60.csv"
    LTLSYNT_LABEL="ltlsynt (derived from 120 s)"
    TACAS23="$CAMPAIGN/full/60s/TACAS23/v1-cap60.csv"
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
    --title "SYNTCOMP26 — ltlsynt / TACAS23 / new Acacia ($CAP s, 8 GiB$TITLE_NOTE)" \
    --timeout "$CAP" --out-prefix "$REPORT/three-way" \
    --markdown "$REPORT/three-way-par2.md"
  if [ "$CAP" = 60 ]; then
    printf '\nltlsynt: derived by censoring archived uniform 120 s observations; not a 60 s run.\n' >> "$REPORT/three-way-par2.md"
  fi
done
```

## Wall-time planning from retained rows

The sums below are `Σ min(seconds, cap)` in **solver-row time**; systemd
startup, cooldown and campaign overhead add wall time. B epoch 1 and epoch 2
are near-identical. N has no observed integrated timing yet. A planning proxy
charges each eligible row its census eligibility time, the entire lift
allocation, and B epoch-1 time, clipped at the outer cap. The full-corpus
proxy also charges each declining row its census eligibility time. It assumes no lifting win, so it
is deliberately conservative; it is not a predicted PAR-2 score.

| Leg | 60 s cap | 17 s cap |
|---|---:|---:|
| B full, epoch 1 / epoch 2 (60 s derived) | 20,754 / 20,754 s | 6,691 / 6,687 s |
| B eligible subset, epoch 1 / epoch 2 (60 s derived) | 3,418 / 3,418 s | 1,004 / 1,003 s |
| N-a selection proxy (91 IDs) | 4,120 s (1.14 h) | 1,197 s (20.0 min) |
| N-b selection proxy (91 IDs) | 4,815 s (1.34 h) | 1,382 s (23.0 min) |
| N-a full proxy (1,524 IDs) | 21,508 s (5.97 h) | 6,936 s (1.93 h) |
| N-b full proxy (1,524 IDs) | 22,204 s (6.17 h) | 7,121 s (1.98 h) |
| ltlsynt retained leg (derived at 60 s) | 15,054 s (4.18 h; reuse) | 4,812 s (1.34 h; reuse) |
| TACAS23 retained 17 s rows, `Σ min(seconds, cap)` | 11,281 s lower bound | 11,229 s (3.12 h; reuse) |
| TACAS23 60 s planning proxy: charge 625 old TIMEOUTs at 60 s | 38,104 s (10.58 h) | — |

The TACAS23 60 s lower bound uses censored 17 s TIMEOUT observations and
cannot predict how many solve under 60 s. Its 10.58-hour proxy charges those
625 rows at the new cap and carries over the other observed times. All four N
selection legs, both N full legs, and TACAS23 60 s require actual measurements.

## VERDICT

The derived 60 s series have 1,524 rows each. All row times are at most 60 s,
and solved counts do not exceed their source 120 s counts. These are censored
120 s observations, not fresh 60 s runs.

| Derived series | Solved at 60 s | Solved at 120 s | PAR-2 at 60 s (total) |
|---|---:|---:|---:|
| B epoch 1 | 1,204 | 1,222 | 40,111.660 s |
| B epoch 2 | 1,203 | 1,221 | 40,174.029 s |
| ltlsynt | 1,282 | 1,293 | 30,270.656 s |

## Final: chosen portfolio on the obfuscated corpus

Run from the **main repository root** after all seven isolated 60 s exports
exist. The owner chooses one 4- or 5-arm portfolio from the selector output.
Run the selector with `--robustness` before that choice. For each size it
compares the leading subset's worst case against every challenger's best case
using the selection-relevant candidates on anomalous or thermal-unknown legs.
If the desired size is **ROBUST**, present its ranking to the owner at once and
move its thermal re-runs to after the obfuscated runs. Otherwise, re-run the
reported deciding arm/instance pairs before the owner picks that size.
The plain corpus gets only the stratified smoke run below; there is no separate
full plain-corpus portfolio leg. Run each timed command serially. The final
binary is frozen at Acacia
`f74ad9d14bb8879550d1af0126d371ebfd4e6253`, SHA-256
`65530fb4ab03245c2f8d9d63f09b6a1bf22bde1599b118755098ed14495c5e96`.
Arm 7, `real:param-lift:oxidd`, uses its own frozen `build_perarm_m2`
binary for selection. The sealed mapping is not needed for selection, smoke, or
either Acacia run.

```bash
set -euo pipefail
ROOT=$(pwd)
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
echo '65530fb4ab03245c2f8d9d63f09b6a1bf22bde1599b118755098ed14495c5e96  '"$BIN" | sha256sum -c -
echo 'c38d1346f37e5fb93f22210721a54e27c929f0cb93be3b9cf658a7f221ad805e  '"$MANIFEST" | sha256sum -c -
python3 -s "$CAMPAIGN/verify-obfuscated-corpus.py" --corpus "$OBF_CORPUS" \
  --manifest "$MANIFEST" --list "$OBF_LIST" --tlsf-map "$OBF_MAP"
python3 -s "$CAMPAIGN/perarm-select.py" --list "$LIST" --robustness
```

Set `CHOSEN` to the owner's comma-separated portfolio, in the selected
order. The selector writes a fixed-seed sample of about 50 original names
covering virtual-best arm credits, all-arm timeouts and UNKNOWN-heavy rows.
It reports missing legs explicitly while the measurements are still running.
Check the written list before starting the smoke leg.

```bash
CHOSEN=${CHOSEN:?set CHOSEN to selected comma-separated 4 or 5 arms}
SMOKE="$CAMPAIGN/perarm-m1/smoke/final-portfolio"
mkdir -p "$SMOKE"
python3 -s "$CAMPAIGN/perarm-select.py" --list "$LIST" \
  --smoke-sample 50 --arms "$CHOSEN" --smoke-output "$SMOKE/stratified-50.list"
python3 -s "$ROOT/benchmarking/run-syntcomp26-coverage.py" \
  --bin "$BIN" --flags "--arms $CHOSEN" \
  --solver-label Acacia-smoke --preset otf_sparse_formula \
  --acacia-sha f74ad9d14bb8879550d1af0126d371ebfd4e6253 \
  --list "$SMOKE/stratified-50.list" --tlsf-map "$MAP" --tlsf-corpus "$CORPUS" \
  --caps 60 --memory-max 8G --memory-swap-max 0 --collect-rusage \
  --output "$SMOKE/Acacia-smoke.tsv"
python3 -s "$ROOT/benchmarking/run-syntcomp26-coverage.py" export-cactus \
  --summary "$SMOKE/Acacia-smoke-summary.tsv" --list "$SMOKE/stratified-50.list" \
  --cap 60 --output "$SMOKE/Acacia-smoke.csv"
python3 -s "$CAMPAIGN/smoke-compare.py" --list "$SMOKE/stratified-50.list" \
  --all-list "$LIST" --arms "$CHOSEN" --portfolio "$SMOKE/Acacia-smoke.csv"
```

Investigate every smoke mismatch before the full obfuscated legs. For thermal
labels, restart the updated `build_scratch/thermal/sample.sh` after the
isolated legs finish; it reads `build_scratch/thermal/current-leg` as an
override. The following loop labels each final run, checks the corpus and
frozen binary again, runs at one uniform cap, and exports a four-column cactus
CSV and raw sidecar. It does not open the sealed map.

```bash
trap 'rm -f "$ROOT/build_scratch/thermal/current-leg"' EXIT
for cap in 60 17; do
  echo '65530fb4ab03245c2f8d9d63f09b6a1bf22bde1599b118755098ed14495c5e96  '"$BIN" | sha256sum -c -
  echo 'c38d1346f37e5fb93f22210721a54e27c929f0cb93be3b9cf658a7f221ad805e  '"$MANIFEST" | sha256sum -c -
  python3 -s "$CAMPAIGN/verify-obfuscated-corpus.py" --corpus "$OBF_CORPUS" \
    --manifest "$MANIFEST" --list "$OBF_LIST" --tlsf-map "$OBF_MAP"
  out="$CAMPAIGN/full-obfuscated/${cap}s/Acacia"
  mkdir -p "$out"
  printf 'obf-%s\n' "$cap" > "$ROOT/build_scratch/thermal/current-leg"
  python3 -s "$ROOT/benchmarking/run-syntcomp26-coverage.py" \
    --bin "$BIN" --flags "--arms $CHOSEN" --solver-label Acacia \
    --preset otf_sparse_formula \
    --acacia-sha f74ad9d14bb8879550d1af0126d371ebfd4e6253 \
    --list "$OBF_LIST" --tlsf-map "$OBF_MAP" --tlsf-corpus "$OBF_CORPUS" \
    --caps "$cap" --memory-max 8G --memory-swap-max 0 --collect-rusage \
    --output "$out/Acacia-cap$cap.tsv"
  rm -f "$ROOT/build_scratch/thermal/current-leg"
  python3 -s "$ROOT/benchmarking/run-syntcomp26-coverage.py" export-cactus \
    --summary "$out/Acacia-cap$cap-summary.tsv" --list "$OBF_LIST" \
    --cap "$cap" --output "$out/Acacia-cap$cap.csv"
done
```

The external baselines use plain names because signal renaming preserved all
1,524 formulas (see `obfuscation-invariance-summary.md`). The 60 s ltlsynt
series is derived from archived 120 s observations; its 17 s series is
archived. Run the fresh TACAS23 60 s leg with the existing `acacia1x`
adapter and adapted SyFCo pairs, serially, then append the seven conversion
failures. The TACAS23 17 s CSV is archived. Do not credit TACAS23 crash
verdicts before the audit.

```bash
V1="$ROOT/_bm-logs.fmcad26-head-6dda2f3b-20260822/build/acacia-v1-best23/src/acacia-bonsai"
PAIRS="$ROOT/_bm-logs.three-way-full-20260905/syfco-adapted"
V1_OUT="$CAMPAIGN/full/60s/TACAS23"
mkdir -p "$V1_OUT"
echo '75fabd3c081030512a0fa5348244aee7382144e0043eb1da2b581651edf84cd8  '"$V1" | sha256sum -c -
python3 -s "$ROOT/benchmarking/run-subset.py" --tool acacia1x --bin "$V1" \
  --list "$LIST" --instances-dir "$PAIRS" --timeout 60 \
  --systemd-scope --memory-max 8G --memory-swap-max 0 \
  --csv "$V1_OUT/v1-convertible.csv"
python3 -s - "$LIST" "$V1_OUT/v1-convertible.csv" "$V1_OUT/v1-cap60.csv" <<'PY'
import csv, pathlib, sys
ids = [line.strip() for line in pathlib.Path(sys.argv[1]).read_text().splitlines()
       if line.strip() and not line.startswith('#')]
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
```

Only after **both** `Acacia-cap60.csv` and `Acacia-cap17.csv` exist, run the
join below. The script refuses earlier invocation before it even reads the
sealed mapping; it pins that mapping to SHA-256
`70f9339504d3fe6ed5deef47268914c08cb74c79c60059e254599e95597b91b9`.
It maps external baselines onto obfuscated IDs, flags TACAS23 verdicts that
contradict another tool or the TLSF status (including corrected status
exceptions), records both unexpected exits and signal crashes, and scores suspicious verdicts
as errors. It writes joined CSVs, per-cap unique-solve/PAR-2 tables, and
`cactus-report.py` plots and reports.

```bash
python3 -s "$CAMPAIGN/join-obfuscated-threeway.py" \
  --acacia60 "$CAMPAIGN/full-obfuscated/60s/Acacia/Acacia-cap60.csv" \
  --acacia17 "$CAMPAIGN/full-obfuscated/17s/Acacia/Acacia-cap17.csv" \
  --ltlsynt60 "$CAMPAIGN/derived-60s/ltlsynt-cap60.csv" \
  --ltlsynt17 "$ROOT/benchmarking/witness-lifting-20260918/opening/17s/ltlsynt-cap17.csv" \
  --tacas60 "$CAMPAIGN/full/60s/TACAS23/v1-cap60.csv" \
  --tacas17 "$ROOT/benchmarking/plots/three-way-full-20260905/syntcomp26-full-v1.csv" \
  --sealed-map "$CAMPAIGN/generic-selection/sealed-mapping.jsonl" \
  --obfuscated-list "$OBF_LIST" \
  --original-map "$MAP" --original-corpus "$CORPUS" \
  --status-exceptions "$ROOT/benchmarking/syntcomp26-status-exceptions.tsv" \
  --out "$POST/three-way"
python3 -s "$CAMPAIGN/thermal-annotate.py" \
  --samples "$ROOT/build_scratch/thermal/samples.tsv" --main-root "$ROOT" \
  --chosen-arms "$CHOSEN" \
  --final60 "$CAMPAIGN/full-obfuscated/60s/Acacia/Acacia-cap60.csv" \
  --final17 "$CAMPAIGN/full-obfuscated/17s/Acacia/Acacia-cap17.csv" \
  --out "$POST/thermal"
```

The thermal annotator reports each leg and final-run median package
temperature, package throttle events per second, minimum available memory,
and monitor coverage. Coverage is the fraction of 1,524 corpus row positions
spanned by the first and last samples. A clean leg has at least 80% coverage;
the throttle baseline is the median rate of completed clean legs. A leg is
anomalous only when its rate exceeds that median by more than 25%. A leg with
less coverage is **thermal unknown** and its selection-relevant set is
recommended regardless of its observed rate. No leg rerun list is generated
until all seven isolated exports exist.

The leg rerun set is the union over the top three 4-arm and top three 5-arm
subsets. For each member arm, it contains a TIMEOUT, MEMOUT, or ERROR that no
other subset arm solves; a solve in the closed **[0.8×cap, cap]** window; and
a unique solve or fastest-arm solve whose removal raises that subset's PAR-2
contribution by more than B's two-epoch noise on the same instance. For each
instance, that noise is the absolute difference between B epoch 1 and epoch 2
at 60 s, counting a solve at its seconds and an unsolved result at 120 s.
The final obfuscated run uses TIMEOUTs and the same cap-relative window:
**[48, 60] s** at 60 s and **[13.6, 17] s** at 17 s. Each recommended set
reports its size, estimated rerun time based on prior capped runtimes, and a
cap-based upper bound. `rerun-commands.sh` is written for review, not run.
