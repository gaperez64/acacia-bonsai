# Phase records

Create a fresh directory and set `ACACIA_PHASE_RECORDS=DIRECTORY`, or use the
runner's `--phase-records-dir`. Recording needs no diagnostics build. Setup
failure warns once and solving continues with accounting off. Disabled recording
adds no clock, resource, formula-serialization or identity-hashing work.

The nonblocking pipe drops full-pipe packets immediately; framing respects runtime
`PIPE_BUF`. A writer opens the files. Sequence numbers and drops are worker-wide.
Observations leave solver choices, budgets and verification unchanged. Times
include instrumentation overhead; process `getrusage` RSS is a high-water mark,
separate from current RSS and concurrent cgroup peak.

Historical narrative, extended-only schema detail and the former profiling README
are archived in docs-20261122.

Record producers are in `src/phase_records.hh` and `src/solver/phase_observation.hh`;
`benchmarking/benchlib.py` reads them. Historical schemas are in docs-20261122.

## Lifecycle, causes and delivery

Join emitting PIDs to configured `worker_spec`; parent spawn/terminal sequences
are independent of child sequences. Requested backend and original polarity remain
separate from effective backend and transformed proof polarity. Every lifecycle
and worker-spec record carries boolean `r_prepass` and `equivariance`, including
bypassed hooks. Missing/conflicting settings make delivery incomplete.

Events are `worker_start`, `route_selected`, `route_start`, `decline`,
`terminal_result`, `parent_terminal`, `parent_winner`. Native routes are
`seed_discovery`, `R`, `U`, `direct`. `verification_start` precedes checking;
`check_complete` means checker success and `verification_complete` adds source
binding. Only `parent_winner` means acceptance after deadline/interruption checks.
Exit codes are 0 REAL, 1 UNREAL, 2 UNKNOWN, 3 ERROR; signals use exit_code -1.
`route_start:equivariance` precedes recognition; `route_start:backward` starts
ordinary solving after decline or with equivariance off.

Under `-s`, `parent_verdict` selects a proof and latches its verdict, before
controller construction. Each worker writes a private file beside the requested
destination. Only the parent renames a closed, successful REAL controller onto
that destination, after acceptance checks and competitor cleanup.
The grace/fallback scheme is Linux-only, like the optional reduction in #467.
Other platforms retain master's first-proof selection and extraction-failure
recovery without grace turns or memory admission.
`parent_extraction` records identify the worker and latched verdict (`exit_code`):
`grace_expired` means its turn ended (initially 250 ms, doubling on each successive
turn for that worker); `fallback` identifies the next worker resumed.
Ready proofs matching the latched verdict have first priority. Only if
none is admissible can an undecided worker capable of that verdict receive a turn
to finish its proof.
Each class retains a separate round-robin cursor when the other class runs.
It receives no export permission until publishing a matching verdict, recorded as
`parent_verdict:selected`; its extraction turn then starts. An opposite verdict
records `verdict_conflict` and discards the worker without export permission.
Workers capable only of the opposite verdict stay stopped. `no_alternative` keeps
the current worker running when neither class has an admissible alternative;
`scope_headroom` means a fallback's resumption
was withheld because scope memory was unreadable or lacked reserve. Stopped
workers remain charged in `memory.current`. A fallback's RSS budget is its
admission RSS plus growth allowance `max(64 MiB, admission RSS)`. Admission requires
headroom for three separate terms below the scope's last eighth: latent capacity
`max(0, VmData - RssAnon)`, COW exposure (conservatively admission RSS), and growth
allowance. The latent and COW terms cover faults in existing mappings separately
from new mappings. Unreadable RSS or anonymous
capacity blocks fallback admission. Before resumption,
`prlimit(RLIMIT_AS)` also caps its soft address-space limit at its current mapped
size plus **only the growth allowance**, preserving the hard limit and any tighter existing soft
limit. Allocation failure is an unsuccessful worker exit. `address_space_limit_failed`
refuses admission if the cap cannot be installed. ASan skips both the cap and latent
term with a diagnostic: writable shadow mappings inflate `VmData`. COW/growth
admission and the existing parent RSS supervisor remain active. The supervisor
enforces the budget; `rss_budget` marks termination of a fallback exceeding it.
Undecided workers undergo the same admission, address-space cap and RSS supervision
as fallback extractors, including sole-survivor cap restoration.
The first selected extractor never needs admission and keeps its original resource
policy. Before returning to it, fallbacks that have run are killed and reaped one
at a time, rechecking usage after each reclamation, as
`parent_terminal:original_headroom` if scope usage exceeds the snapshot at its first
suspension (or either reading is unavailable). If a fallback is the only surviving
worker, `sole_extractor` removes its additional RSS budget and restores its soft
address-space limit to the hard limit. Exits of suspended workers are reaped by the
ordinary supervisor too. Failed and reclaimed workers' partial files are removed
to release their file charges. Refused candidates do not block
later candidates. `verdict_conflict` rejects an opposite proof. `published` identifies
the worker whose controller was atomically installed. Parent observations copy
the worker snapshot and do not advance its live sequence. Slow extractors are
suspended, retained and revisited; contradictory proofs never receive permission
to export. Stop acknowledgement, discard reaping and tracked descendant reaping are polled without blocking
deadline or RSS checks; an exit racing suspension is accepted before any handoff.
After an extractor fails with no admissible replacement from either priority class,
`parent_terminal` with reason `extraction_irrelevant` identifies killed/reaped
unusable workers first. Admission is retried after reaping them, before reclaiming
matching undecided workers if still necessary. Each discard returns to supervision
before choosing another victim. Admission waits for pending leaders and descendants
to be reaped, and sole-survivor checks count them as live. If admission still fails, remaining
workers receive `extraction_failed` and the invocation finishes UNKNOWN (or ERROR
if a worker reported ERROR). Decision-only runs do not use this scheduling or file protocol.

