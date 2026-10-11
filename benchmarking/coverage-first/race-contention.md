# Single-input portfolio attribution (#202)

`benchmarking/diagnose-portfolio.py` accepts exactly one TLSF input. It reuses
`benchlib.run_systemd_scope`: every race or isolated arm gets a fresh workload
cgroup with an 8 GiB memory maximum and zero swap. The external memory observer
keeps the workload cgroup available after normal exit, timeout, or OOM. No list
runner or timed campaign is added. The `all` mode runs the shipping race followed
by each shipping arm strictly sequentially. Membership comes from the built
`.acacia-config.json`; observed worker specifications are checked against it. A lock excludes
another copy in the same worktree, and the existing scope guard refuses to start
with an active `acacia-*` workload. The guard also runs between individual arms.

Use the current Docker group to resolve the build configuration, then run:

```sh
python3 benchmarking/diagnose-portfolio.py run \
  --binary BUILD/src/acacia-bonsai \
  --build-config BUILD/.acacia-config.json \
  --input INPUT.tlsf --cap 60 --output _bm-logs.attribution/INPUT
```

`--mode race` and `--mode standalone` select only that part of the diagnostic.
`--cpu-quota` optionally supplies the same scope CPU quota for every invocation;
the default preserves the supplied campaigns' unrestricted CPU setting. No CPU
affinity is changed. Observed cgroup memory/swap/CPU constraints, available CPUs,
binary/input digests, initial/final host load, temperatures and frequencies are
retained when available. Unreadable temperature/frequency files remain absent.

Each invocation writes:

* `phases/`: the original nonblocking worker/route/stage records, including
  parent terminal records and accepted winner metadata.
* `samples.jsonl`: process-tree RSS, cumulative CPU tick deltas, fraction of
  observed arm CPU consumption, process/thread counts and last CPU assignment,
  sampled every 100 ms by default from `/proc` outside the workload.
* `events.jsonl`: phase records annotated with their emitting PID.
* `summary.json`: result, which resource ended the invocation, authoritative
  cgroup peak/OOM counters with collection sources, and per-arm exit/decline
  times, sampled peaks, CPU work and thread counts.
* `stdout.log` and `stderr.log`: original solver output and diagnostics.

CPU share is the fraction of observed arm CPU consumption during a sample, not
an entitlement to that fraction of the machine. Mean cores is sampled CPU work
divided by worker lifetime. CPU work is a lower bound because work after the
last sample and short-lived descendants may be missed. An arm that exits between
samples has null CPU/RSS sample fields and an explicit missing reason; existing
phase CPU/peak-RSS records provide additional lower bounds. Summed process RSS
double-counts shared pages and is never substituted for concurrent cgroup memory.
A route decline may fall back within the same arm: compare its decline timestamp
to the parent terminal record before concluding that a core was released.
Killed arms can have incomplete terminal telemetry; that is separate from a
recorded decline. Missing memory/events or observer failure aborts further runs.

The offline `select` command joins a uniform closing race table with explicitly
supplied standalone tables using their recorded input identity. It ranks actual
unsolved-race/solved-standalone pairs first, then the largest recorded race or
fastest standalone wall cost, then slowdown. It records source paths and selected
rows; it never selects solver behavior. Duplicate race identities are rejected.

```sh
python3 benchmarking/diagnose-portfolio.py select \
  --race-rows CLOSING.tsv --arm-rows ARM1.tsv ARM2.tsv ARM3.tsv ARM4.tsv \
  --limit 3 --output _bm-logs.attribution/selection.json
```

These are attribution diagnostics, not performance admission. An isolated arm's
answer does not establish a new race answer. If no race loss or specific budget
obstruction is reproduced, do not change budgets or scheduling on speculation.
Resource/default changes still require the driver's matched paired race screens
and no-loss/no-new-OOM gate from the charter.

Verification on 10 October 2026: the shipping-option checked build's unit suite
passed 70/70; full pytest passed 2218 tests, with 40 skips and 23 subtests; the
separate hardcoding guard passed 96 tests with one skip. The eight new harness
tests cover terminal/decline separation, resource attribution, process-tree
parsing, partial records including UTF-8, identity joins, duplicate race rows
and observed worker membership. Ruff and configuration checks passed. A final
scoped correctness fixture exercised the finished harness's six invocation
paths. Solver and tlsf-tools source remain unchanged.

## Opt-in stage scheduling

`--stage-concurrency off|COUNT` bounds concurrent memory-heavy stages across the
existing arms. The default is `off`; a build may set its default with
`-Dacacia_stage_concurrency=COUNT` (zero means off). All shipping presets retain
zero. This changes admission only: worker membership, allocation budgets, proof
binding, independent checking, cancellation and absolute deadlines are retained.

The existing translation, reduction, input/action construction, BDD and antichain
stage markers acquire a lease. Unclassified computation is conservatively heavy.
Startup, source/proof binding, completed proofs, summary and teardown markers are
unrestricted. `target_check`, `independent_check`, `certificate-check` and
`verification` mark independent checks. Telemetry-only inapplicable phases do not
request leases. The scheduling channel is lock-free shared state, independent of
the nonblocking diagnostic pipe and available with diagnostics disabled.

The global lease is three seconds, chosen as a responsiveness/fairness allowance,
not from workload timings. A newly requested independent check can receive one
exclusive three-second slice. Aging overrides priority: a continuously waiting
arm is selected within the current live heavy-arm count times three seconds,
plus the existing parent polling interval (at most 10 ms) and operating-system
stop acknowledgment. After priority expires, a long check joins the ordinary
rotation. Declines and exits release their leases at the next parent observation;
a fallback in the same arm must request another stage. No instance names, formula
fingerprints, learned state or per-family limits influence scheduling.

A worker gates entry before memory-heavy work. The parent uses `SIGSTOP` on whole
worker process groups to preempt an admitted long stage and `SIGCONT` to resume
it, retaining its memory. It acknowledges stops before issuing replacement grants;
request generations prevent an old grant from admitting a new stage. Forked
weakening attempts inherit the supervisor's process-group lease and do not write
its scheduling slot. The existing group-wide `SIGKILL` cleanup also kills stopped
workers; waiting does not extend any deadline or create a verdict.

The single-input harness accepts `--stage-concurrency COUNT` and includes it in
every recorded solver command. The initial diagnostic allowance is **two** heavy
stages, chosen globally before observations to limit competition while retaining
ordinary parallel progress. Use `--mode race --cap 60` for each separate diagnostic.
Matched full-race coverage and memory screens remain the driver's admission gate.
