# Legacy worker phase records

`ACACIA_PHASE_RECORDS` enables the existing nonblocking pipe/writer transport.
The solver never opens telemetry files. Packets, including framing, respect the
runtime `PIPE_BUF`; full pipes drop packets immediately. Sequence numbers and
drop counts retain their existing worker-wide meaning. No options, limits,
search choices, worker membership or verification obligations change.

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
`subjob-summary`, `translation`, `translation-lowering`, `input-push`,
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

`benchmarking/legacy-panel.py` accepts fresh `--acacia` rows, historical
`--comparator` rows, `--classification` structural metadata, `--phase-root`, and
optional paired `--baseline` allowance rows. `--source-root` supplies the TLSF sources
bound to the metadata's SHA-256 values. The sampler orders by size, AP counts, parameter
dimension, semantics and evidence features; ties use SHA-256 of source tokens with
comments, quoted descriptive text and whitespace removed. Names and paths never rank
rows. Identical structural/evidence copies are exchangeable. It refreshes membership, and covers all observed missing-set
polarities, native rejection reasons and legacy last stages. It first includes
paired gains, four seeded both-unsolved samples, and three samples each of
Acacia-only and near-cap common solves; evidence-stratum coverage may require
additional both-unsolved rows. Near-cap means at least 80% of the supplied cap,
the existing classification threshold. Remaining places use structural frontier
sampling. Default size is 28, allowed range 24–32. Missing strata are disclosed;
insufficient capacity fails instead of silently dropping required strata.
Worker events are merged by monotonic time and sequence before choosing the last stage.
Writer confirmation, failures, drop counters and sequence gaps retain their delivery state.
Names are identity data only in the generated manifest. Seeds, selection reasons,
input digests, phase-tree digest, structure, refreshed outcomes and binary pins
are recorded. Historical comparator times remain historical; this tool makes no
matched performance claim and launches no solvers.

The existing weakening census and attribution completeness reader also recognize
compact stage packets and the separate parent snapshot sequence. Their prior
lifecycle, writer confirmation and missing-data checks remain required.