DECLINE means inapplicability/unsupported input or completed logical certificate
rejection. Resource, deadline, cancellation, invalid input, integrity, internal
and operational failures are STOPPED with typed causes. Finite K exhaustion is
resource; unclassified false/UNKNOWN is inconclusive. Only CHECK_OK/CERT_FAILED
or REFUTED declines; CHECK_OK/INVALID or UNKNOWN is error. Causes are independent
of legacy return/fallback. Failed alternatives preserve earlier integrity causes;
successful recovery clears them. `pipeline_adapt_target` reports unsupported
adaptation as applicability, allocation failure as resource.

`obligation_start` observes the assigned child from the parent;
`obligation_eligibility` is parent-wide with worker/index -1. Completion comes from
the child normally, the parent on forced exits; `fork_failed` is a parent error.
These unsequenced records retain Spot trust/outcome/reason/times. `tlsf_parse`
identifies emitting process, role and local call count. `dpa_import` belongs to
the OxiDD parity worker. `parity_tier`, `parity_solved`, `parity_verdict` share the
child sequence and carry worker/tier/trust/time. Completeness checks validate
schemas, emitters and ordinary delivery; unknown events still undergo lifecycle
validation.

`restriction_attempt` records carry `candidate`, `family` (one-based eligible
input-family ordinal, zero for combined/constant candidates), accepted `polarity`,
`outcome`, `cause`, `mono_ns`, `wall_ns`, child PID and wall/memory limits.
Start records precede forking; `outcome=accepted` requires a reaped, in-budget
accepted exit. `restriction_input` winners prove UNREAL of the positive restricted
objective; `restriction_output` winners prove REAL. Declines use
`restriction_opposite`, `restriction_inconclusive` or `restriction_no_candidate`.
Deadline, resource and operational errors use the corresponding stop cause;
the original arm then resumes. A killed arm may leave only a start
record; parent terminals and descendant cleanup retain cancellation/resource
evidence. These attempts are independent of safety-core weakening.

Read shared terminal snapshots after reaping. `parent_terminal.telemetry` is
`complete`, `dropped`, `incomplete` or `unavailable`. Delivery needs one writer
summary, zero failed/incomplete packets, matching packet totals, sequences,
requested workers and parent terminals. Truncated lines retain their confirmed
prefix and failure reason. Missing/dropped records never imply decline or zero
work. Writer failure changes observability, not verdicts. Readers reject symlinks
and nonregular files and cap each file at 64 MiB, recording rejection reasons.

Generate the per-origin cause census with `python3 tests/check-decline-causes.py`;
validate the saved output with `--check`, or select it with `--output FILE`.
[Independent expectations](../tests/attribution-cause-expectations.json) and
`test_attribution_fault_inventory.py` check causes. Source assertions do not
establish runtime branch coverage.

