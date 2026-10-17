# Legacy worker phase records

`ACACIA_PHASE_RECORDS` enables the existing nonblocking pipe/writer transport.
The solver never opens telemetry files. Packets, including framing, respect the
runtime `PIPE_BUF`; full pipes drop packets immediately. Sequence numbers and
drop counts retain their existing worker-wide meaning. No options, limits,
search choices, worker membership or verification obligations change.

Generate the detailed source-line census on demand with
`python3 tests/check-decline-causes.py`; it writes ignored
`build_scratch/attribution/decline-causes.md`. Use `--output FILE` and
`--check` to export or validate a saved census. The hook table below is the living
contract; source-line listings belong in generated reports.

The observation header is `src/solver/phase_observation.hh`. Graph censuses and
payload walks run only with phase observation enabled. Disabled scopes return before
PID, clock, resource and exception sampling. The actioner caches that decision outside
its input loop; added hot-loop counters use cached observer flags or sink/pointer guards. Times include observation
work inside the stage; they are diagnostic observations, not performance results.
RSS is the process high-water mark from `getrusage`, not current retained memory
or a concurrent cgroup peak. External collection remains authoritative for those.

## Wire schema

Each newline-delimited JSON object is one bounded packet. Existing lifecycle,
native, weakening, equivariance and generic phase objects remain readable.
The added event forms are:

| Event | Fields |
|---|---|
| `stage_entry` | `event`, `worker`, `worker_pid`, `seq`, `subjob`, `subjobs`, `run_id`, `k`, `stage_id`, `stage`, `mono_ns`, `dropped_records`, `reason` |
| `stage_completion`, `stage_stopped` | entry fields plus `wall_ns`, `cpu_ns`, and `peak_rss_kb` when `getrusage` succeeds |
| `stage_metric` | `event`, `worker_pid`, `seq`, `stage_id`, `mono_ns`, `dropped_records`, `key`, `value` |
| `stage_censored` | parent snapshot: `event`, `worker`, `worker_pid`, `seq`, `subjob`, `subjobs`, `run_id`, `k`, `stage_id`, `stage`, `entry_ns`, `mono_ns`, `signal`, `dropped_records` |

`worker` is the physical portfolio index, `worker_pid` its PID. `stage_id` is a
worker-local monotonically allocated stage occurrence, including nested stages.
`subjob` is one-based for executed runner jobs; zero denotes worker preparation.
`subjobs` is null until planned jobs are known (extended witnesses remain lazy).
`run_id` is the existing weakening/fallback identity. `k` is null before search.
Counters are decimal strings in `value`; distributions, reasons and statuses are
strings. All stage/metric keys are bounded internal metadata, never source text.
Unknown measurements are omitted. A measured empty complete graph can have zero
states/edges/SCCs; an absent graph cannot. Inapplicable stages have reason
`inapplicable-*` and no work counters. Their event overhead is not solving cost.
Exceptions produce stopped events through scope unwinding. SIGKILL cannot unwind;
the parent's shared-memory stage snapshot preserves its identity and entry time.
A dropped snapshot remains missing evidence, never an algorithmic outcome.

Stages: `simplification`, `direct-checks`, `pre-pass`, `decomposition`, `subjob`,
`subjob-summary`, `translation`, `translation-event-univ`, `translation-lowering`, `input-push`,
`input-push-lowering`, `preprocessing`, `booleanization`, `io-preparation`,
`io-decoding`, `action-construction`, `picker-preparation`, `k-attempt`, `search`,
`verification`, and `certificate-check`. Translator time covers `trans.run`,
including Spot's internal postprocessing; explicit lowering is separate. Lazy
IO implementations can decode during action construction; unavailable independent
decode observations stay absent. Classic backward final replay and frozen sparse
action construction are explicitly inapplicable. Local backward certificates
still expose their independent certificate-check stage.

## Metric keys

