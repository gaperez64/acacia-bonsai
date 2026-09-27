# Stage A review remediation (2026-09-24)

This follow-up addresses findings 2–4 of `build_scratch/review-stageA/REPORT.md`
inside acacia-bonsai. It does not change tlsf-tools or finding 1's ownership ABI.
No file was staged or committed.

## Finding 2: signal renaming

`obfuscate-tlsf.py` now reads both `name[extent]` and `TYPE name`
declarations (and declarations without a trailing semicolon). The sidecar is
checked against every declared signal before it is written and again by the
corpus verifier. Scalar, indexed-bus, enum-bus, and parameter-preservation
fixtures pass. The whole-corpus `tlsfinfo`/`tlsf2ltl` rerun produced **1,524
equal, 0 mismatches, 0 errors**. See `obfuscation-invariance-summary.md` and
`obfuscation-invariance.tsv`.

## Finding 3: hardcoding guard and metamorphic checks

The guard folds constant string expressions, follows input-path aliases into
basename and table-lookup operations, rejects source-hash/signature verdict
registries, and scans the invoked monitor and native solver/checker files for
family labels and corpus paths. Regression mutants cover the reviewer's
concatenated-basename, SHA-256 table, and input/output-arity table examples.

Metamorphic tests cover randomized basenames alone, seeded signal renaming,
monitor/latch-looking names, and three generated unseen small GR(1) specs.
The generated specs all reached certified REALIZABLE answers on both sides.
Two strict xfails preserve known tlsf-tools failures:

- `controllable_a` input: pending tlsf-tools injective ownership encoding.
- `monitor_0_state_0` input: pending tlsf-tools disjoint monitor/latch symbols.
  The generated AIG has both `i0 monitor_0_state_0` and
  `l0 monitor_0_state_0`; the original certificate check declines while the
  alpha-renamed case verifies.

A self-checked `LD_PRELOAD` trace of a verified direct answer and a fallback
case captured **742 file-open attempts in 12 processes**, with **zero opens**
of corpus, historical table, or registry paths. Raw trace, interception source,
and summary are in `build_scratch/stageA/file-open-trace*`.

## Finding 4: eligibility admission

The 11.333333 s run is labeled **reducibility under the lift slice**:
572/1,524 inputs. Serial remeasurement under the wrapper's unchanged
eligibility gate admitted **527 at 17 s (0.85 s gate)** and **534 at 60 s
(1.0 s gate)**. The 60 s set adds seven inputs and contains the 17 s set.
Exact admitted ID lists, per-input statuses, times, and decline reasons are in
`generic-admission-17.list`, `generic-admission-60.list`, and
`generic-admission.tsv`; see `generic-census-summary.md`.

## Validation

- Full `tests/pytest` collection attempted; the environment lacks the
  `acacia_boomslang` module for the Python 3.13 interpreter, so two modules
  fail collection. Its Spot binding also reports an unresolved symbol and
  makes one import-dependent test skip. The available suite passed:
  **935 passed, 4 skipped, 2 xfailed, 8 subtests passed**.
- Dated lifting suites, run serially: **66 passed, 3 skipped,
  41 subtests passed** in 703.31 s.
- Final obfuscation test rerun after tightening assertions: **8 passed,
  2 xfailed**.
- Ruff and `git diff --check`: passed.

## VERDICT

**REJECT** pending the separate tlsf-tools ownership fix (finding 1) and
monitor/latch symbol collision fix. Findings 2 and 4 are remediated; the
finding 3 guard and tests now expose both known name-dependent failures.
