Per-arm accounting uses the existing `ACACIA_PHASE_RECORDS` directory switch.
Create the directory before setting the variable. No diagnostics build, Meson
option, Python process, or periodic sampler is required. Recording disabled means
no added clock, resource, formula serialization, or identity hashing work on the
solver path. Scoped calls check the cached flag before initializing scope state.
Any recorder setup failure warns once on stderr and solving continues with
accounting off. Shared allocation precedes Linux subreaper setup.
`--phase-records-dir` in the benchmark runner enables the same switch.
Use a fresh output series and binary/source hashes for this build; frozen-binary
measurements and manifests remain separate.

Records added:

- `worker_reap`: the parent's per-child `wait4` user/system nanoseconds, Linux
  `ru_maxrss` in kB, status, reason, worker PID and pre-fork monotonic start identity.
  `self_cpu_ns` subtracts separately recorded descendants reaped by the worker.
  Missing usage or uncertain descendant subtraction is JSON null.
- `worker_resource`: monotonic time, cumulative **process-only** CPU, current RSS,
  kernel PID start ticks and CPU resolution at decline, route stop, verification,
  terminal result, explicit exit, observed exit and cancellation requests. Sibling
  cancellation has boundary `winner_cancelled`; interruption has `interrupted`.
  Self stamps use the process CPU clock; parent samples use one `/proc/PID/stat`
  read and have one scheduler-tick CPU resolution. RSS is a point observation.
- `descendant_spawn`, `descendant_reap`, `descendant_censored`: separate subprocess
  identities and `wait4` usage, attributed to their worker. The legacy safety-core
  prepass forks a subprocess; the obligation helper calls Spot in process. The
  accounting-enabled parent is a Linux subreaper and uses process groups to cover death during fork
  registration. Worker `wait4` user/system values include descendants it reaped;
  do not add those inclusive values to the descendant table.
- `accounting_segment` / `accounting_segment_censored`: contiguous leaf intervals
  that pause around nested work and resume at the child's exact completion stamp.
  These wall/CPU intervals never overlap. `segments.tsv` exposes them directly;
  unfinished segment CPU is null.
- `accounting_entry`, `accounting_end`, `accounting_censored`: absolute monotonic
  and cumulative process CPU stamps, phase/parent IDs, durations and loss counts.
  Native R discovery, candidate reduction/construction, seed solve/check, direct
  solve and target check are split. Equivariance recognition and solving are
  separate. Spot classification and deterministic/GFG solving are split. Child
  intervals are subtracted from parent intervals for exclusive totals. Legacy
  and native scopes use one durable stack and common parent IDs; no overlap
  reconciliation is needed. Frozen records retain conservative legacy accounting.
- `work_identity`: SHA-256 of the actual translated formula, length-prefixed
  **ordered** I/O alphabet, provider, translation options and preprocessing options.
  `ACACIA_PHASE_RECORDS_FORMULA=1` adds the exact `spot::str_psl` text as `formula`, writing the identity directly to `PID-final.jsonl` to avoid the pipe packet size limit.
  Native `native_work_identity` uses the stricter full TLSF source identity
  (including declarations/parameters), exact reduction/options and OxiDD provider;
  `accounting_artifact` binds solve/check phases to the exact AAG SHA-256. Similar
  formulas or timings alone never establish identical work. Identities match only
  the same invocation directory, PID, subjob and run ID. A missing packet stays
  unknown. Duplicate groups stay within that invocation and work unit; frozen
  captures supply no inferred identities.
- `parent_record_summary`: final parent loss counter. Interrupt handlers capture a scalar snapshot with signal-safe syscalls before
  immediately killing workers; formatting and final writes run during ordinary
  parent cleanup. Parent worker specs, reap
  usage, recovered boundary stamps and active phase entries are written to
  `PID-final.jsonl`, independently of the best-effort nonblocking writer pipe.
  Failed final writes increment loss and report an error on stderr. The existing
  producer/writer summaries still report dropped packets and destination failures.

Each worker has a fixed 16-entry shared phase stack and five committed resource
boundary slots. These survive worker SIGKILL and are recovered **after reaping**.
Repeated boundaries replace their durable last sample; pipe records retain their
history when delivered. A killed phase has no measured end CPU: its CPU remains
null even when the worker's final `wait4` CPU is known. Record loss makes affected
exclusive residuals unknown rather than zero. Durability here covers worker death
and normal/signal-handled parent cleanup, not power loss or parent SIGKILL.

Obligation recording uses the same `spot::obligation_synthesis` call, arguments,
ordering and early termination as shipping builds. Acacia-side preprocessing and
simplification, the opaque Spot call (`obligation_spot`, wall and process CPU),
and strategy extraction/output are recorded at boundaries Acacia owns. The work
identity hashes the formula, ordered I/O alphabet and options actually passed to
Spot. Spot-internal obligation phases are not split: translation, propositional
preprocessing and SCC/game propagation remain one opaque segment. Recording only
adds observations around existing calls and never selects an alternative driver
or algorithm.

`analyze-prof.py` and `prof_lib.py` accept both frozen and new records. Use the
existing CLI paths (`--rows`, `--panel`, `--phases`, `--output-dir`).
New worker columns include final user/system/self CPU and separate descendant CPU;
`descendants.tsv` contains subprocess records; `identities.tsv` preserves matching
keys. `workers.tsv`, `lifetimes.tsv` and `duplicates.tsv` contain the raw memory,
lifetime and duplication answers; no extra memory/lifetime rollups are generated.
Per-arm summaries also report observed phase CPU divided by final self CPU
when every worker in that group has final usage. Lifetimes include measured
boundary-to-reap CPU and its resolution bounds. `quality.tsv` reports final usage
coverage, parent summary presence, censored phases/descendants and record loss.

A boundary RSS and time until reap do **not** measure a retained-memory integral
or prove that memory remained allocated until reap. Those cells remain NA; there
is no sampling resolution or missed-short-interval guarantee because no interval
sampler is enabled. Declines and stopped optional routes may have useful fallback
work. The first terminal/cancellation boundary identifies useless work; a decline
alone does not. CPU snapshots, hashing, formatting and record I/O have unmeasured
recording overhead. Calibration belongs in a later measurement campaign.

Validation covers real solver verdicts under injected setup failures, fake worker
lifecycles (exit, decline, signals, cancellation, loss and subprocess adoption),
synthetic analyzer records, paired obligation semantic/controller checks and
native observer nesting. The full debug unit suite can be run serially with:

```sh
CARGO_BUILD_JOBS=1 meson compile -C build_scratch/accounting-fix -j 2
meson test -C build_scratch/accounting-fix --suite unit --num-processes 1 --no-rebuild
```