| Stage | Keys |
|---|---|
| Decomposition / pre-pass / summary | `subjobs_planned`, `subjobs_executed` |
| Complete graphs | `states`, `edges`, `acceptance_sets`, `sccs`, optionally prefixed `input_` / `output_` |
| Booleanization | `numeric_dimensions`, `boolean_dimensions` (numeric coordinates precede the Boolean tail) |
| Input push | `interned_keys`, `expanded_keys`, `distinct_original_sources`, `repeated_source_expansions`, `partition_computations`, `emitted_tuples`, `limit_reason` (`none`, `states`, `edges`) |
| IO decoding | `decoded_sets`, `decoded_endpoints`, `decoded_set_payload_bytes_estimate`, `decoded_set_peak_payload_bytes_estimate` |
| Actions | `action_payloads`, `action_endpoints`, `action_table_payload_bytes_estimate`, `action_construction_peak_payload_bytes_estimate` |
| K attempts | `k_attempts_started`, `k_attempts_completed`, `status` |
| Search / verifier oracle | `queries`, `steps`, `bdd_operations`, `peak_live_nodes`, `peak_result_nodes`, `threshold_calls`, `threshold_misses`, `threshold_shortcuts`, `threshold_hits`, `preimage_calls`, `preimage_misses`, `preimage_hits`, `cache_rank_bytes`, `oracle_payload_bytes_estimate`, all prefixed `search_` / `verify_` |
| Search / verifier memo | `cache_evictions`, `oracle_memo_bytes_estimate`, `peak_oracle_memo_bytes_estimate`, `oracle_memo_budget_bytes`, `interned_targets`, all prefixed `search_` / `verify_` |
| Sparse rows | `row_calls`, `failed_rows`, `wrapper_rows_requested`, `wrapper_rows_generated`, `wrapper_states_discovered`, `wrapper_edges_generated`, `search_rows_requested`, `search_generated_rows`, `row_generation_ms`, `row_cache_payload_bytes_estimate` |
| Sparse search | `expansions`, `game_states`, `guarded_choices`, `queue_pushes`, `queue_pops`, `queue_peak`, `loss_queue_peak`, `dependency_peak`, `rank_support_distribution`, `rank_support_sum`, `rank_support_max`, `rank_interner_bytes`, `losing_antichain_rank_bytes` |
| Existing sparse proof work | `subsumption_scans`, `subsumption_nodes_checked`, `subsumption_nodes_invalidated`, `scan_tombstones`, `scan_prefilter_rejects`, `scan_exact_compares`, `reopen_enqueues`, `subsumption_queries`, `subsumption_hits`, `subsumption_prefilter_skips`, `losing_insertions`, `losing_removals`, `losing_antichain_size`, `losing_antichain_peak`, `proofs_total`, `proofs_in_initial_cone`, `dependency_list_len_sum`, `dependency_list_len_max` |
| Verification | `retained_search_payload_bytes_estimate` sampled before the independent checker; `verification_rows_requested`, `verification_rows_rebuilt`, `verification_row_ms`, `verification_row_payload_bytes_estimate`, `verification_applications` where applicable |
| Verifier subphases | `verify_traversal_*`, `verify_invariant_*`, `verify_proof_bad_*`, with suffixes `observed`, `queries`, `steps`, `bdd_operations`; only entered phases appear in P |
| BDD GC | `bdd_gc_count`, `bdd_gc_wall_ns` per observed sparse search/check; hook preserves the previous handler and is never installed with observation off |

Threshold calls include boundary shortcuts and failed queries. Misses count
cache lookups that require computation, including computations stopped before
publication. Thus calls = hits + misses + shortcuts plus any calls stopped before
lookup. Preimage calls/misses likewise preserve interrupted work. Row calls count
all store accesses, requested rows count missing cache rows, and requested-source
sets count distinct sources. Queue pushes count expansion-queue enqueues; pops
include expansion and loss queues. Dependency peak is maximum retained incoming
references at one rank node, not a global edge count.

Byte estimates count accounted object/vector payload capacities. They omit
allocator/list/tree/hash-node overhead, bucket storage, BDD manager allocations,
and the immutable Spot graph. Decoding is append-only, so its accounted peak is
its retained payload. Action-construction peak includes retained unique tables
and the pending input's table before deduplication. Search snapshots include
ranks, choices, proofs, incoming references, queued identifiers, oracle entries
and decoded search rows; the checker remains independent. These estimates are
not substitutes for measured heap/RSS/cgroup peaks.

Memo byte fields describe current, peak and allowed memo storage; eviction counts
count cache clears. `interned_targets` counts currently referenced target identities,
not all identities ever seen. `cache_rank_bytes` includes their owned ranks;
`oracle_payload_bytes_estimate` includes reference counts and retained buckets.
Search cache observations are taken at search completion before release;
`retained_search_payload_bytes_estimate` measures what remains at verification entry.
Memo accounting/history: `refactor-closeout20261017`, `legacy20261008-closing`.

