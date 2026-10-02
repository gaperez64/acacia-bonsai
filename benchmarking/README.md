# Benchmarking protocol

[RESULTS.md](RESULTS.md) is the one current results-and-gaps report.
[DATA-STRUCTURES.md](DATA-STRUCTURES.md) is the retained architecture reference
for antichains, state vectors and the forward graph. Historical campaign
narratives and original measurements are in the archives named by
[evidence-index.tsv](evidence-index.tsv). The active work is in
[optimize-20260927/plan.md](optimize-20260927/plan.md).

## Evidence and retention

Start every new campaign in an ignored `_bm-logs*/` directory or outside the
checkout. That includes raw rows, route records, proofs, thermal samples,
plots, and generated reports from their first observation. Do not first write
raw data under `benchmarking/` and later remove it. Keep original rows and
correction sidecars together; do not normalize old observations in place.

When a campaign closes, use `scripts/acacia-evidence.py` to pack and verify its
original inputs and outputs, publish the archive to an authorized durable
location, read it back, and commit its URL, SHA-256, status, and provenance to
[evidence-index.tsv](evidence-index.tsv). **Durable evidence is the verified
external archive plus its committed index row.** The index does not turn a
partial or stopped campaign into a completed measurement. Fetch and verify
archived inputs into a separate directory with:

```sh
python3 -s scripts/acacia-evidence.py fetch --campaign ID --dest DIR
```

Restored archives preserve their `benchmarking/` paths under `DIR`. Reporting
tools in [tools/](tools/) accept that root or explicit input paths. Fetch is
opt-in; ordinary builds, tests, and solver runs stay offline. Frozen executables
are pinned by SHA-256 in [baselines.tsv](baselines.tsv) and also archived; G1
must use the matching old configuration's executable. The read-only/default
`scripts/prune-artifacts.sh` respects the registries and write-protected local
artifacts. Do not prune active, unpublished, unadjudicated, or sole-copy local
evidence before archive verification.

CI runs `scripts/check-evidence-growth.py` to reject new generated rows, logs,
proof bundles, thermal data and compressed archives in Git. Its small fixture
exceptions are explicit in [evidence-growth-allowlist.tsv](evidence-growth-allowlist.tsv).

## Running and reporting

Build the compile-time presets with `./self-benchmark.sh -R -c PRESET` and use
the registry in `config/acacia-presets.json` to identify options. Run both
binaries on exactly the same instance list, one solver at a time, recording
verdict, wall time, exit code, cap, binary SHA-256 and resource regime. For
example:

```sh
python3 -s benchmarking/run-subset.py \
  --bin build_best_decomp_mona/src/acacia-bonsai \
  --list tests/suites/benchmarks/syntcomp26/all.list \
  --timeout 17 --systemd-scope --memory-max 8G \
  --csv _bm-logs/best_decomp_mona.csv
python3 -s benchmarking/cactus-report.py \
  --csv baseline=_bm-logs/best_decomp_mona.csv \
  --csv candidate=_bm-logs/otf_sparse_formula.csv \
  --timeout 17 --out-prefix _bm-logs/comparison \
  --markdown _bm-logs/comparison.md
```

For `run-syntcomp26-coverage.py` observations, export a uniform-cap CSV with
`benchmarking/run-syntcomp26-coverage.py export-cactus --summary SUMMARY --runs RUNS --list LIST
--cap 17 --output CSV`. Export validates exact list membership, one row per
ID, finite nonnegative times, summary/run agreement, solved verdict/exit
agreement, and recorded provenance. It rejects staged or missing observations
and unresolved conflict sidecars. The accompanying `.raw.tsv` keeps original
result, exit, wall time and cap for subtype counts. CSVs alone do not carry a
cap or source identity. `cactus-report.py` reports PAR-2, solved REAL/UNREAL,
TIMEOUT, RESOURCE_LIMIT, UNKNOWN, ERROR, and SYFCO-FAIL separately. A virtual
best is a derived per-instance best of named series, never a measured runner.

`rank_bm_logs.py` scores `_bm-logs/` configurations by PAR-2. For Meson
`--slice=N/M` runs, first use `aggregate_bm_slices.py` with `--min-slices` to
exclude incomplete sets. `loss-set.py`, `ltlsynt_ablation_report.py`,
`run_diag_targets.py`, and `summarize-diag-phases.py` inspect individual losses
and phase stalls. `speedup-scatter.py` provides a paired per-instance view.
These tools read explicit input paths and write to an ignored output directory.

