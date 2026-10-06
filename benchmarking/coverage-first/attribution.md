# P0 attribution (#210)

This package adds live worker and native route events to `ACACIA_PHASE_RECORDS`.
It changes no portfolio membership, ordering, solver caps, proof checks, or deadline
acceptance rule. With the environment variable unset there is no writer, shared
worker snapshot, event formatting, or native observer callback. The tlsf-tools
worktree remains based on `404963544436013d11ca232f4721d6da4499681f`; its uncommitted
changes must accompany the Acacia changes. Existing public C struct layouts stay unchanged; the new observer carries a typed failure status.

## P1 runtime ablation states

`--r-prepass on|off` and `--equivariance on|off` preserve incumbent defaults.
Each worker lifecycle event includes boolean `r_prepass` and `equivariance`, also
retained by parent spawn/terminal/winner records after child cancellation.
These are invocation settings, independent of whether a worker reaches the hook.
The native worker now calls `tlsf_gr1_both_from_target_v2` with versioned routing
options; its observer and the legacy v1 entry point remain supported. R-off with
U-off skips seed and candidate work inside the same combined context and retains
its direct solver settings. Backward decision workers emit `equivariance` before
recognition when enabled and `backward` on ordinary solving after decline or with
it disabled. Entry into recognition does not imply an admitted symmetry.
See [the matched ablation protocol](p1-ablation-protocol.md) for all four legs,
resource controls and the separate backward-worker membership comparison.

## Reading events

Each invocation needs a fresh, existing diagnostic directory. Files are named for
**emitter** PIDs. `worker_pid` and `worker` (launch index) identify the solver worker;
parent events reside in the parent's file. `worker_spec` preserves its requested
kind, polarity, translation preference, transform, and provider. Event `mono_ns`
is the observation's monotonic clock; `seq` orders a producer's observations.
A parent's spawn sequence is independent of the child's sequence.

`worker_start`, `route_selected`, `route_start`, `decline`, `terminal_result`,
`parent_terminal`, and `parent_winner` describe the lifecycle. `parent_winner` is
emitted only at the parent's existing acceptance boundary, after its deadline and
interrupt checks. Exit codes keep their existing meanings (0 REAL, 1 UNREAL,
2 UNKNOWN, 3 ERROR). A signalled child has exit_code -1 and an explicit signal.
Deadline-rejected answers have no parent_winner, even if their exit code was 0/1.
Sibling cleanup records `winner_cancelled`; external interrupts record `interrupted`.

`requested_backend` never follows coercion or selection. `effective_backend` does.
`original_polarity` refers to the original specification, while `proof_polarity`
refers to the game being proved. A legacy UNREAL worker proves REAL on its transformed
game. A combined native worker initially has original polarity `both` and unknown
proof polarity; once its exact proof side is known the fields become REAL/REAL or
UNREAL/UNREAL. Its requested polarity stays `both` in worker_spec. Seed polarity
is a separate completed-call statistic and never establishes a target verdict.

Native combined routes are `seed_discovery`, `R`, `U`, and `direct`. Selection and
start come from `tlsf_gr1_both_from_target_v1` **before** their work executes.
`verification_start` precedes checking, `check_complete` means the independent
checker succeeded, and `verification_complete` follows Acacia's artifact/source
binding. None of these intermediate events is a parent-accepted verdict.
`route_stopped` records resource, deadline, cancellation, and error outcomes separately
from algorithmic/applicability declines. Its `reason` is the typed category
`resource`, `deadline`, `cancelled`, or `error`; `stage` keeps the obstruction code.
Caught failure status drives classification, including failures before checking.
Unclassified incumbent false/UNKNOWN results use STOPPED/inconclusive. Finite K-bound
exhaustion uses STOPPED/resource; neither creates an applicability decline.
Checker causes travel separately from the legacy return status so fallback and
incumbent API results remain unchanged. The native diagnostic adapter accepts the
same categories for preparation/reduction failures and failures without an observer.
Decline reasons remain bounded obstruction stage codes; the incumbent native
stderr diagnostics retain the corresponding human-readable messages.

## Completeness and reuse

`parent_terminal.telemetry` reports **producer** completeness: `complete` means a
child emitted its terminal event and reported no drops; `dropped` reports a
nonzero child drop count; `incomplete` means it never emitted its final event;
`unavailable` means shared metadata could not be allocated. The parent reads its
small shared snapshot only after reaping, so even a killed-before-start worker
has its PID and requested context in parent metadata. Its last route/stage stays
explicitly unknown when it was not observed. Missing packets never mean decline.

