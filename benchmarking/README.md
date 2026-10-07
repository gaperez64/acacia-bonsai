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

## Memory collection

`benchlib.run_systemd_scope` remains the shared invocation runner. On Linux it
creates a fresh delegated systemd scope for each invocation, with an owner and a
workload in sibling cgroups. The requested memory/swap limits apply to the workload;
the small owner remains outside that limit, holds the empty workload cgroup after
normal exit, timeout or OOM, and waits for the external driver's acknowledgement.
The driver reads `memory.peak` and `memory.events` before deletion and requires an
empty `cgroup.events` populated state before and after those reads. Any owner error
invalidates both observations even when the cgroup path and numeric files are present;
the original error is retained as both missing reasons. A still-populated cgroup or
unreadable/invalid drain state also withholds peak/events. Valid reaped-process RSS
remains independently available. Group OOM stays
disabled by default, preserving the prior child-OOM behavior; its explicit setting
is recorded. CPU properties still bound the complete scope. This collector changes
measurement overhead and scope layout: use fresh matched runs, not old timings.

`scope_memory_peak_bytes` retains its schema name and means the concurrent workload
cgroup peak, including all solver children. `max_process_rss_bytes` is the Linux
`wait4.ru_maxrss` maximum for the reaped process tree, recorded by default; descendants
that the solver did not reap may not contribute to this RSS statistic. RSS is never
the concurrent scope peak. GNU time remains an optional CPU/RSS diagnostic. A missing
peak, zero/sentinel read, unsupported platform or failed collector carries a reason;
virtual reservations and OOM-only observations cannot replace a peak. Non-Linux
runs report unsupported cgroup/RSS fields and cannot supply the Linux memory gate.
Unscoped/campaign-shared runs carry explicit missing reasons; G3's instance mode
is required for per-invocation memory evidence.

Native coverage, portfolio-pair and `run-subset.py --raw-tsv FILE` keep their existing
primary columns and write sources, cgroup path, OOM setting and missing reasons to
`FILE.memory.tsv`. Current readers join this sidecar by observation identity and
measurements; master readers can still read the primary TSV. `run-subset.py --csv FILE`
also writes `FILE.memory.tsv` by default. Resuming an expanded primary TSV preserves
its original bytes in `*-legacy.tsv` before moving provenance into the sidecar.
Journal annotation records parent accounting in `parent_scope_memory_peak_bytes`
and `parent_scope_memory_swap_peak_bytes`, separately from workload collection.

Joins and paired reports give numerator/denominator and missing reasons separately
for scope peak and process RSS. Explicit missing reasons and absent/invalid collection
provenance exclude even numeric observations. A cohort median requires 100% coverage (a global,
predeclared rule); otherwise it is suppressed and selection by outcome, including
MEMOUT, is disclosed. Join denominators include every campaign row, including
nonexecuted conversions; maxima describe only the observed subset.

Run the real Linux lifecycle panel **outside the sandbox**, from this worktree:

```sh
set -o pipefail
mkdir -p _bm-logs.p0-memory-validation
/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 -u tests/check-scope-memory.py \
  | tee _bm-logs.p0-memory-validation/lifecycle.jsonl
```

It uses the shared runner at 64 MiB/no swap for normal exit, timeout, child OOM and
explicit group/invocation OOM. It asserts four distinct cgroups, positive authoritative
peaks, retained OOM events, correct classifications and RSS or an explicit reason.
The host needs a delegated Linux v2 memory controller, `memory.peak`, `cgroup.kill`
and a functioning user systemd manager. Failure remains visible; there is no fallback
memory estimate. The fake panel is `tests/pytest/test_scope_memory.py`.

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
  `benchmarking/solver-profile-gate.sh --rule optimization BASELINE-BIN CANDIDATE-BIN`.
  Select the rule before measuring: `optimization` applies the existing 5% improvement
  proxy (or the existing single-target pass to G3); `membership-default` requires
  no target beyond the same 6% regression ceiling and passes to G3 without a speedup
  requirement. Both record the rule and thresholds in `g2s-rule.txt` and `decision.txt`.
  The SYNTCOMP25 `mixed` target is `evasion0.ltl`.
- **G3, landing bar:** `benchmarking/landing-campaign.sh` compares paired
  binaries, suite lists and a 17 s timeout using `landing-bar.py`; every suite
  must print `GATE PASS`. `--scope-mode instance` keeps the runner outside
  each solver's 8 GiB scope so solver OOM is recorded. The default `campaign`
  mode shares the scope. A mode change needs a fresh output directory and
  matched remeasurement of both binaries.
- **G4, corpus correctness:**
  `python3 benchmarking/corpus-correctness-gate.py build` runs Meson's
  realizable/unrealizable suites and reads their JSON records. Timeouts are allowed;
  failed tests, false-verdict markers (including in timed-out tests), missing records
  and unexplained launcher exits fail. Meson's exit status alone is insufficient.
- **G5, native TLSF parity:** run `tlsf-verdict-parity.py` and
  `check-tlsf-conversion.py` against the selected TLSF corpus. Use
  `tlsf-verdict-parity.py --prepare --native-inspect build/tests/tlsf-frontend-inspect
  --canonicalizer build/tests/ltl-formula-canonicalize` with its usual required
  arguments. Preparation and spot-check failures do not skip parity on available
  pairs; the summary reports those exits and missing conversions separately, and
  any unresolved failure keeps the gate failed. No implicit known-instance waiver.
  Raw-LTL pairs must use the effective Mealy target: `convert-tlsf-corpus.py
  SOURCE PAIRS --target Mealy --canonicalizer build/tests/ltl-formula-canonicalize`.
  This adapts SyFCo's plain formula once using its declared source semantics;
  the native inspector already adapted its formula. Normalization timeout is UNKNOWN,
  never evidence of equivalence. Plain pairs remain appropriate for tools taking
  their own semantics flag.

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