## Corpus and panel selection

For TLSF-backed SYNTCOMP25/26 suites, `syntcomp-corpus.py materialize` creates
a flat corpus from the `tests/syntcomp-benchmarks` submodule, verifies it and
records `.acacia-tlsf-corpus-path` unless `--no-record` is set. A corpus can be
selected by `--tlsf-corpus DIR`, `ACACIA_TLSF_CORPUS=DIR`, or the build's
`-Dacacia_tlsf_corpus_dir=DIR`, in that order. G1 uses the same lookup without
a CLI corpus flag. Stale paths are skipped; configure Meson with the corpus
directory to enumerate corpus suites. The benchmark suite lists and TLSF
source maps fix logical instance identity; use the same set and frontend for
both candidates. A stratified panel is a screen, not a full-corpus result.

## Gates

- **G0, correctness:** `meson test -C build --suite=unit` and
  `meson test -C subprojects/posets/build`; both must report `Fail: 0`.
- **G1, frozen verdicts and coverage:**
  `benchmarking/regression-gate.sh --baseline-bin PREVIOUS-BIN build`.
  `--baseline-bin` or `REGRESSION_BASELINE_BIN` is required, with no default.
  Use the same configuration built from the previous revision. All 40
  sentinels must pass and the script must print `GATE PASS`; verdict changes
  and coverage losses fail. Its baseline CSV uses frozen verdicts and
  `baseline_seconds` from `tests/suites/benchmarks/regress-expected.tsv`.
  The binary serves near-cap remeasurement only.
- **G2, Posets proxy:** `benchmarking/posets-microbench.sh` (advisory).
- **G2s, solver-profile proxy:**
  `benchmarking/solver-profile-gate.sh BASELINE-BIN CANDIDATE-BIN`.
  The SYNTCOMP25 `mixed` target is `evasion0.ltl`.
- **G3, landing bar:** `benchmarking/landing-campaign.sh` compares paired
  binaries, suite lists and a 17 s timeout using `landing-bar.py`; every suite
  must print `GATE PASS`. `--scope-mode instance` keeps the runner outside
  each solver's 8 GiB scope so solver OOM is recorded. The default `campaign`
  mode shares the scope. A mode change needs a fresh output directory and
  matched remeasurement of both binaries.
- **G4, corpus correctness:**
  `meson test -C build --num-processes 1 --suite=ab/realizable
  --suite=ab/unrealizable`; timeouts are allowed, but `Fail: 0` and no false
  verdict marker are required.
- **G5, native TLSF parity:** run `tlsf-verdict-parity.py` and
  `check-tlsf-conversion.py` against the selected TLSF corpus.

The frozen G1 shipping-preset twin is
`best_decomp_rank_bucketed_mona_eq_min_blocks_2`, whose binary SHA-256 is
`6467869a4411233ec148f7136fe6a6595a43205cc2bbd412f8d8beacb55ec2e9`.
It is pinned in [baselines.tsv](baselines.tsv). The old
`build_best_decomp_mona` default was a different configuration; substituting
it changes the question answered by near-cap remeasurement. Verdict flips and
row-set mismatches are configuration-independent regressions. A lost solve
against a different configuration is not evidence of a change regression;
establish one with a same-configuration previous-revision comparison such as
G3. G1 still fails on either category.

## Measurement regime

Performance gates use a 17 s per-instance cap and run sequentially in a user
systemd scope with `MemoryMax=8G` and `MemorySwapMax=0`. Campaign tools create
process groups for individual solvers inside that scope. PAR-2 charges twice
the cap for every timeout, UNKNOWN, resource limit, or error, reporting those
categories separately. If a lost baseline answer took more than 80% of the
cap, `landing-bar.py` remeasures both binaries at three times the cap before
deciding. Native TLSF primary runs and their diagnostics both use
`--tlsf-only` so an available `.ltl` sibling cannot change the frontend.
Longer diagnostic answers affect the gate decision, never the primary 17 s
coverage. Report censored higher-cap rows as **derived**, with their source
and transformation recorded.

Read PAR-2-only changes against the measured same-configuration noise floor.
Three baseline runs spanned 2778.691–2799.825 s on SYNTCOMP25 (**21.134 s**)
and 1702.186–1714.260 s on SYNTCOMP26 (**12.074 s**). A change inside that
spread alone is not performance evidence; coverage changes and per-instance
losses remain gate evidence.
