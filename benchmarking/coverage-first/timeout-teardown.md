# Timeout teardown and attribution

The 180-row P0 evidence panel used `wt-p1-ablation` at `afa76d10`. Its older
`benchlib.run_systemd_scope` waits for the `systemd-run` client at the cap, then
calls `systemctl --user stop` on the whole scope. `KillMode=control-group` makes
that a scope-wide termination request, including the solver parent, its workers,
and its record writer. The stop helper checks scope state and escalates through
`systemctl --user kill --signal=SIGKILL` if needed. The panel did not capture
which processes actually received which signals; that causal attribution remains
an inference from the code and missing records, not a captured signal trace.

The current gates branch instead retains a delegated scope with a lifecycle owner
in its `observer` subgroup and the solver and descendants in `invocation`. Before
this repair, the runner's timeout callback touched `cancel`; the owner immediately
wrote `cgroup.kill`, killing the parent, workers and writer together. After reaping
the invocation leader it killed remaining descendants, drained the cgroup, published
`ready.json`, and waited for the external memory reader's `ack` before removal.
Retaining memory was correct, but immediate killing interrupted solver teardown.

The timer and verdict boundary stay separate from cleanup. `run_process_group`
starts its elapsed clock before `Popen`; `communicate(timeout=cap)` or
`wait(timeout=cap)` enforces the existing launcher wait cap. On expiry it latches
`timed_out`, invokes the scope's cancellation callback, and returns exit 124 even
if the solver subsequently prints a verdict and exits successfully. `seconds` is
still elapsed wall time through launcher/output collection, including timeout
cleanup, before the final process-group and scope sweeps. Unsolved PAR-2 remains
`2 * cap`; cleanup time is not an extra solve budget or PAR-2 charge.

Route-attributed launches also pass the solver an absolute monotonic acceptance
deadline, computed immediately before scope launch. The solver checks it before
launching each arm and before accepting a reaped child. At expiry it kills every
known worker process group and PID with SIGKILL, reaps them, emits parent terminals
(`deadline` or `deadline_rejected`), and exits UNKNOWN. SIGTERM and SIGINT already
use an async-signal-safe handler: flag interruption and kill known worker groups
and PIDs; ordinary parent code reaps and emits terminals (`interrupted` or
`interrupted_rejected`). The handler now also preserves errno across its kill calls.
Child signal handlers use `_exit(UNKNOWN)`; the writer was forked before those
handlers and must never receive the initial scope-wide TERM.

On parent exit the record hook emits its `record_summary`, closes the nonblocking
producer pipe, and waits up to 100 ms for writer EOF/drain, then up to 10 ms to reap
a forcibly stopped writer. The writer alone writes JSONL and its final
`writer_summary`. Killed workers legitimately lack their own terminal/summary;
each must still be named by the parent's terminal with SIGKILL and incomplete
producer telemetry. Complete delivery does not assert complete killed producers.

The repaired cancellation protocol publishes one absolute cleanup deadline and
sends SIGTERM only to the invocation's unreaped main PID. The global allowance is
**500 ms**, chosen to cover the existing 110 ms writer closure bound plus worker
kill/reap and scheduling slack. It is fixed for all workloads, not fitted to panel
outcomes. Scheduling delay consumes this allowance; repeated cancellation cannot
restart it. If the parent finishes sooner, remaining descendants are reclaimed
immediately. At the deadline the owner writes `cgroup.kill`. Existing final drain,
peak/events publication, external read, acknowledgement, removal, and scope sweep
remain in that order. The three-second drain/observer waits are collection bounds
after cancellation, not extensions of the 500 ms opportunity for live solving.
Malformed/legacy cancellation markers permit no extra cleanup allowance.

Linux resource collection now uses the owner's wait4 counters, including the
solver's reaped children, for CPU seconds and maximum process RSS. This removes
GNU time as an invocation leader when `--collect-rusage` is enabled: the TERM
recipient is the solver, and RSS provenance remains wait4 rather than scope peak.
Other platforms retain the existing GNU time fallback. Counter precision now
comes from wait4 directly, rather than GNU time's formatted output. The raw TSV
and memory sidecar schemas, cap, verdict classifications, and PAR-2 are unchanged.

Pytest covers normal and hung cancellation, scheduling delay and repeated requests,
late printed answers in all capture modes, and collection before deletion. With
`ACACIA_ATTRIBUTION_TEST_BUILD` pointing to a checked attribution build, the runner
regression launches the real five-worker portfolio fixture through the real owner,
replacing only systemd launch and cgroup filesystem operations. The saved row must
be TIMEOUT with five parent terminals, parent summary, a loss-free writer summary,
and no winner. Attribution pytest and the Meson unit panel independently exercise
five stalled workers with deadline, SIGTERM and SIGINT, plus diagnostics-off parity
and rejection of a reaped answer after the absolute deadline.

## Fresh real panel outside the sandbox

Run the script left at `build_scratch/timeout-teardown/rerun-small-panel.sh` in this
worktree. It uses the repaired gates runner and the checked attribution binary,
selects the first four original TIMEOUT rows and first two solved controls from
`p0exit/D.tsv`, and creates a fresh six-row, 17-second, 8 GiB/no-swap panel with
phase and route records. No recorded rows are resumed or recycled. The preset
label is resolved from `docker_default`; the checked attribution build uses those
five arms. These are delivery correctness observations, not performance admission
measurements. The binary digest and both uncommitted source diffs are saved beside
the new rows. It invokes the existing `wt-p1-ablation` completeness reader and
requires complete delivery and authoritative peak/events for all six rows, while
keeping producer incompleteness explicit. Run only after the driver has ensured
campaign exclusivity.

```sh
bash /home/gperez/GIT-repos/acacia-bonsai/build_scratch/cov/wt-p0-gates/build_scratch/timeout-teardown/rerun-small-panel.sh
```

Expected gate: every newly timed-out row has exactly one parent terminal per
spawned worker, the parent summary and a matching loss-free writer summary, no
accepted winner, and valid memory.peak/memory.events provenance. A broken destination
or uninterruptible parent/writer still cannot be reported as complete delivery;
the fixed hard-kill bound intentionally does not promise telemetry from a hung
process. No real systemd or benchmark run was performed in the sandbox.