## Lifecycle and native verification

Every lifecycle and `worker_spec` record, including parent spawn/terminal/winner
records, must contain JSON booleans `r_prepass` and `equivariance`. These describe
invocation settings even if a worker bypasses the hook. Missing, nonboolean or
conflicting settings make delivery incomplete. `route_start:equivariance` precedes
recognition and does not imply admitted symmetry; `route_start:backward` means
ordinary solving after decline or with equivariance off.

Native combined routes are `seed_discovery`, `R`, `U` and `direct`. Selection/start
events precede their work. `verification_start` precedes checking; `check_complete`
means the independent checker succeeded; `verification_complete` follows Acacia's
artifact/source binding. Only `parent_winner` establishes parent acceptance after
deadline/interrupt checks. Intermediate verification events establish no accepted
verdict. Historical event/API details: `refactor-closeout20261017`,
`cov20261006-p0exit`, `cov20261006-p1ablation`.

## Weakening records and completeness

IDs use invocation directory, `worker_pid` and `prepass`; `run_id=0` names the full
objective and positive IDs are candidate index + 1. Lifecycle fields
`weakening_requested_mode` / `weakening_mode` retain requested/effective modes
(`basic`, `extended`, `off`); automaton-UNREAL coerces extended to basic.

| Event | Payload contract |
|---|---|
| `weakening_entry`, `weakening_attempt_start`, `weakening_attempt_budget`, `weakening_fallback_start` | Absolute monotonic `deadline_ns` and observed `remaining_ns`; entry is actual pre-pass entry, fallback is full-objective entry. |
| `weakening_limits` | Basic: `candidate_limit=8`, `global_child_threshold=64`, `local_budget=null`. Extended adds `group_limit=4`, `group_size_limit=4`, `ast_node_limit=16384`, `ast_edge_limit=32768`, `support_membership_limit=32768`, `guarantee_limit=8192`, `support_visit_limit=32768`, `poll_ns=1000000`. |
| `weakening_eligibility` | Boolean `eligible`, `reason`, `generated`, `oversized_globals`, `largest_global_conjunction_size`, `safety_conjuncts`, `obligations`; largest size is zero if no immediate G conjunction was observed. |
| `weakening_candidate_generated`, `weakening_binding` | Generation precedes attempts; `candidate_index`, `obligation_index` and derivation identify selection. Bindings carry `kind`, `objective_fnv1a64`, `alphabet_fnv1a64`, `structural_size` where available; kinds distinguish `simplified_original`, `candidate`, `runner_objective`, `original_objective`. |
| `weakening_source`, `weakening_source_digest` | `source_fnv1a64`, `alphabet_fnv1a64`, `inputs`, `outputs`, `format`, `semantics`, `target`, `effective_target`, `rsimp_changed`; optional original TLSF `source_sha256`. |
| `weakening_attempt_end`, `weakening_end` | Attempt `outcome`, `reason`, `observer`, `wall_ns`, `cpu_ns`, `peak_rss_kb`, memory provenance; end carries total pre-pass wall/CPU time and outcome. |
| `weakening_budget_limits` | `attempt_fraction=0.05`, `prepass_fraction=0.20`, `cleanup_fraction=0.5`; requested/default `attempt_ms`, `total_ms`; JSON booleans `attempt_override`, `total_override`; no-deadline caps `unbounded_attempt_ns`, `unbounded_total_ns`; `memory_scope=inherited_invocation`. |
| `weakening_local_budget`, `weakening_attempt_limit` | Actual `prepass_deadline_ns`, `attempt_limit_ns`; per-attempt effective absolute `attempt_deadline_ns`, respectively. |

Basic eligibility reasons are `eligible`, `not_unreal_worker`, `synthesis_requested`,
`not_top_level_and`, `no_global_conjunction`, `global_conjunction_threshold_not_met`,
`absent_safety_conjuncts`, `no_non_safety_obligations`, and the generator's public
`zero_allowance`. Worker/synthesis gates describe an unentered generator, not an AST
census. A worker bypassed before entry has UNKNOWN eligibility. The threshold is
strictly more than 64 immediate children; safety conjuncts remain in every candidate.
`structural_size` counts AP/operator occurrences, treating n-ary operators as
binary. Basic `obligation_index` indexes the flattened non-safety vector.

