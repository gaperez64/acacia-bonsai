# Offline phase-record comparison

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


## Explicit driver adjudications

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
