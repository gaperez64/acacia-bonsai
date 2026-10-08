# Coverage-first P0: gates and memory

Implemented in order: #211, then #214. Source baseline is Acacia `d9d3fd43`
and tlsf-tools `404963544436013d11ca232f4721d6da4499681f`. No solver dispatch,
threshold tuning, benchmark exceptions, staging or commits. The shipping configuration
was not changed. Live issue/PR refresh and measurements were excluded by the offline
charter; other worktrees and frozen binaries were preserved.

#211 corrects G4 record classification, independent G5 preparation/spot-check/parity
outcomes, one-time target adaptation of SyFCo's plain AST, and declared G2s rules.
SyFCo overwrite-target alone does not adapt its raw formula; overwrite-semantics can
produce unsupported strong-next. The existing Spot helper applies the structural AP
delay once to the SyFCo side. Native output is already adapted. The independent
normalizer remains conservative: timeout UNKNOWN and unequal keys FAIL; no equivalence
is inferred from a failed comparison. Missing conversions and unresolved checks block
G5 without skipping available pairs. Exceptions require separate reviewed evidence
metadata; none were introduced. G2s retains 5% and 6% defaults and its existing
single-target optimization proxy; membership/default checks use the same regression
ceiling without requiring an optimization win.

#214 retains the shared benchlib runner and moves only its lifecycle/collection work
outside the workload limit. The external driver acknowledges `memory.peak` and
`memory.events` before the owner deletes the fresh invocation cgroup. Timeout cleanup
uses the same retained lifetime. OOM group killing is opt-in, preserving incumbent
child-OOM behavior. Sources, missing reasons, cgroup and OOM setting are recorded;
RSS uses wait4 by default and stays distinct from concurrent memory.peak. RSS covers
the reaped tree, so unreaped descendants may be absent. Under-covered medians are
suppressed with numerator/denominator, missing reasons and selection-bias disclosure.
The 100% completeness rule is global and was selected before any measurements.
The scope layout/observer overhead needs fresh matched timing, not recycled campaigns.