`weakening_alphabet` uses `encoding=length_prefixed_partition_hex`, zero-based
`chunk`, total `chunks` and hex `data` (up to 64 original bytes per chunk). Decode
the exact ordered partition as input count, input strings, output count, output
strings: each number ends in a colon and each UTF-8 string has its byte length plus
colon before its bytes. Keep unused declared signals. FNV-1a-64 checksums bind
invocation artifacts and do not prove implication. `weakening_proof` links source,
candidate, transformed runner objective and derivation; extended checked-game
receipts prove the derived game, with parent replay transferring original UNREAL.
The source binding retains the unsimplified formula supplied to `run_ltl` and its
original partition, TLSF semantics/target and effective target.

Attempt outcomes are `proof`, `inconclusive`, `decline`, `exception`, `cancellation`.
Entry/fallback reset the opt-in outcome snapshot, so a prior stop cannot taint the
next attempt. Basic worker RSS is process-lifetime high-water, not per-attempt peak;
extended reaped-child RSS is that child's peak. After a kill, a parent cancellation
end has elapsed wall time and stop reason, with CPU/memory null; it is terminal,
not a completed worker attempt. `deadline_ns=0` with `remaining_ns=null` means no
invocation deadline. A measured zero remaining budget means exhausted time.

`benchmarking/weakening-census.py DIRECTORY --output census.tsv` requires generated,
started, terminal and proof run IDs to join, with each successful run's candidate
and runner-objective bindings. Missing starts/ends, bindings, producer prefixes,
sequence gaps, terminal metadata, writer summaries, malformed lines or nonzero
drops prevent exact counts. Confirmed `observed_generated`/`observed_cancellations`
remain lower counts; exact counts become UNKNOWN. Parent cancellation may close a
complete pre-pass census; a later killed fallback does not invalidate it. Known
zero fallback budget after weakening, or deadline termination before fallback in
a complete census, establishes starvation; positive budget does not. Missing/null
budget stays UNKNOWN, and winner cancellation stays separate. Schema history:
`refactor-closeout20261017`, `cov20261006-p0exit`, `cov20261007-screens`.

## Guarded translation observations

When the structural guard fires, legacy phase records contain
`translation-event-univ` entry/completion with `distinct_promises`,
`acceptance_set_width`, `simplified_promises` metrics. Observation does not enable
rewriting, and guard eligibility predicts neither an executed route nor a solve.

`tests/event-univ-census-isolated.py` runs one fresh process per source-map row.
The headerless `input-status.tsv` stores original one-based ordinal,
`ok|exhausted|error_N`, elapsed seconds; signal exits use 128 + signal, launcher
failure uses `error_127`. `census.tsv` has columns `source_row`, `status`, `frame`,
`component`, `expected_components`, `distinct_promises`, `width`, `guard_fires`.
The seven frames are `raw-positive`, `raw-negative`, `simplified-positive`,
`simplified-negative`, `real-components`, `formula-unreal`, `automaton-unreal`.
Discard a sole decomposition result as the runner does; retain all multiple REAL
components with their component-local pass.

Count an input only when status is `ok` and every expected component of all seven
frames is present. Partial firing rows never establish membership. Missing,
exhausted, failed and truncated inputs stay unknown. `summary.json` reports
complete counts, guard ordinals, status counts and exhausted ordinals;
`guard-firing.list` labels partial membership. `inputs/` retains individual output
and errors, `run.json` the bounds/isolation, and `identity.json` SHA-256 bindings for
source map, census, statuses, executable and sources plus build/compiler/libraries.
Direct summary rejects incomplete runs unless `--allow-incomplete` is supplied.
The isolated driver returns nonzero for errors/unexplained incompleteness; expected
exhaustion reports a partial census with exit zero. History:
`refactor-closeout20261017`, `census20261013-isolated`.

## Readers and selection