Delivery needs separate validation. Require a `writer_summary` with zero
`failed_records` and `incomplete_packet:false`, zero producer drop counts (including
the parent's record_summary), and the expected worker starts/specs, parent terminals,
and accepted winner. A missing writer summary, malformed/truncated JSON, sequence
gaps, missing events, or destination failures make delivery incomplete. A slow,
full, broken, or killed writer never changes the solver verdict; an unusable
destination cannot itself carry a reliable loss report. Never turn its silence
into an algorithmic obstruction. Ignore incomplete trailing lines, flag them,
and keep the confirmed prefix.

Audit an existing coverage-runner campaign without executing the solver:

```sh
python3 benchmarking/attribution-completeness.py RUNS.tsv \
  --phase-records-dir PHASES \
  --output OUTPUT/rows.tsv --report OUTPUT/completeness.md --summary OUTPUT/summary.json
```

The reader reuses `summarize-worker-phases.py` for TSV validation, numeric parsing,
and report tables, and `summarize-diag-phases.py` for any captured checkpoints.
It joins every invocation by the runner's sanitized label/cap/instance/run-index
layout, rather than merging repeated instances or selecting the largest cap.
Inventories use explicit `--arms` or the current preset registry when available;
otherwise the reader checks all observed identities and contiguous launch indices.
The expected inventory is retained in `expected_worker_specs` in each output row;
worker kind, requested polarity, translation, transform and provider are checked
against the corresponding launch index, together with requested backend and
runtime settings. Requested context is independent of effective backend coercion
and the combined worker's later proof polarity. Both `r_prepass` and `equivariance`
are mandatory JSON booleans in every lifecycle and worker-spec record; missing,
invalid or conflicting values make delivery incomplete.

Only regular JSONL destinations are read. `lstat`, a nonblocking open without
following symlinks, and a descriptor check reject FIFOs, symlinks, devices and
files replaced during inspection. A global 64 MiB per-file limit also bounds reads
when a file grows after inspection. Rejected destinations have explicit delivery
reasons; the reader continues auditing the other confirmed records. This is a
reader resource bound, independent of solver thresholds or campaign outcomes.
The registry is an inventory check, not proof that an older binary has the same
configuration. Each row carries separate delivery/producer states, all reasons
with path:line evidence, accepted winners, route events, declines, and stops.
Parent terminal cancellation/deadline records remain distinct from child route
stops. Reason counts count rows once per reason and may overlap.

Producer incompleteness from a documented kill is retained even when delivery of
the observed prefix and parent metadata is complete. A worker known only from
parent metadata remains in the inventory with an unknown last route; its missing
start is flagged under the delivery rule above. Selection without start at the
end of a killed worker's contiguous prefix can precede the kill, so it does not
by itself prove a missing delivered event. The pre-fork parent spawn sequence is
independent of the child; the parent terminal sequence resumes the reaped shared
snapshot, followed by the winner sequence. Large jumps between spawn and terminal
in the parent file are therefore not delivery gaps.

For cap recycling, additionally require the same binary/configuration/input and
resource regime, an accepted winner before the smaller cap, and compatible route
choices and route budgets. Complete attribution is necessary, not sufficient to
establish cap independence. A result rejected by the parent cannot be recycled as
a solve. This package does not run or claim a benchmark or recycling campaign.

## Correctness checks

`check-attribution.py` uses the existing small native CLI fixtures: repeated
liveness (R), anchored recurrence (U), and plain liveness (direct). It compares
records on/off verdicts, verifies full single-worker lifecycles, interrupts each
native route and checking by timeout, SIGKILL, and parent SIGTERM, exercises a
late answer, a sibling winner, raw LTL, synthesis backend coercion, and refusal
to reuse incomplete attribution.
`native_attribution_api.cc` checks event order, typed cancellation before/during
checking, deadline stops, and invalid-argument errors, with no published candidate.
The tlsf-tools `native_both_api` assertions cover R schema/checker capacity and U
memory, candidate allowance, schema capacity, and checker capacity: each emits
STOPPED with LIMIT, no decline for that route, followed by a verified direct fallback.
The worker panel checks a generated 2,050-element preparation-budget failure through
all native arm kinds, R capacity fallback, and a proof-binding error.
`check-record-transport.py` checks 512-byte packets, a full pipe that recovers,
blocked writers, FIFO/broken destinations, and file-size exhaustion. Existing
native proof-binding and portfolio deadline tests remain required. Hook literal
reads live in test-only translation units/headers under `tests/`; test executables
supply writer controls through function parameters. The production target is scanned
for native, portfolio, transport, and tlsf-tools mutation hooks in every build type.
The owner hardcoding guard is byte-identical to master (`d9d3fd43`), with no new exemptions.

Build reproduction (local dependencies must already be available):

```
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
meson setup build_scratch/p0-attr --wrap-mode=nodownload --buildtype=debugoptimized \
  -Dacacia_native_arms=true -Dacacia_spot_guarded_backend=true \
  -Dacacia_forward_safety_solver=true \
  -Dacacia_default_arms=real:small:backward,real:small:forward,unreal:formula:spot-guarded-sparse,unreal:automaton:forward,both:gr1-real-lift:oxidd
meson compile -C build_scratch/p0-attr -j 4
meson test -C build_scratch/p0-attr --suite unit --num-processes 4
```

No structural thresholds were added or tuned. No staging, commits, source pin
updates, worktree cleanup, or benchmark measurements are part of this package.
The initial Meson setup attempted an automatic dependency download; it was stopped
without downloading files. Subsequent setup used `--wrap-mode=nodownload`. OxiDD archive/headers and yyjson are reused from the existing local
checkout, with the archive validated against its existing build stamp.

Validation after independent review on 6 October 2026: the owner guard is restored
byte-for-byte from `git show d9d3fd43:tests/pytest/test_no_benchmark_hardcoding.py`.
The unmodified guard and generic lifting guard pass 111 tests with one optional-corpus
skip. The final rebuilt binary passes all 62 unit tests. Ruff, config-frontends,
clang-format 22.1.8 on follow-up C/C++ files, whitespace
checks, and the production-target mutation-hook scan pass. No production caps or
thresholds changed. U checker capacity is exercised with a test-only artifact-byte
budget so the public checker returns a typed LIMIT; R uses its existing checker-limit
fault. Both assertions require STOPPED, no route decline, and verified direct fallback.

All 179 enabled tlsf-tools functional tests pass, including the three native lift/both
API tests. The full integrated 181-test run exposed two existing test-context issues:
`source_layout` looks for standalone Meson introspection under the subproject build
root, where Meson does not generate it; `oxidd_build_stamp` expects Git revision
metadata, while this worktree links only OxiDD's already-built headers/archive.
Neither test was changed. Both pass using their required contexts: a standalone
nodownload tlsf-tools configuration for layout, and the actual existing OxiDD checkout
for the stamp test. The checkout and its archive are read only.

Reproduce the context-dependent checks after configuring the main build:

```
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
meson setup build_scratch/p0-attr/tlsf-layout subprojects/tlsf-tools \
  --wrap-mode=nodownload --buildtype=debugoptimized --default-library=static \
  -Dnative_gr1=enabled -Doxidd=enabled -Dresearch_tools=false
python3 subprojects/tlsf-tools/test/api/check_layout.py \
  subprojects/tlsf-tools build_scratch/p0-attr/tlsf-layout
python3 subprojects/tlsf-tools/test/api/test_oxidd_stamp.py \
  subprojects/tlsf-tools/scripts/oxidd_stamp.py \
  /home/gperez/GIT-repos/acacia-bonsai/subprojects/tlsf-tools/external/oxidd \
  build_scratch/p0-attr
python3 build_scratch/p0-attr/run-functional-tlsf.py
```

The scratch runner selects every enabled tlsf-tools test except the two unchanged
metadata checks exercised above. `followup-tlsf-functional.log`, `followup-layout.log`,
and `followup-stamp.log` preserve the results; the integrated-run failure log is retained
as `followup-tlsf.log`. Main-build checks and logs use the `followup-` prefix.
OxiDD stamp: commit `bc4354cbb86f3940bacc6675e4a0d2abb97ed8c4`, clean build,
archive SHA-256 `5853e796772106396967969e46c815df4731af4e1422629235561f63545123b2`.
Production Acacia binary SHA-256: `472d762d4348f02652c697eeb7ff3bccaf8842923d75ab9e7325cdf73b583ff6`.
Generated object files were removed after validation; tested binaries and logs remain.

## Second-review attribution follow-up

Zero TAA rank-node capacity now produces a resource stop at the candidate outcome,
in both only and fallback modes. The fallback still rebuilds and solves with the
incumbent backward backend; selecting that route clears the stopped candidate's
reason. Generic result handling preserves recorded typed reasons. Translation
exceptions produce resource/error stops. Native allocation, standard and unknown
exceptions preserve their typed terminal reason and emit no exception decline.

Binding corruption before routing or late in R, U and direct checking retains the
legacy DECLINED return, zero target checks and no published proof. Its observer
cause is ERROR. The versioned `tlsf_gr1_lift_from_target_v1` also exposes the diagnostic
cause separately for the parameter arm; existing entry points and public struct
layouts remain intact. Parser/artifact/seed integrity and checker failures retain
their diagnostic causes through recovered seed searches. Numeric rank capacity
uses LIMIT. The existing 256-level rank capacity and all solver/portfolio budgets
are unchanged; no threshold was added or tuned.

The third review found that the stage-name audit missed provider and U metadata
integrity failures. The current [cause census](decline-causes.md) is generated from
mandatory cause arguments in the code. It replaces that audit. Legacy return values
and statistic/phase names containing "decline" retain their incumbent meaning and
are independent of lifecycle classification.

Regressions exercise both candidate modes, three native exception types, initial
binding corruption and late binding corruption in R/U/direct, malformed U certificate
counts, U rank capacity, and typed causes from the versioned parameter lift entry
point. The direct binding fixture permits the earlier genuine seed-window decline;
its binding route must stop with ERROR, zero checks and no published proof.

Final follow-up validation: 62/62 unit tests and all 179 tlsf-tools functional tests
pass. The two unchanged integrated metadata-context failures remain reproducible;
both checks pass in their required standalone contexts. The unchanged owner and
generic guards pass 111 tests with one optional-corpus skip. Ruff, clang-format
22.1.8 on all 21 changed/new C/C++ files, config-frontends, whitespace checks and
installable-binary hook exclusion pass. Review2 large generated files and 181
scratch objects (682.7 MB) were removed; tested binaries and small evidence remain.

## Third-review structural follow-up

Every Spot provider `Declined` construction now requires an immutable applicability
or integrity cause. Integrity keeps the legacy DECLINED/UNKNOWN/fallback behavior
and emits STOPPED/error. Unsupported acceptance, alternation and PSL remain
applicability declines. The provider tests inspect complete serialized lifecycles,
including terminal UNKNOWN, for acceptance/AP mutation, undeclared sets, null
providers/iterators, foreign states, and resource faults.

Every native `Failure` and `decline()` construction now requires a `FailureCause`,
separate from its legacy return status. Native solver/checker mappings return that
cause type. R/U game, certificate, source and policy-sidecar consistency checks
carry ERROR; exact grammar mismatches between valid seeds and targets remain
applicability. Recovered seed/projection/summary searches retain non-applicability
causes if all alternatives fail. No return code, verdict, fallback policy, public
C struct layout, solver budget or threshold changed.

`native_failure_census` enumerates every existing numbered U/R/seed/binding fault
mode, checks decline/stop/success classification, and checks verified direct fallback
where expected. U faults 4 and 5 stop with ERROR before the verified direct fallback.
The pytest inventory compares those cases with the actual hook branches and checks
that the generated 275-origin cause census is current. The new provider assertions
and both U integrity assertions fail against the saved pre-fix sources.

The full pytest suite passes with bindings rebuilt from this worktree: 1,163 passed,
31 skipped, 23 subtests passed. All 62 unit tests pass. The owner hardcoding guard is
byte-identical to master, SHA-256
`c53228cb075e525d0f01b1f9c2fbfc400369ebbba828bbf5342a1d9322d03221`.
The new census adds one tlsf-tools functional test; the two existing integrated
metadata-context issues are still checked separately in their required contexts.
No staging, commits, network or timing/benchmark campaign was performed.

Third-round validation: all 180 enabled tlsf-tools functional tests pass, including
`native_failure_census`. The full integrated run retains the two unchanged metadata
context failures; both checks pass separately. Ruff passes on all seven changed/new
Python files, clang-format 22.1.8 passes on all 23 changed/new C/C++ files, configuration
frontends and whitespace checks pass, and the installable-binary hook scan passes.
Ignored evidence and the charter report are in `build_scratch/p0-attr-structural/`.
Generated objects/pre-fix executables (776,877,824 bytes) were removed after validation;
tested binaries, Python bindings, small logs and source snapshots remain.

## Fourth-review integrity follow-up

Preparation now has versioned `tlsf_gr1_lift_target_prepare_v1` and
`tlsf_gr1_lift_target_prepare_exact_v1` diagnostic outputs. All three Acacia lifting
callers consume the explicit cause. A failed strict retry keeps an earlier integrity
cause when its own obstruction is applicability; successful strict retry clears it.
Legacy return codes, error stages/messages and retries are preserved. The native
status adapter no longer guesses a cause from a stage whitelist.

Reduction now requires a diagnostic status at every `Failure` origin and exposes it
through `tlsf_gr1_reduce_v1`, without changing existing C structures. Completed Spot
monitor determinism/completeness/state-acceptance/sticky-region checks, transition
AP/Boolean checks and bound frontend signal/formula/provenance contracts report
ERROR independently of their legacy UNSUPPORTED result. Direct and lifting callers
consume that diagnostic output. All three trusted-seed solvers classify every game
validator rejection as integrity, preserving legacy DECLINED and verified fallback.

The audit also found CHECK_OK with INVALID/UNKNOWN and four U game/policy guards
(unnamed latch, absent bad predicate, nonsingleton justice and Skolem letter
support). They now stop with error. Checker classification uses status and verdict;
only completed CERT_FAILED/REFUTED checks decline. U fault hooks 9/10 violate a
policy interface contract and stop with ERROR; hook 8 remains a logical certificate
rejection and declines. Their legacy API outputs remain identical.

The [cause census](decline-causes.md) now enumerates 734 checks/origins across
preparation, reduction, seed solving, R, U, checking, OxiDD and Spot providers, with
an independent per-origin expected-cause assertion and an exhaustive hook inventory.
The runtime panels reach original validators with corrupted artifacts and inspect
serialized worker events. They include all numbered native hooks, all Acacia proof
hooks in each lifting arm, all game-validator checks in each seed solver, the four U
invariants, every checker status/verdict pair, all solver failure kinds and all
closure-provider FailureKind conversions. Source assertions protect unforced
branches; these results do not claim full branch coverage.

Final correctness: 63/63 root unit tests; 182 tlsf-tools functional tests, with the
same two integrated metadata-context failures as review4, both passing separately
in their required contexts; FULL pytest 1,900 passed, 31 skipped, 23 subtests passed.
The exact review4 diagnostics-off panel matches master and the saved prior binary
on 15/15 cases. All 72 legacy API status/verdict/proof/check-count/polarity/route
comparisons match prior evidence, including all 64 cases shared with master.
The charter and owner hardcoding guard remain byte-identical. Formatting, ruff,
configuration frontends and release-hook isolation pass. No options, thresholds,
budgets, worker order/membership, public C struct layouts or proof/fallback policy
changed. Evidence and the charter-format report are in
`build_scratch/p0-attr-final/`; no staging or commits.


## Timeout teardown follow-up

The parent already treats SIGTERM/SIGINT as orderly cancellation: its handler
only flags interruption and kills worker groups/PIDs; normal code reaps every
worker and emits parent terminals before the exit hook closes/drains the writer.
The handler now preserves errno across signal delivery. Diagnostics-off behavior,
worker order, proof checks and deadline acceptance remain the same.

The coverage runner repair belongs to `wt-p0-gates`: it requests parent-only TERM
at the cap and gives one global 500 ms cleanup window before invocation-wide
`cgroup.kill`, while retaining TIMEOUT and collecting peak/events before deletion.
See that worktree's `benchmarking/coverage-first/timeout-teardown.md` for the exact
lifecycle and fresh panel command. The existing writer's flush/reap bound is 110 ms;
the allowance also covers worker reaping and scheduling, without accepting a late
answer. Killed workers remain explicitly producer-incomplete, with complete parent
and writer delivery required separately.

`check-attribution.py` now checks five stalled workers under deadline, SIGTERM and
SIGINT, requiring all parent terminals, a parent summary, and a matching loss-free
writer summary. The same fixture checks run in pytest with
`ACACIA_ATTRIBUTION_TEST_BUILD=<checked-build>`; missing terminals or either summary
are rejected by negative oracle tests. Deadline handling is compared with diagnostics
disabled. The delayed-reaped-answer check now also requires complete cleanup delivery.