The implementation checks are complete. #211 has all four regression tests; #214's
closure evidence is still partial until the driver runs the real Linux panel in
[the protocol](README.md#memory-collection). Fakes test normal, timeout, child OOM,
invocation OOM, fresh scopes, collection before deletion, missing/sentinel files,
non-Linux fields, journal protection, and biased report inputs. No `systemd-run` or
benchmark campaign was run in the sandbox.

## Reviewer follow-up

All four findings have regressions and are corrected. Parent journal peaks and swap
peaks use separate `parent_scope_memory_*` fields. The annotation CLI carries the
input memory sidecar into its output, preserving collector failures. Statistics and
paired memory gates exclude explicit missing reasons and missing/invalid collection
provenance, even when a numeric value is present. The failed-delegation regression
remains 0/1 with its original missing reason and no median after journal annotation.

Primary coverage, portfolio-pair and raw-subset columns match `d9d3fd43` exactly.
Additional provenance is fsync'd to `FILE.memory.tsv` and joined by observation
identity, binary, scope and measurements. Regression tests obtain the pinned master
reader with read-only `git show` and feed it new output. Resuming expanded files saves
original bytes in `*-legacy.tsv`; sidecar round trips preserve provenance without
rerunning completed rows. Recycled and annotated rows also carry their sidecars;
stale sidecars cannot supply provenance for different observations.

The owner publishes `os.waitstatus_to_exitcode` through its final state. The observer
restores that decoded workload status in `RunResult`, preserving timeout precedence.
SIGTERM is recorded as CRASH with `signal:15`; an ordinary exit 143 remains ERROR.
OOM classification retains master's nonzero-exit rule, including UNREALIZABLE/exit 1
with OOM kills. Exit zero with child OOM and timeouts retain their previous treatment.

The second re-review fixes owner errors masked by a published cgroup path. Failed
drain, cgroup setup, fork, started-state publication, wait4, cancellation and kill
failures now withhold peak/events and retain the exact owner error as both missing
reasons. Readable numeric files cannot make these rows complete. The reader checks
`cgroup.events` before and after reading peak/events; a populated, missing, malformed
or disappearing drain state excludes both observations. The owner uses the same
populated-state validation instead of accepting absent/invalid state as empty.
Valid wait4 RSS remains independently available; per-file peak/events read failures
continue to carry their own reasons without erasing the other valid statistic.

Twenty new regressions require 0/1 scope coverage, preserved failure reasons and no
median or maximum. The failed-drain case retains `populated 1` alongside a positive
raw peak. Other cases cover owner errors with an empty readable cgroup, population
before/after reading, and unreadable/invalid drain state. All twenty fail against a
scratch reconstruction of the previous collector and pass after the fix. The landing
campaign's fake scope now publishes its empty state explicitly.

## File ownership

| Issue | Files |
|---|---|
| #211 | `benchmarking/corpus-correctness-gate.py`, `benchmarking/check-tlsf-conversion.py`, `benchmarking/convert-tlsf-corpus.py`, `benchmarking/tlsf_pairs.py`, `benchmarking/tlsf-verdict-parity.py`, `benchmarking/solver-profile-gate.sh`, `benchmarking/solver-profile-score.py`, `tests/ltl_formula_canonicalize.cc`, `tests/pytest/test_p0_gates.py` |
| #214 | `benchmarking/scope_memory.py`, `benchmarking/benchlib.py`, `benchmarking/run-syntcomp26-coverage.py`, `benchmarking/run-subset.py`, `benchmarking/run-portfolio-pairs.py`, `benchmarking/tools/threeway-join.py`, `benchmarking/tools/recycle-cap.py`, `benchmarking/paired-admission.py`, `benchmarking/compare-backend-race.py`, `benchmarking/annotate-scope-results.py`, `tests/check-scope-memory.py`, `tests/pytest/test_scope_memory.py`, `tests/pytest/test_scope_accounting.py`, `tests/pytest/test_coverage_runner.py`, `tests/pytest/test_run_subset.py`, `tests/pytest/test_p4_tools.py`, `tests/pytest/test_paired_admission.py`, `tests/pytest/test_backend_comparison.py`, `tests/pytest/conftest.py`, `tests/pytest/test_benchlib_scopes.py`, `tests/pytest/test_portfolio_pairs.py`, `tests/pytest/test_landing_campaign.py` |
| Shared documentation | `benchmarking/README.md`, `benchmarking/RESULTS.md`, `benchmarking/coverage-first/p0-gates-memory.md` |

## Reproduction

Use the repository Python tools under `/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/`.
The follow-up checked build used a source snapshot under
`build_scratch/p0-drain-followup/source`, with the local cached yyjson copied into that
snapshot. It was configured with `PKG_CONFIG_PATH=/usr/local/lib/pkgconfig`,
`--buildtype=debugoptimized --wrap-mode=nodownload -Dacacia_enable_tlsf_frontend=true
-Dbuild_tests=true`, and compiled with `-j 4`. A local cached yyjson source was used;
no dependency source was edited. Large scratch build objects are removed at handback.

```sh
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
meson setup build_scratch/p0-drain-followup/checked build_scratch/p0-drain-followup/source \
  --wrap-mode=nodownload \
  --buildtype=debugoptimized -Dacacia_enable_tlsf_frontend=true -Dbuild_tests=true
meson compile -C build_scratch/p0-drain-followup/checked -j 4
meson test -C build_scratch/p0-drain-followup/checked --suite unit --num-processes 4
ACACIA_GATE_TEST_BUILD=build_scratch/p0-drain-followup/checked \
/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/pytest -q \
  --basetemp=build_scratch/p0-drain-followup/pytest-final \
  tests/pytest/test_{p0_gates,tlsf_verdict_parity,solver_profile_score,solver_profile_targets,scope_memory,benchlib_scopes,benchlib_classify,coverage_runner,run_subset,portfolio_pairs,p4_tools,paired_admission,backend_comparison,scope_accounting,no_benchmark_hardcoding,landing_campaign,run_diag_targets,run_diag_targets_tlsf,censor_to_cap,cactus_report,landing_bar,spot_demand_campaign,small_invariant_campaign,run_witness_sprint}.py
```

Run ruff on the Python files in the ownership table. No Meson options changed.
The real-cgroup reproduction command is in the protocol and runs the same existing
runner. A failed real panel blocks #214 closure; do not relabel it as supported or
substitute legacy journal/virtual-memory observations for missing invocation peaks.

## Validation results

- `meson test -C build_scratch/p0-drain-followup/checked --suite unit --num-processes 4`:
  52 passed, 0 failed.
- The 24-suite pytest command above: 719 passed, 3 skipped. Skips are one optional
  corpus check and two optional matplotlib plot checks. All new gate/lifecycle and
  reviewer regressions ran, including the pinned master readers.
- The 20 second-review regressions: passed; all 20 fail against the previous
  collector reconstructed in scratch (expected failures).
- Ruff on all 29 touched Python files: passed. `bash -n benchmarking/solver-profile-gate.sh`:
  passed. `git diff --check --ignore-submodules=all`: passed.
- Real cgroups and timing campaigns: not run, as required by the charter. The
  external real-cgroup panel remains necessary before claiming #214 closure.