To bind fresh Spot captures, set `ACACIA_PHASE_INVOCATION=LABEL` alongside
`ACACIA_PHASE_RECORDS=DIRECTORY` and `ACACIA_SPOT_CAPTURE_DIR=CAPTURES`, then run
`benchmarking/legacy-phase-table.py --invocation LABEL=DIRECTORY --capture CAPTURES
--output TABLE`. Use a distinct label for each invocation. The producer copies that
label into `invocation` in every capture snapshot and optional history record through
the existing capture write path. It adds no file operations. The label is read only
when capture is enabled; setting it alone leaves records-off execution unchanged.
The phase reader assigns the same supplied label to its rows. `ACACIA_DIAG_INSTANCE`
remains separate instance metadata and does not supply invocation identity. An unset
or empty invocation label leaves captures unbound and therefore unjoined.

Set `ACACIA_RELEASE_TEST_BUILD` to a release build directory when running
`tests/pytest/test_legacy_phase_table.py` to exercise the producer/CLI regression.
It uses generated specifications, two decomposed frozen sparse subjobs, and K=2/5.

`benchmarking/legacy-phase-table.py --invocation LABEL=DIRECTORY --output DIRECTORY`
uses `benchlib.load_phase_records`; repeat `--invocation` to join runs. Optional
`--capture DIRECTORY` joins complete Spot captures for one invocation only when
invocation/PID/subjob/run/K are all present and match unambiguously. Captures must
carry an explicit `invocation` matching the supplied label, a conclusive `WIN_K` or
`LOSE_K` status, and matching entered/completed stage records. Search rows receive
search metrics; verification rows receive verifier metrics. Historic captures without
these identities or completion evidence remain unjoined. Incomplete captures never supply completion
times or prove a phase finished. Historic unentered verifier subphase zeros stay
absent. Rows bind subjob/run/K to entry identity, with planned totals enriched when known.
Rows preserve occurrence IDs in addition to invocation/worker/subjob/K/
stage; repeated pre-pass or fallback work is never merged. Output is `phases.tsv`
plus `workers.json`. Empty cells are unknown. `state` distinguishes completed,
stopped, inapplicable, censored, unknown-entry and unknown-completion. Drop counts, writer failures
or sequence gaps mark `delivery=partial` even when a particular row completed.
An absent writer confirmation is `delivery=unconfirmed`.
Only the exact stage occurrence identified by the terminal snapshot is censored.
An earlier missing completion remains unknown, with no terminal-time lower bound;
enclosing intervals are not inferred from nesting. Censored durations are lower bounds,
not completed durations. Worker summaries
subtract nested finished intervals (including observed stopped work) and keep censored stages separate.

Historical panel construction procedures are preserved in the campaign archives.
The retained phase reader and classification commands accept explicit restored evidence.

The existing weakening census and attribution completeness reader also recognize
compact stage packets and the separate parent snapshot sequence. Their prior
lifecycle, writer confirmation and missing-data checks remain required.

## Offline phase-record comparison

`check-cap-independence.py` and `recycle-cap.py validate` compare recorded outcomes,
configuration, the accepted winning arm, terminal outcomes/reasons, decisions, declines,
and deterministic work. They never run a solver. Unknown fields are compared by default;
the comparator has no name, instance, verdict, or expected-counter exception.

The current producer in `src/phase_records.hh` writes packets into producer-PID files.
Parent files contain `worker_spawn`, `worker_spec`, `parent_terminal`, `parent_winner`,
and the parent phases `source_snapshot`, `frontend_conversion`, `argument_handling_total`.
Child files contain lifecycle, weakening, phase and legacy stage events. Stage metrics
encode the metric name in `key` and its value in `value`. A separate writer file contains
`writer_summary`. `benchmarking/legacy-phase-table.py` in the candidate worktree joins
stages by physical PID and `stage_id`; its reader otherwise retains these raw events.

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

These exact top-level fields are excluded from equality:

| Fields | Reason |
| --- | --- |
| `pid`, `worker_pid` | OS process identities differ on every invocation; used to join before exclusion. |
| `seq` | Transport serial number, including events carrying only timing. Audited for gaps within each run. |
| `scope`, `unit`, `scope_name`, `unit_name` | Invocation-specific resource scope/unit names. |
| `mono_ns`, `entry_ns`, `completion_ns`, `stage_start_ns` | Absolute monotonic timestamps. |
| `deadline_ns`, `outer_deadline_ns`, `absolute_ns`, `reference_ns` | Absolute deadline/reference clocks; different launches/caps necessarily change them. |
| `wall_ns`, `cpu_ns`, `elapsed_ns` | Observed wall/CPU durations. |
| `remaining_ns`, `initial_remaining_ns`, `allowance_ns`, `consumed_ns`, `total_ns` | Time remaining, allocated or consumed in the invocation/relative time budget. Policy mode and fraction remain compared. |
| `rss_kb`, `peak_rss_kb`, `peak_rss_bytes` | Observed process resident memory, affected by runtime and scheduling. |
| `mallinfo2` | Allocator residency/free-arena snapshot, affected by runtime allocation history. Deterministic payload byte counts remain compared. |