`benchmarking/weakening-census.py DIRECTORY --output census.tsv` requires joined
generation/start/terminal/proof IDs, bindings, producer prefixes, gap-free sequences,
terminal metadata, writer summaries and zero drops for exact counts. Otherwise
observed generation/cancellation counts are lower bounds. Parent cancellation can
close a prepass census even if fallback later dies. Known zero fallback budget or
deadline termination before fallback proves starvation in a complete census;
positive budget does not. Unknown budget and winner cancellation stay distinct.

## Guarded translation observations

`translation-event-univ` records `distinct_promises`, `acceptance_set_width` and
`simplified_promises`. Observation does not enable rewriting or establish solves.
`tests/event-univ-census-isolated.py` isolates each input. Count membership only
when status is `ok` and every expected component of all seven frames is present:
raw/simplified positive/negative, real-components, formula-unreal, automaton-unreal.
Discard a sole decomposition result. Partial, exhausted and failed inputs stay
unknown. The driver saves statuses, rows, summaries, inputs/errors, bounds and
identity hashes. Direct summary rejects incomplete runs unless
`--allow-incomplete`; expected exhaustion can return a partial census with exit 0.

## Reading phases

```sh
python3 benchmarking/legacy-phase-table.py --invocation LABEL=DIRECTORY --output DIRECTORY
```

Repeat `--invocation` for multiple runs. To join Spot captures, also set
`ACACIA_PHASE_INVOCATION=LABEL` and `ACACIA_SPOT_CAPTURE_DIR=CAPTURES` during
recording, then pass `--capture CAPTURES`. Join only explicit matching
invocation/PID/subjob/run/K identities, conclusive WIN_K/LOSE_K captures and matching
entered/completed stages. Unbound, incomplete and ambiguous captures stay unjoined.
`ACACIA_DIAG_INSTANCE` supplies instance metadata, not invocation identity.

Output is `phases.tsv` and `workers.json`. Empty cells mean unknown. States are
completed, stopped, inapplicable, censored, unknown-entry and unknown-completion.
Keep occurrences separate, including repeated K or prepass/fallback work. Loss,
writer failure and sequence gaps make delivery partial; absent writer confirmation
is unconfirmed. Only the terminal snapshot's exact occurrence is censored; older
missing completions have no inferred end or enclosing interval. Censored durations
are lower bounds. Summaries subtract finished nested intervals, including stopped
work, and keep censored stages separate. Set `ACACIA_RELEASE_TEST_BUILD` to exercise
producer/CLI regressions with a release binary.

## Offline phase-record comparison

`check-cap-independence.py` and `recycle-cap.py validate` compare recorded outcomes,
configuration, the accepted winning arm, terminal outcomes/reasons, decisions, declines,
and deterministic work. They never run a solver. Unknown fields are compared by default;
the comparator has no name, instance, verdict, or expected-counter exception.

The comparator joins parent and child records by physical PID within each invocation,
then assigns configured arm identities from `worker_spec` and `worker_spawn`. For a legacy
REAL worker this is polarity/translation/backend; for UNREAL it is polarity/transform/backend;
for a native worker it is the recorded kind. A legacy phase labelled merely `legacy` belongs
to its producer's worker, not to a single aggregate legacy arm. Child work remains in
producer order. Parent terminal snapshots follow it. Stage occurrence IDs are renumbered
by first appearance within that worker, preserving references and occurrence multiplicity.
`run_id`, `subjob`, `subjobs`, `prepass`, `worker` index and K remain compared.

Completion in this format means a parent terminal with `telemetry=complete`, no signal and
a nonnegative exit code, plus child records. A cleanup terminal can report `winner_cancelled`
after reaping a child that already exited normally: its outcome and reason still participate
in comparison. Signalled or incomplete arms remain subject to the existing race-truncation
policy. On conclusive rows with that policy enabled, arms that are incomplete in either run
are skipped. Configuration and parent winner remain checked independently. Completed arm
records cannot disappear behind this exception. The older per-arm `record_summary` format
continues to use its original completion and ordered-record comparison.

Exact exclusions are `VOLATILE_FIELDS` and `VOLATILE_METRICS` in
`tools/check-cap-independence.py`, including `PHASE_VOLATILE_FIELDS` from `benchlib.py`.

