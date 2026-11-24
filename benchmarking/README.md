# Benchmarking

Run commands from the repository root in Bash. [Results](RESULTS.md) gives
measurements; [solver contracts](DATA-STRUCTURES.md) and
[decisions](#decisions) explain the arms and policies. Historical procedures
and the previous guide are archived in docs-20261122.

## Try a small run

Use the [debug build](../README.md#build). These two checked-in inputs need
neither a downloaded corpus nor systemd:

```bash
build_dir=${ACACIA_BUILD_DIR:-build}
example_dir=${ACACIA_EXAMPLE_DIR:-_bm-logs/quickstart}
mkdir -p "$example_dir"
python3 - "$example_dir" <<'PYINPUTS'
from pathlib import Path
import sys

out = Path(sys.argv[1])
cases = {
    'real': 'tests/ltl/realizable/01.tlsf',
    'unreal': 'tests/ltl/unrealizable/simple_arbiter_unreal1_4_1.tlsf',
}
(out / 'inputs.list').write_text(''.join(f'{name}\n' for name in cases))
(out / 'sources.tsv').write_text(''.join(f'{name}\t{path}\n' for name, path in cases.items()))
PYINPUTS
python3 -s benchmarking/run-syntcomp26-coverage.py subset \
  --bin "$build_dir/src/acacia-bonsai" \
  --list "$example_dir/inputs.list" --tlsf-map "$example_dir/sources.tsv" \
  --timeout 17 --csv "$example_dir/run.csv"
python3 -s benchmarking/cactus-report.py \
  --csv "debug=$example_dir/run.csv" --timeout 17 \
  --title 'Local debug example' --out-prefix "$example_dir/report" \
  --markdown "$example_dir/report.md"
```

Expect REALIZABLE and UNREALIZABLE. `run.csv` holds wall times; `report.md`
holds the summary. Use matched release builds for performance comparisons.

## Evidence and retention

Write raw rows, logs, route records, proofs, thermal samples and plots to ignored
`_bm-logs*/` directories or outside the checkout from the start. Preserve original
observations and correction sidecars together.

Close campaigns with `scripts/acacia-evidence.py`: pack, publish to an authorized
durable location, read back and verify, then register URL, SHA-256, status and
provenance in [evidence-index.tsv](evidence-index.tsv). Partial campaigns stay
partial. Restore evidence with:

```sh
python3 -s scripts/acacia-evidence.py fetch --campaign ID --dest DIR
```

Fetch checks digests and member layout. Paths remain relative to the restore root;
[reporting tools](tools/) accept that root or explicit paths. Fetch is opt-in.
New manifests preserve executable modes; binaries in pre-#245 archives need
`chmod +x DIR/PATH/TO/FROZEN-BINARY` after restoration.

Frozen binaries are pinned in [baselines.tsv](baselines.tsv); its `archive` column
locates each executable and `path` records its historical location.
`scripts/prune-artifacts.sh` reports by default, respects both registries and
write-protected artifacts, and deletes only with `--delete`. Keep active,
unpublished, unadjudicated and sole-copy evidence until archive verification.
Preserve [publication holds](evidence-do-not-publish.txt), including relocated
mapping data. CI's `scripts/check-evidence-growth.py` rejects generated bulk;
fixture exceptions are in [evidence-growth-allowlist.tsv](evidence-growth-allowlist.tsv).

## Running and reporting

Build presets with `./self-benchmark.sh -R -c PRESET`. Compare identical input
lists, frontend, cap and resource limits, one solver at a time. Record verdict,
wall time, exit status, binary SHA-256 and build options. With the baseline and
`CANDIDATE_BUILD` compiled:

```sh
preset=$(python3 scripts/acacia-config.py list-group docker_default | head -n1)
python3 benchmarking/syntcomp-corpus.py init
python3 benchmarking/syntcomp-corpus.py materialize --out _bm-logs/tlsf-corpus
python3 -s benchmarking/run-syntcomp26-coverage.py subset \
  --bin "build_${preset}/src/acacia-bonsai" \
  --list tests/suites/benchmarks/syntcomp26/all.list \
  --tlsf-map tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv \
  --tlsf-corpus _bm-logs/tlsf-corpus \
  --timeout 17 --systemd-scope --memory-max 8G \
  --csv _bm-logs/baseline.csv
python3 -s benchmarking/run-syntcomp26-coverage.py subset \
  --bin CANDIDATE_BUILD/src/acacia-bonsai \
  --list tests/suites/benchmarks/syntcomp26/all.list \
  --tlsf-map tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv \
  --tlsf-corpus _bm-logs/tlsf-corpus \
  --timeout 17 --systemd-scope --memory-max 8G \
  --csv _bm-logs/candidate.csv
python3 -s benchmarking/cactus-report.py \
  --csv baseline=_bm-logs/baseline.csv \
  --csv candidate=_bm-logs/candidate.csv \
  --title "Baseline and candidate" --timeout 17 --out-prefix _bm-logs/comparison \
  --markdown _bm-logs/comparison.md
```

`run-syntcomp26-coverage.py` supports staged caps and resume. `subset` accepts
native TLSF or converted pairs, including ltlsynt; conversion is untimed.
`summarize` and `export-cactus` need no solver or campaign guard.

### Summaries and export

The default summary covers the sorted union of observed IDs for the current label
in raw rows and conflicts, regardless of resume list or limit. Its cap is the
maximum requested or recorded cap for that label; requested caps persist in
`<output>-requested-caps.tsv`. The first deciding cap supplies the answer/time.
Failure fields require an observation at the overall cap.

```sh
python3 -s benchmarking/run-syntcomp26-coverage.py summarize --runs RUN.tsv
python3 -s benchmarking/run-syntcomp26-coverage.py summarize --runs RUN.tsv \
  --solver-label LABEL --list PANEL.list --cap 60
```

Explicit panels keep their order, reject duplicate IDs and include unobserved IDs
as unsolved with unknown failures. `--cap` is a floor. Older campaigns without
saved requests need their original maximum when later stages were never reached.
Conflicts stay in their sidecar and make summarization/collect-policy resumes
exit 3, including when omitted by the current list. Stop-policy runs save the
conflict and summary before exiting. A rebuild never adjudicates a conflict.

```sh
python3 -s benchmarking/run-syntcomp26-coverage.py export-cactus \
  --summary SUMMARY --runs RUNS --list LIST --cap 17 --output CSV
```

Export requires exact list membership, one row per ID, finite nonnegative times,
summary/run agreement, verdict/exit agreement and provenance. It rejects staged or
missing observations and unresolved conflicts. The `.raw.tsv` sidecar keeps
failure subtypes; CSV alone has no cap or source identity.
`cactus-report.py` separates REAL/UNREAL, TIMEOUT, RESOURCE_LIMIT, UNKNOWN, ERROR
and SYFCO-FAIL. A virtual best is derived per input and cannot identify the winner
of a measured race. `classify` and `threeway` report winner attribution and
comparisons; `landing-bar.py` implements admission.

### Obfuscated SYNTCOMP26 screens

Prepare each treatment in its own directory with the self-checking `tlsfobf`:

```sh
python3 -s benchmarking/obfuscate-corpus.py \
  --tlsfobf /path/to/tlsfobf \
  --tlsf-map tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv \
  --tlsf-corpus /path/to/original-corpus \
  --list tests/suites/benchmarks/syntcomp26/all.list \
  --mode all --seed 145 --output /path/to/evidence/obfuscated

python3 -s benchmarking/run-syntcomp26-coverage.py \
  --bin /path/to/acacia-bonsai --solver-label obfuscated \
  --list /path/to/evidence/obfuscated/instances.list \
  --tlsf-map /path/to/evidence/obfuscated/tlsf-sources.tsv \
  --tlsf-corpus /path/to/evidence/obfuscated/corpus \
  --status-exceptions /path/to/evidence/obfuscated/status-exceptions.tsv \
  --caps 17 --memory-max 8G --memory-swap-max 0 \
  --conflict-policy collect --output /path/to/evidence/obfuscated-screen.tsv

python3 -s benchmarking/compare-obfuscated-screen.py \
  --plain /path/to/evidence/plain-screen.tsv \
  --obfuscated /path/to/evidence/obfuscated-screen.tsv \
  --manifest /path/to/evidence/obfuscated/manifest.tsv \
  --noise-s 0.5 --output /path/to/evidence/comparison.tsv
```

Use the same binary, flags, single cap and resources for plain and obfuscated
runs. The plain panel may be larger; the obfuscated screen must be complete.
Totals use accepted inputs; report rejections separately. Opposing decisive
verdicts exit 3. Coverage flips do not fail the comparison. `--noise-s` is an
absolute per-input threshold, not a calibrated noise floor; unsolved times are
censored.

`rename` removes identifier/INFO cues; `reorder` permutes declarations, clauses
and commutative operands; `all` combines them. All modes drop comments and the
SYNTCOMP trailer, including status/reference-size annotations. Preparation applies
reviewed status corrections to the originals and writes a generated exception
table bound to absent annotations. Pass that table to the obfuscated run. Unknown
expectations stay unknown; the manifest retains original sources and expectations.

Preparation runs serially without a systemd scope, with a default 60 s per input.
Failures are recorded and omitted from map/list. Resume checks hashes, repairs
missing/corrupt outputs and reuses cached failures. Changed sources, tool, mode,
seed or timeout need a new directory. Neutral names follow a seeded permutation
of list order. Keep `maps/` to translate controller names back; it stays outside
`corpus/`.

## Bounded one-sided restrictions

`--restriction-prepass on|off` defaults to on for native TLSF decision runs with
legacy arms. One existing formula-unreal arm tries input identification; one
real arm tries constant outputs. Other arms start normally. Native-only portfolios
and synthesis (`-s`) skip these attempts.

Candidates use expanded declaration provenance. Input buses of at least two
members share their first member: try all buses together, then the first two
separately if there are multiple buses (at most three attempts). The output
candidate sets all non-family outputs false. Substitution covers the complete
positive effective Mealy objective, including assumptions and strict-semantics
guards, with the original I/O map. Only UNREAL input-restricted or REAL
output-restricted results transfer, following tlsf-tools `Restriction.lean`.

Each attempt runs in a private subprocess capped at 512 MiB address space/RSS.
Input attempts have 500 ms each and 1 s total; the output attempt has 200 ms.
The invocation deadline bounds both. ASan omits the address-space cap. Timeout,
resource/error, opposite and inconclusive results resume the original objective.
`restriction_attempt` records and the `restriction_input`/`restriction_output`
winner routes are described in [phase records](legacy-phase-records.md).

## Synthesis size

`synthesis-size.py` invokes each labelled binary with `-s`, then checks its
controller separately. `--extra-args MEMBER='ARGS'` shell-splits solver arguments
for one label, for example `--extra-args candidate='--aiger-mode both+ud'`.
A binary can have several labels, each measured and verified independently.
Unknown/repeated labels and malformed quoting are errors; omitted labels get no
extra arguments. The runner never invokes a shell or passes these arguments to
the checker. `run.json` keeps supplied entries and parsed `solver_args`.
Acacia rejects unsafe `+dc` modes (#466); see the
[CLI guide](../docs/cli.md#write-a-controller). Resolve members through the registry:

```bash
# Activate the environment for your installed Spot and C++ toolchain first.
export TMPDIR="$PWD/build_scratch/synthesis-size/tmp"
export BENCHMARK_COMPILE_JOBS=1
mkdir -p "$TMPDIR"
mapfile -t members < <(python3 scripts/acacia-config.py list-group docker_default)
bins=()
for member in "${members[@]}"; do
  systemd-run --user --scope -p MemoryMax=5G -p MemorySwapMax=0 \
    ./self-benchmark.sh -R -c "$member"
  bins+=(--bin "$member=$PWD/build_${member}/src/acacia-bonsai")
done
checker="build_${members[0]}/tests/controller-check"
systemd-run --user --scope -p MemoryMax=5G -p MemorySwapMax=0 \
  meson test -C "build_${members[0]}" --no-rebuild --num-processes 1 controller-check
systemd-run --user --scope -p MemoryMax=5G -p MemorySwapMax=0 \
  python3 -s benchmarking/synthesis-size.py "${bins[@]}" --checker "$checker" \
  --list tests/suites/benchmarks/syntcomp26/all.list \
  --tlsf-map tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv \
  --tlsf-corpus /path/to/materialized-corpus --timeout 17 --verify-timeout 60 \
  --verify-max-states 200000 \
  --systemd-scope --memory-max 5G --memory-swap-max 0 \
  --output _bm-logs/synthesis-size
```

A smaller panel can use the same logical IDs. Selection uses TLSF status and
reviewed corrections. `--expected-status` defaults to `REALIZABLE`, also accepting
`UNREALIZABLE`, `UNKNOWN` or `all`; `--status-exceptions` replaces the default table.
Runs are serial, with a fresh directory and no resume/overwrite.

Keep circuits, stdout/stderr and `run.json` as evidence. `rows.tsv` records source
expectation, verdict, bounded synthesis time/exit/failure, AIGER `M/I/L/O/A`, optional
`REF_SIZE`, and separate verification status/time/exit. A printed answer followed
by crash/timeout is unsuccessful. Malformed and unsupported circuits cannot count
as verified. Summary size statistics use verified controllers; comparisons need
the intersection of verified input IDs across every supplied member.
`total_ands_latches` and `median_ands_latches` count A + L only on that intersection
and stay empty when it is empty. Consider controller coverage alongside size.

The independent `tests/controller-check` keeps TLSF source TARGET and strict
semantics, requires the declared I/O names, and explores reachable latches from
zero using BDD edges. It checks Moore independence and empty intersection with a
fresh negated-objective translation. It trusts Spot's parser/BDDs/translation and
the shared TLSF conversion, without trusting solver verdicts or proofs.
`--verify-max-states` defaults to 200000; exceeding it, explicit AIGER 1.9 reset
fields or parser limitations return unsupported, never verified. Timeouts remain
inconclusive. See `--help` for output columns and checker limits.

## Memory collection

On Linux, `benchlib.run_systemd_scope` creates one delegated scope per invocation.
An owner outside the workload's memory limit holds its cgroup until the driver
reads `memory.peak`/`memory.events` and acknowledges. The workload must be empty
before and after reads. Owner/drain errors invalidate both observations and retain
their reason. Group OOM is off by default; CPU properties bound the whole scope.

`scope_memory_peak_bytes` is the concurrent workload peak, including children.
`max_process_rss_bytes` is Linux `wait4.ru_maxrss`; unreaped descendants may be
absent. Linux coverage records CPU by default; `--no-collect-rusage` disables CPU
only. Elsewhere collection defaults off and `--collect-rusage` uses GNU time.
Resume must match the original collection mode. Missing, unsupported or failed
collection stays unknown; process RSS and virtual reservations cannot replace a
scope peak. Use G3 instance mode for per-invocation memory evidence.

Native coverage and `run-subset.py` write `FILE.memory.tsv` sidecars. Joins report
coverage and missing reasons separately for scope peak and process RSS; numeric
values with invalid provenance are excluded. Cohort medians require 100% coverage,
including nonexecuted conversions in the denominator. Maxima cover the observed
subset only. Disclose outcome-based selection.

Validate the collector outside the sandbox on a host with delegated cgroup v2
memory, `memory.peak`, `cgroup.kill` and a user systemd manager:

```sh
set -o pipefail
mkdir -p _bm-logs.p0-memory-validation
python3 -u tests/check-scope-memory.py \
  | tee _bm-logs.p0-memory-validation/lifecycle.jsonl
```

This checks normal exit, timeout, child OOM and invocation OOM at 64 MiB/no swap.
The fake panel is `tests/pytest/test_scope_memory.py`.

## Corpus and panel selection

`syntcomp-corpus.py materialize` reconstructs and verifies the flat TLSF corpus
from `tests/syntcomp-benchmarks`, recording `.acacia-tlsf-corpus-path` unless
`--no-record` is set. Lookup precedence is `--tlsf-corpus DIR`,
`ACACIA_TLSF_CORPUS=DIR`, build option `-Dacacia_tlsf_corpus_dir=DIR`, then a valid
recorded pointer with its `.acacia-tlsf-corpus` marker. G1 has no CLI corpus flag.
Configure Meson with the directory to enumerate suites. Lists/maps fix logical
identity. A stratified panel is only a screen.

## Gates

- G0, correctness: `meson test -C build --suite=unit` and
  `meson test -C subprojects/posets/build`; both must report `Fail: 0`.
  First configure and compile the standalone Posets test build:
  `meson setup subprojects/posets/build subprojects/posets`, then
  `CARGO_BUILD_JOBS=2 meson compile -C subprojects/posets/build -j 2`.
- G1, frozen verdicts and coverage:
  `benchmarking/regression-gate.sh --baseline-bin PREVIOUS-BIN build`.
  `--baseline-bin` or `REGRESSION_BASELINE_BIN` is required, with no default.
  Use the same configuration built from the previous revision. All 40
  sentinels must pass and the script must print `GATE PASS`; verdict changes
  and coverage losses fail. Its baseline CSV uses frozen verdicts and
  `baseline_seconds` from `tests/suites/benchmarks/regress-expected.tsv`.
  The binary serves near-cap remeasurement only.
- G2, Posets proxy: `benchmarking/posets-microbench.sh` (advisory).
- G2s, solver-profile proxy:
  `benchmarking/solver-profile-gate.sh --rule optimization BASELINE-BIN CANDIDATE-BIN`.
  Select the rule before measuring: `optimization` applies the existing 5% improvement
  proxy (or the existing single-target pass to G3); `membership-default` requires
  no target beyond the same 6% regression ceiling and passes to G3 without a speedup
  requirement. Both record the rule and thresholds in `g2s-rule.txt` and `decision.txt`.
  The SYNTCOMP25 `mixed` target is `evasion0.ltl`.
- G3, landing bar: `benchmarking/landing-campaign.sh` compares paired
  binaries, suite lists and a 17 s timeout using `landing-bar.py`; every suite
  must print `GATE PASS`. `--scope-mode instance` keeps the runner outside
  each solver's 8 GiB scope so solver OOM is recorded. The default `campaign`
  mode shares the scope. A mode change needs a fresh output directory and
  matched remeasurement of both binaries.
- G4, corpus correctness:
  `python3 benchmarking/corpus-correctness-gate.py build` runs Meson's
  realizable/unrealizable suites and reads their JSON records. Timeouts are allowed;
  failed tests, false-verdict markers (including in timed-out tests), missing records
  and unexplained launcher exits fail. Meson's exit status alone is insufficient.
- G5, native TLSF parity: use the complete command below with a materialized
  TLSF corpus. SyFCo remains the independent converter wherever it succeeds, and only
  its rejections use the existing tlsf-tools inspector's already-Mealy pair without
  further adaptation. `conversion.tsv` and parity rows record each converter and
  SyFCo error, and the summary names every fallback with its reason as not
  independently cross-checked; these pairs still undergo verdict parity and need
  no manual waiver. Preparation or spot-check failures, missing conversions,
  frontend errors and opposite verdicts still fail the gate; normalization timeouts
  remain UNKNOWN, and available pairs still run after preparation failures.

For the 2025 selection (`build` must include the test helpers):

```sh
python3 benchmarking/tlsf-verdict-parity.py --prepare \
  --bin build/src/acacia-bonsai \
  --source _bm-logs/tlsf-corpus --converted _bm-logs/g5-pairs \
  --list tests/suites/benchmarks/syntcomp25/all.list --expect-count 1579 \
  --native-inspect build/tests/tlsf-frontend-inspect \
  --canonicalizer build/tests/ltl-formula-canonicalize \
  --csv _bm-logs/g5.csv --summary _bm-logs/g5-summary.txt \
  --status _bm-logs/g5-status.txt
```

For the 2026 selection, use its `all.list` and `--expect-count 1524`.

Use [baselines.tsv](baselines.tsv) to restore the same configuration from the
previous revision. Substituting a different configuration invalidates change
attribution. G1 still rejects verdict flips, row-set mismatches and lost solves.

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
coverage. Report censored higher-cap rows as derived, with their source
and transformation recorded.

Read PAR-2-only changes against the measured same-configuration noise floor.
Three baseline runs spanned 2778.691 to 2799.825 s on SYNTCOMP25 (21.134 s)
and 1702.186 to 1714.260 s on SYNTCOMP26 (12.074 s). A change inside that
spread alone is not performance evidence; coverage changes and per-instance
losses remain gate evidence.

## Diagnostic tools

[Phase records](legacy-phase-records.md) covers recording, profiling, completeness
and cap comparison. `legacy-phase-table.py` also exposes `completeness` and
`weakening`; `make-panel.py` selects deterministic panels.
Optional Posets capture/replay: `python3 benchmarking/posets-capture.py --help`
and `python3 benchmarking/posets-matrix.py --help`; the detailed method is in
docs-20261122 at `benchmarking/posets-matrix.md`.

Optional `tlsfinfo`/`tlsf2ltl` test helpers use `ACACIA_TLSF_TOOLS_BUILD`
(default `build/subprojects/tlsf-tools`); they are ordinary Meson targets.
Retired tools and tests are in campaign archives; restoring them preserves the
campaign's original partial/stopped status.

## Decisions

- Family/name-based dispatch and sequential Python lifting are withdrawn. Use
  source-bound generic native arms; `gr1-par2-20260923` remains a partial campaign
  (`param-lift-20260922`, `gr1-par2-20260923`).
- The earlier obfuscated corpus is not a blind holdout: its mapping was published.
  Use genericity/renaming checks (`opt20260927-p4close`).
- Lifting uses fixed work budgets, with invocation deadlines as hard stops.
  Disclose outcome-informed tuning and holdout limits (`opt20260927-p1b6`).
- Existing evidence does not justify TLSF normalization, a feature-based arm
  selector or an isl-backed downset (`route-20261019`, `costmodel-20261020`,
  `islprobe-20261020`).

The former decision table, including superseded policies, is archived in
docs-20261122. Current solver policies are in [solver contracts](DATA-STRUCTURES.md).