Only these `stage_metric` records are excluded as timing measurements:

| Metric key | Reason |
| --- | --- |
| `bdd_gc_wall_ns` | Time spent in BDD garbage collection; `bdd_gc_count` remains compared. |
| `first_useful_action_query_ns`, `query_snapshot_ns` | Absolute dense query observation clocks. Query counts and rank/support values remain compared. |
| `first_useful_rank_query_ms` | Elapsed time to the first useful rank query. |
| `row_generation_ms` | Row generation duration. |
| `row_started_clock_ms` | Row generation start clock. |
| `stage_started_clock_ms` | Stage start clock. |
| `verification_row_ms` | Verification row generation duration. |

Transport completeness is retained per worker. A worker with dropped records has unknown
work; its work comparison is skipped, without assigning missing counters zero or claiming
complete attribution. Other complete workers and parent configuration/winner facts remain
strictly compared. Parent winner delivery snapshots (`telemetry`, `dropped_records`) belong
to transport completeness, separately from the winner's solver facts. Both runs' drops are
reported even when the first run already has a drop.

For the current transport,
missing/duplicate writer summaries, nonzero `failed_records`, `incomplete_packet`, a
`delivered_records` count that disagrees with the number of stored packets, worker sequence
gaps, conflicting worker identities, missing terminal records and missing child records for
completed workers fail validation. Writer delivery totals are audited within each run;
they are not equated across runs because race losers can emit different numbers of packets.
`writer_summary` is transport evidence rather than solver work. Its fields are consumed by
these checks instead of contributing a fictitious solver arm.

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
have an **Adjudicated recycling mismatches** section. The report replays the saved
validations and checks the derived provenance before disclosing their accepted findings
in Markdown and JSON. Telemetry completeness remains incomplete in attribution reports.

## Lifecycle, causes and delivery

Use a fresh diagnostic directory per invocation. Files name emitting PIDs; join
workers by physical PID and configured `worker_spec`, not a generic legacy label.
Parent spawn/terminal sequences are independent of child sequences. Requested
backend stays requested; effective backend follows selection/coercion. Original
polarity and transformed proof polarity differ; seed polarity is not a target proof.

Lifecycle events are `worker_start`, `route_selected`, `route_start`, `decline`,
`terminal_result`, `parent_terminal`, `parent_winner`. The winner appears only after
parent deadline/interruption acceptance. Exit codes are 0 REAL, 1 UNREAL, 2 UNKNOWN,
3 ERROR. Signals have exit_code -1 and an explicit signal. Rejected late answers
have no winner. Combined native routes are seed discovery, R, U and direct; entry,
checking and source-binding completion remain distinct from acceptance.

DECLINE means genuine inapplicability/unsupported input or a completed logical
certificate rejection. Resource, deadline, cancellation, invalid input, integrity,
internal and operational failures are STOPPED with their typed cause. Finite K
exhaustion is resource; an unclassified false/UNKNOWN is inconclusive. Only
CHECK_OK/CERT_FAILED or REFUTED declines; CHECK_OK/INVALID or UNKNOWN is error.
Keep diagnostic cause independent of legacy return/fallback. Failed alternatives
retain earlier integrity causes; successful recovery clears them.

Machine-readable [cause expectations](../tests/attribution-cause-expectations.json)
checked by
`tests/check-decline-causes.py` and `test_attribution_fault_inventory.py`, own the
per-origin census. Source assertions are not runtime branch coverage. Corrupt
source/game bindings, monitor/AP/transition contracts, proof artifacts and checker
verdicts must fail without a published proof, while original fallback stays valid.