Transport completeness is retained per worker. A worker with dropped records has unknown
work; its work comparison is skipped, without assigning missing counters zero or claiming
complete attribution. Other complete workers and parent configuration/winner facts remain
strictly compared. Parent winner delivery snapshots (`telemetry`, `dropped_records`) belong
to transport completeness, separately from the winner's solver facts. Both runs' drops are
reported even when the first run already has a drop.

For the current transport,
missing/duplicate writer summaries, nonzero `failed_records`, `incomplete_packet`, a
`delivered_records` count that disagrees with the number of stored pipe packets, worker sequence
gaps, conflicting worker identities, missing terminal records and missing child records for
completed workers fail validation. Writer delivery totals are audited within each run;
they are not equated across runs because race losers can emit different numbers of packets.
The pre-fork `worker_spawn` sequence is checked separately from the child's sequence.
`writer_summary` and `parent_record_summary` supply transport evidence to these
checks and do not contribute solver work.
Durable `<pid>-final.jsonl` files bypass the pipe writer and its delivery counter;
the filename identifies them, not the event type. Exclude those files and the
writer summary itself from the delivered packet count, retaining their records
for lifecycle and work comparisons.

All other content remains compared, including verdict/exit/signal, reason, backend, route,
polarity, telemetry, source/proof bindings, limits, K, states, edges, rows, queries, work counts,
GC counts, cache counts and deterministic byte estimates. A genuine mismatch invalidates
recycling; it must be reported and adjudicated without changing this comparison to fit it.


### Explicit driver adjudications

`recycle-cap.py validate --adjudications TSV` and the independent comparator CLI
(`--adjudications TSV --series LABEL`) apply driver decisions after strict comparison.
The TSV columns are `series`, `instance`, `finding_class`, `mismatch`, `decision`,
`rationale`, `evidence`. `mismatch` is the exact failure message, excluding the CLI's
`MISMATCH [kind]` display prefix. `evidence` is a nonempty JSON list of objects with
`path` and `sha256`; relative paths resolve against the TSV directory.

Only an exact series/instance/message match is eligible. All cited SHA-256 hashes
must verify; both original TSVs and every available phase file for the pair must
be cited. Both rows must have identical result, exit code, timeout flag and
resource reason and the same series label. Each comparison retains separate structured
work, configuration, proof-binding, outcome, decline and lifecycle differences. Unknown
fields remain strict. The only eligible `decision` differences are the parent's winning
worker/arm/backend/stage and `parent_terminal.reason` changing between `exit` and
`winner_cancelled`. Other lifecycle reasons and terminal outcomes remain strict.
An accompanying work or other strict difference stays a failure even when the eligible
variation is adjudicated.

The `telemetry-dropped` class removes only an exact transport-completeness finding with
structured drop counters; it cannot remove work or parent facts. The
`same-verdict-winner-variation` class requires structured eligible decision differences on
conclusive rows. Outcome, panel, work, configuration, proof-binding, decline, completion
and other lifecycle failures cannot be adjudicated.
Missing or changed evidence, malformed rows and duplicate bindings fail closed.

Validation JSON retains every accepted mismatch, class, driver decision, rationale,
evidence binding and identical outcome, plus the adjudication TSV path and digest.
Merge replays the full validation, including these checks, before writing derived rows;
its provenance retains the accepted findings. Validation output and the campaign report
have an Adjudicated recycling mismatches section. The report replays the saved
validations and checks the derived provenance before disclosing their accepted findings
in Markdown and JSON. Telemetry completeness remains incomplete in attribution reports.

## Timeout and search observations

Timeout latches TIMEOUT/exit 124 despite late answers. Wall time includes cleanup;
unsolved PAR-2 stays twice the cap. Route launches enforce a monotonic acceptance
deadline. Cancellation sends SIGTERM to the unreaped main PID and allows one
absolute 500 ms window; scheduling delay consumes it and repeated requests cannot
renew it. The writer receives no initial scope-wide TERM. Parent closure allows
100 ms writer drain and 10 ms reap after forced stop. At deadline, cgroup.kill
reclaims descendants, then the driver drains, reads peak/events, acknowledges and
removes the scope. Collection never extends verdict acceptance. Malformed/legacy
cancel markers grant no extra time.

Nonsolve obstruction uses each surviving/signalled worker's latest mapped stage;
earlier declines do not vote. Distinct known stages are mixed; absent evidence is
unknown. This does not identify the critical path. Native preparation is
construction, seed/direct/obligation work search, target checking verification.
Summary-only records provide no endpoints or unentered checker zeros.

