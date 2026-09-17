# Closing three-way comparison — demand-sparse sprint

**Outcome: no package was admitted, so the closing candidate is the baseline configuration.** Following plan §9.6,
the required three-way report reuses the baseline's primary observations for the candidate label instead of running
the same executable twice. The two candidates that were measured end to end and not admitted appear only in the
supplementary report.

## Primary report (required)

[three-way-par2.md](three-way-par2.md), [three-way.png](three-way.png), [three-way.pdf](three-way.pdf), from
[acacia-baseline.csv](acacia-baseline.csv), [acacia-candidate.csv](acacia-candidate.csv) (byte-identical copy of the
baseline rows) and [ltlsynt.csv](ltlsynt.csv).

| Series | Solved / 1,524 | REAL | UNREAL | PAR-2 total (s) | PAR-2 mean (s) | Timeouts | UNKNOWN | Memory failures | Errors | SYFCO-FAIL | Dataset SHA-256 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| Acacia baseline | 1,176 | 533 | 643 | 12,624.620 | 8.284 | 345 | 1 | 2 | 0 | 0 | `922ac353…` |
| Acacia candidate (= baseline) | 1,176 | 533 | 643 | 12,624.620 | 8.284 | 345 | 1 | 2 | 0 | 0 | `922ac353…` |
| ltlsynt | 1,258 | 574 | 684 | 9,504.731 | 6.237 | 254 | 2 | 0 | 3 | 7 | `f81beba8…` |

## Supplementary report (evaluated, not admitted)

[supplementary-evaluated-par2.md](supplementary-evaluated-par2.md), [.png](supplementary-evaluated.png),
[.pdf](supplementary-evaluated.pdf), adding [evaluated-p0-p1a-not-admitted.csv](evaluated-p0-p1a-not-admitted.csv)
and [evaluated-p0-not-admitted.csv](evaluated-p0-not-admitted.csv).

| Series | Solved | REAL | UNREAL | PAR-2 total (s) | PAR-2 mean (s) | Why not admitted |
|---|---:|---:|---:|---:|---:|---|
| P0+P1a, preset `otf_sparse_formula_loss_hints` | 1,177 | 533 | 644 | 12,553.750 | 8.237 | No confirmed §8.2 benefit; g-unreal-115 validated slower (51 s: 17.44–19.04 s vs 16.31–16.69 s) |
| P0, preset `otf_sparse_formula` | 1,173 | 532 | 641 | 12,672.294 | 8.315 | Lower confirmed success on three near-cap instances; +1.0–1.4% cycles (G2s), attributed to code placement |

Single primary observations only; the PAR-2 differences are not §8.2 evidence. Five-round confirmations and 51 s
adjudications are in [confirmations.tsv](confirmations.tsv); every status and beyond-resolution time difference is in
[gains-losses.tsv](gains-losses.tsv). Package-level reasoning is in [../decisions.md](../decisions.md).

## Identities

| Role | Executable | SHA-256 | Configuration |
|---|---|---|---|
| Baseline (= candidate) | `_bm-logs.20260915-closing/bin/candidate-f7919b83` | `0f4f48b6b751be91d9e0575105d4678d47dbccb8c998c01d71f9597abe22fb99` | preset `otf_sparse_formula`, shipped four arms; solver source identical to `6410fd34` |
| Evaluated P0+P1a | `bin/cand-hints-4cbf975c` (branch `research/ds-p1a-loss-hints`) | `936b11b8b2c711bbc9296719b2018bdf25ec5bb8778b5fb60bdc4865916d29ae` | preset `otf_sparse_formula_loss_hints` |
| Evaluated P0 | `bin/retained-e27dd528` (branch `research/ds-p0-lifecycle-records`) | `14b71f879273a287df70e5fe692b951e9eaf546ccb3ae91648bdcf143f0e2246` | preset `otf_sparse_formula` |
| External | `/usr/local/sbin/ltlsynt`, ltlsynt (spot) 2.15.1.dev; SyFCo v1.2.1.2 | `ea761a1c0594278bd4b525369977677520c8b9d3dcc2f5a45dab91d961663900` | existing `run-subset.py` adapter |