Producer `parent_terminal.telemetry` is `complete`, `dropped`, `incomplete` or
`unavailable`. Read shared snapshots after reaping. Delivery separately requires
exactly one writer summary, zero failed/incomplete packets, audited packet totals,
producer sequences, requested workers and parent terminals. Truncated trailing
lines retain their confirmed prefix and an incomplete reason. Missing/dropped
records never imply decline or zero work. Writer/destination failure changes
observability, not the solver verdict. Readers reject symlinks/nonregular files
and bound individual files to 64 MiB, retaining explicit rejected-file reasons.

Weakening IDs use invocation/PID/prepass; run_id zero is the original objective and
positive IDs identify candidate index + 1. Generated candidates, started attempts,
proof receipts and accepted original verdicts are separate. Bind source digest,
ordered alphabet including unused signals, normalization, objective and derivation;
FNV checksums bind invocation artifacts and do not prove implication. Basic proofs
are on the simplified original; extended replay starts at the exact parsed objective.
Unobserved eligibility, cancellations, generation counts and fallback budget stay
unknown. A never-started fallback can be starved; winner cancellation is separate.

## Timeout cleanup

On timeout the runner latches TIMEOUT/exit 124 even if a later verdict is printed.
Wall seconds include cleanup; unsolved PAR-2 remains twice the cap. Route-attributed
launches also enforce the absolute monotonic parent acceptance deadline.
Cancellation gives only the unreaped main PID SIGTERM and a single absolute 500 ms
cleanup window. Scheduling delay consumes it and repeated requests cannot reset it.
The writer gets no initial scope-wide TERM. Parent closure allows 100 ms for writer
drain and 10 ms to reap after forced stop. At the cleanup deadline cgroup.kill
reclaims descendants; then drain, publish peak/events, external read/acknowledge,
remove and sweep. Collection waits never extend verdict acceptance. Malformed or
legacy cancel markers grant no extra cleanup. Linux uses the owner's wait4 status
and RSS; process RSS remains distinct from concurrent cgroup peak.

## Construction/search observation

Invocation obstruction uses explicit MEMOUT separately. On nonsolves, surviving or
signalled workers vote by their latest mapped stage; earlier completed declines do
not. Distinct known stages mean mixed; absent evidence means unknown. This is not
critical-path attribution. Native preparation maps to construction, seed/direct and
obligation work to search, and explicit target checking to verification. Summary-only
records have no reconstructed endpoints; unentered checker zeros stay absent.

Dense first-useful-query marks a completed rank/action application; sparse first
query marks a successful oracle rank query from row-store factory creation. These
have different clock origins. Dense rank samples at the first four applications
then powers of two (at most 65 samples per stage) record `query_snapshot_ns`,
`completed_action_applications`, `query_rank_support`, `query_rank_maximum` after
the application. `first_useful_action_query_ns` marks the first completion's
absolute monotonic time. A stage change resets the count. Observation off performs
no scan, counter update, clock or resource sample. Samples describe the source
rank's support/maximum, not unique ranks or retained memory. Timestamp metrics
are excluded from cap equality; sampled counts and rank values stay compared.
Per-K occurrences retain repeated K across subjobs. Only explicit completions count;
an incomplete nested search/check cannot manufacture an enclosing completion.
20 ms scope samples bracket verification entry only within a 100 ms maximum gap;
these are whole-scope observations, not instantaneous checker memory or process RSS.

Exact attribution, cause/hook censuses, timeout and comparison predecessors are in
`refactor-closeout20261017`; original campaigns are `cov20261006-p0exit`,
`legacy20261008-closing-amend1`, `postmem20261012-closing`, `memory-search20261013`.

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
| Acacia `source-hash`, `swap-game`, `swapped-game-with-matching-hash`, `swap-certificate`, `wrong-side`, `wrong-method`, `missing-policy-hash`, `policy-hash`, `unexpected-region-policy-hash`, `corrupt-proof` | STOPPED/error | `attribution-contracts`: every hook in all three lifting arms, complete lifecycle, UNKNOWN, no DECLINE |
| Acacia `ACACIA_NATIVE_TEST_CORRUPT_PROOF` | STOPPED/error | `attribution-contracts`: direct proof parser/checker rejection, UNKNOWN |
| Acacia `r-decline` | STOPPED/resource | `attribution`: bounded schema resource exhaustion, verified direct fallback |
| Acacia `region-method` | VERIFIED (region) | `native-gr1-lift-cli` / `native-param-lift-cli`: resource-limited policy alternative recovers via region verification |
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