Dense first-useful-query marks completed rank/action application; sparse first
query starts at row-store factory creation, so clocks differ. Dense samples at
the first four applications and powers of two (at most 65 per stage) record
`query_snapshot_ns`, `completed_action_applications`, `query_rank_support`,
`query_rank_maximum`. `first_useful_action_query_ns` is the first completion's
absolute time; stage changes reset counts. Samples describe source ranks, not
unique ranks or retained memory. Time metrics are excluded from cap equality;
counts/rank values stay compared. Only explicit completions count. Whole-scope
20 ms samples bracket verifier entry only within a maximum 100 ms gap.

`parity_reduction_attempt` carries the full worker lifecycle context, `seq` and
`dropped_records`, plus the reduction option, attempt, outcome, cause, child PID,
timing and memory cap. The parity worker emits it after the bounded child is
reaped, or when an attempt is skipped. The reduction child has recording disabled;
attempt outcomes describe controller optimization and establish no verdict.

## CPU and resource accounting

This section replaces `prof/README.md`, archived in docs-20261122.

| Record | Meaning |
|---|---|
| `worker_reap` | Parent `wait4` user/system nanoseconds, Linux peak RSS kB, status/reason, PID/start identity. Self CPU subtracts known reaped descendants; uncertain values are null. |
| `worker_resource` | Monotonic time, process-only CPU, current RSS, start ticks and clock resolution at route/result/exit/cancellation boundaries. Parent `/proc/PID/stat` samples have one scheduler-tick resolution. |
| `descendant_spawn`, `descendant_reap`, `descendant_censored` | Subprocess identity and usage attributed to a worker. Worker `wait4` includes reaped descendants; do not add it to descendant totals. |
| `accounting_segment`, `accounting_segment_censored` | Nonoverlapping leaf intervals, paused during child work. Unfinished CPU is null. |
| `accounting_entry`, `accounting_end`, `accounting_censored` | Phase/parent IDs, absolute monotonic/process CPU stamps, durations and loss counts. Exclusive totals subtract children. Frozen records retain conservative accounting. |
| `work_identity` | SHA-256 of actual formula, ordered length-prefixed I/O alphabet, provider, translation and preprocessing options. `ACACIA_PHASE_RECORDS_FORMULA=1` adds exact `spot::str_psl` text in `PID-final.jsonl`. |
| `native_work_identity`, `accounting_artifact` | Full TLSF source/reduction/options/provider identity and exact checked AAG digest. |
| `parent_record_summary` | Final loss counter; worker specs, reap usage, recovered boundaries and active entries use `PID-final.jsonl` independently of the pipe. Failed final writes increment loss and report stderr. |

Match identities only within invocation directory, PID, subjob and run ID; missing
identities stay unknown. Each worker has a durable 16-entry phase stack and five
resource boundary slots, recovered after reaping. Repeated boundaries replace the
last durable sample while pipe history survives if delivered. Killed phases have
null end CPU even with known final wait4 usage. Loss makes exclusive residuals
unknown. Recovery covers worker death and handled parent cleanup, excluding parent
SIGKILL or power loss. Linux subreaping/process groups cover descendant adoption.

Obligation instrumentation surrounds the shipping Spot call without changing its
arguments or ordering. `obligation_spot` is opaque; translation, propositional
preprocessing and SCC/game propagation inside Spot are not split.

```sh
python3 benchmarking/prof/analyze-prof.py --rows ROWS --panel PANEL \
  --phases PHASES --output-dir OUTPUT
```

The analyzer accepts frozen/new records. It writes workers, phases, lifetimes,
stages, native, segments, identities, descendants, duplicates, quality and per-arm
tables. `quality.tsv` reports usage coverage, parent summaries, censored work and
loss. Per-arm observed/final CPU ratios require final usage for every worker in
the group. Lifetimes retain boundary-to-reap CPU resolution bounds.

Boundary RSS and time until reap cannot measure retained-memory integrals; those
cells stay NA. Declined routes may do useful fallback work; terminal/cancellation
boundaries identify useless work. Recording overhead remains uncalibrated.

## Fault and corruption hooks