Paths under `bin/` are relative to `_bm-logs.20260916-demand-sparse/`; full hashes of every frozen executable are in
its `bin/freeze-manifest.tsv`. [manifest.json](manifest.json) records these identities, the corpus, harness and host.

## Regime and timing boundary

SYNTCOMP26 `tests/suites/benchmarks/syntcomp26/all.list` (1,524 IDs) with its native TLSF source map; uniform 17 s
cap; `MemoryMax=8G`, `MemorySwapMax=0` per whole invocation; no CPU quota; strictly sequential invocations on a quiet
machine; host i7-11850H (16 threads), kernel 7.2.5-200.fc44. All three primary series were measured on 2026-09-17 in
one epoch: P0+P1a 04:13–06:05, baseline 06:06–07:58, ltlsynt 08:00–09:22, P0 13:31–15:23 UTC. The 2026-09-15/16 rows
were not reused because near-cap outcomes had drifted (see [../reuse.md](../reuse.md)).

Acacia times include its native TLSF frontend. The ltlsynt route excludes the cached SyFCo conversion from timed
synthesis (cache seeded from the 2026-09-15 run's identical conversions), as in earlier three-way reports; conversion
failures stay visible as SYFCO-FAIL. Every nonsolved observation, including one that returns immediately, is charged
34 s by the shared `benchlib.par2`. Primary rows are single observations; confirmation rounds and 51 s answers never
replace them.

## Exact commands

The retained three-way workflow is `_bm-logs.20260916-demand-sparse/scripts/closing.sh` (SHA-256 `78201510…`) and
`closing2.sh` (`bb2038e4…`) around the existing tools. Acacia legs (per series, with its own binary, preset, SHA and
output):

```sh
python3 benchmarking/run-syntcomp26-coverage.py --bin "$BIN" --solver-label "$LABEL" \
  --list tests/suites/benchmarks/syntcomp26/all.list --tlsf-map tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv \
  --tlsf-corpus "$CORPUS" --caps 17 --memory-max 8G --memory-swap-max 0 --conflict-policy collect --collect-rusage \
  --preset "$PRESET" --acacia-sha "$SHA" --output "$OUT.tsv" --resume
```

External leg:

```sh
python3 benchmarking/run-subset.py --tool ltlsynt --bin /usr/local/sbin/ltlsynt --list all-instances.list \
  --tlsf-map tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv --tlsf-corpus "$CORPUS" --syfco-cache syfco-cache \
  --systemd-scope --memory-max 8G --memory-swap-max 0 --timeout 17 --csv ltlsynt.csv
```

Export and report (tools from PR #187, `sprint/ds-threeway-report` at `7919a06d`; no solver is run):

```sh
python3 benchmarking/run-syntcomp26-coverage.py export-cactus --summary "$OUT-summary.tsv" --runs "$OUT.tsv" \
  --list tests/suites/benchmarks/syntcomp26/all.list --cap 17 --output acacia-baseline.csv
python3 benchmarking/cactus-report.py --csv "Acacia baseline=acacia-baseline.csv" \
  --csv "Acacia candidate (same as baseline, nothing admitted)=acacia-candidate.csv" --csv "ltlsynt=ltlsynt.csv" \
  --title "SYNTCOMP26 — baseline / candidate / ltlsynt (17 s, 8 GiB)" --timeout 17 \
  --out-prefix three-way --markdown three-way-par2.md
```

Confirmations: `scripts/screen.sh` (five alternating rounds through the same coverage driver) evaluated by
`benchmarking/paired-admission.py`; tables: `scripts/closing-tables.py`. Export validated the exact 1,524-ID set,
uniform cap, finite times and verdict/exit agreement for every Acacia series; no conflicts were collected.
The `export-cactus` CSVs normalize MEMOUT to RESOURCE_LIMIT and CRASH to ERROR for the plotter only; the
`*.raw.tsv` sidecars keep the original status, exit code, time and cap.