<!-- attribution-hooks:start -->
| Hook / corruption / control | Expected telemetry | Runtime assertion |
|---|---|---|
| `env_rank_fault` 1,2,3,4,5,7,9,10,12 | STOPPED/error | `native_failure_census`: exact route/stage/cause, no DECLINE, verified direct fallback |
| `env_rank_fault` 6,8 | DECLINE/applicability | `native_failure_census`: bounded grammar mismatch or completed certificate rejection, verified direct fallback |
| `env_rank_fault` 13 | STOPPED/resource | `native_failure_census`: rank-depth overflow, verified direct fallback |
| `lift_test_fault` 1 | STOPPED/error | `native_failure_census`: game/seed binding, verified direct fallback |
| `lift_test_fault` 2,4 | STOPPED/resource | `native_failure_census`: checker capacity/allowance, verified direct fallback |
| `lift_test_fault` 3 | DECLINE/applicability | `native_failure_census`: CHECK_OK/CERT_FAILED, verified direct fallback |
| `lift_test_fault` 5 | VERIFIED, no failure | `native_failure_census`: recovered game mutation with matching binding, one final check |
| `lift_test_fault` 6,8 | STOPPED/error | `native_failure_census`, `native_corruption_guards`: corrupt R justice metadata or verified U verdict; verified direct fallback with unchanged proof |
| `lift_test_fault` 7 | STOPPED/error | `native_failure_census`, `native_lift_api`: corrupt verified R verdict; legacy DECLINED, no published proof, both proof orders |
| `lift_test_fault` 9 | DECLINE/applicability | `native_fold_budgets`, `native_failure_census`: CHECK_OK/CERT_FAILED, R/DECLINE/fold_seed_check, then DIRECT VERIFIED; retained check counters |
| `lift_test_fault` 10,11,12,13 | STOPPED/resource | `native_fold_budgets`, `native_failure_census`: allocation failure during discovery, folded candidate construction, region checking or checker JSON copy; UNKNOWN, no certificate, JSON cleanup for 13 |
| `lift_test_fault` 14,15 | DECLINE/applicability | Upstream `native_parametric_fold_both`, `native_failure_census`: seed-window state growth / ordered-domain rejection |
| `lift_test_fault` 16-21 | DECLINE/applicability | Upstream `native_parametric_fold_both`, `native_failure_census`: tag transport, candidate, seed agreement, region rejection, rank depth and fairness; no verified R event |
| `lift_test_fault` 22-26 | STOPPED/resource | Upstream `native_parametric_fold_both`, `native_failure_census`: allocation failure in support discovery, candidate construction, region checking, JSON copy or cached-seed discovery; no published proof, JSON cleanup for 25 |
| `both_test_seed_fault` 1 | DECLINE/applicability | `native_failure_census`: mixed seed polarity, verified direct fallback |
| `both_test_seed_fault` 2 | STOPPED/error | `native_failure_census`: unknown seed, verified direct fallback |
| `both_test_seed_fault` 3 | STOPPED/resource | `native_failure_census`: U schema capacity, verified direct fallback |
| `both_test_seed_fault` 4 | STOPPED/deadline | `native_failure_census`: expired seed solve, no verdict |
| `both_test_check_fault` 1,2; U observer mutation before checking | STOPPED/error | `native_both_api`: late prepared-game/source binding; no verified result |
| `env_rank_test_apply_cap`, `both_test_env_rss_spike_bytes` | STOPPED/resource | `native_both_api`, `native_real_both_api`: cap/RSS cooperation, retained legacy status |
| Reduction `source-duplicate`, `formula-ap` | STOPPED/error | `attribution-contracts`: direct + all three lifting arms; both prepare APIs and legacy UNSUPPORTED/DECLINED |
| Reduction `monitor-incomplete`, `monitor-nondeterministic`, `monitor-acceptance`, `monitor-empty`, `monitor-sticky` | STOPPED/error | `attribution-contracts`: actual completed Spot monitor checks, all four arms |
| Reduction `transition-ap`, `transition-temporal`, `provenance-boolean` | STOPPED/error | `attribution-contracts`: original Boolean/AP validators, all four arms |
| Reduction `unsupported-class` | DECLINE/applicability | `attribution-contracts`: original structural recognition decline, all four arms |
| Preparation `provenance-duplicate`, `provenance-retry` | STOPPED/error | `attribution-contracts`: all three lifting arms and both versioned/legacy APIs; earlier integrity retained after strict applicability failure |
| Preparation `provenance-recover` | OK, no retained failure | `native_contract_api`: successful strict retry clears cause; exact preparation still rejects |
| `seed-null`; null, constraints/no justice, nonsingleton justice, nonconstant reset | STOPPED/error | `native_contract_validators`: all three seed solvers; `attribution-contracts`: shared seeds followed by verified direct fallback in both combined modes |
| U unnamed latch, missing unique bad predicate, nonsingleton justice, Skolem letter dependency | STOPPED/error | `native_env_contract_validators`: each original guard, immutable cause and legacy DECLINED; both environment/system dependency sets |
| Acacia `source-hash`, `swap-game`, `swapped-game-with-matching-hash`, `swap-certificate`, `wrong-side`, `wrong-method`, `missing-policy-hash`, `policy-hash`, `unexpected-region-policy-hash`, `unverified-region`, `corrupt-proof` | STOPPED/error | `attribution-contracts`: every hook in all three lifting arms, complete lifecycle, UNKNOWN, no DECLINE |
| Acacia `ACACIA_NATIVE_TEST_CORRUPT_PROOF` | STOPPED/error | `attribution-contracts`: direct proof parser/checker rejection, UNKNOWN |
| Acacia `r-decline` | STOPPED/resource | `attribution`: bounded schema resource exhaustion, verified direct fallback |
| Acacia `region-method` | VERIFIED (region) | `native-gr1-lift-cli` / `native-param-lift-cli`: resource-limited policy alternative recovers via region verification |
| Acacia `ACACIA_NATIVE_TEST_TARGET_CHECKS` | VERIFIED for R 1-2, U 1, direct 1-3; otherwise STOPPED/error | `native-gr1-lift-cli`, `attribution-contracts`: missing/excess checks rejected; `native-gr1-parametric-fold-lift-cli`: parametric R retry accepted and excess rejected with bindings retained |
| Acacia exceptions `bad-alloc`, `standard`, `unknown` | STOPPED/resource, error, error | `attribution`: complete terminal UNKNOWN lifecycle, no DECLINE |
| Spot provider faults `allocation`, `length`, `capacity`, `bdd_growth` | STOPPED/resource | `spot-lazy-buchi-view`: original provider/BDD checks + serialized lifecycle |
| Spot provider faults `count`, `meaning`, `late_meaning`, `mark`, `ap_inventory`, `null_iterator`; missing provider, foreign/null state | STOPPED/error | `spot-lazy-buchi-view`: original contract checks, UNKNOWN, serialized lifecycle, no partial row |
| Unsupported PSL, arbitrary/incomplete acceptance, raw alternation | DECLINE/applicability | `spot-lazy-buchi-view`: legacy DECLINED + serialized lifecycle |
| Closure `FaultPoint::{branch,guard,interning,publication}` | STOPPED/error (`injected`) | `closure-buchi-provider`: each rollback point; `spot-lazy-buchi-view`: all 11 FailureKind lifecycle conversions |
| Closure cancellation and normalization/branch/guard/state/row/memory budgets | STOPPED/cancelled or resource | `closure-buchi-provider`: genuine fault origin; `spot-lazy-buchi-view`: every cause conversion |
| TAA rank node cap zero, `candidate=only/fallback` | STOPPED/resource | `attribution`: both modes; retained UNKNOWN or verified incumbent fallback |
| `ACACIA_TEST_ATTRIBUTION_PAUSE` at R/U/direct/checking; worker SIGKILL; parent SIGTERM | Parent STOPPED/deadline/signal/interrupted; incomplete child telemetry | `attribution`: selected route retained, never interpreted as applicability |
| `ACACIA_TEST_CHILD_MODES` / `ACACIA_TEST_DELAY_AFTER_REAP` (late, failed, killed, winner) | Parent lifecycle reason; late result rejected | `native-gr1-cli`: deadline/kill/reaping/winner attribution |
| `ACACIA_TEST_SYNTHESIS_REAP_FD` | Parent SIGKILL cancellation reaped before interruption check | `synthesis-rendezvous`: deterministic `interrupted` terminal reason |
| Transport writer stall/delay, 512-byte pipe, full/broken/slow destination | Telemetry loss/incomplete; never a solver decline | `worker-record-transport`, `record-transport`: bounded nonblocking delivery, explicit dropped/incomplete records |
<!-- attribution-hooks:end -->
