# Brief CA — run the lifting/direct route as a concurrent arm beside B

> Historical research note: the Python wrapper and lifting package below are retained
> only as differential oracles. Current solver runs use native `acacia-bonsai` arms.

Read the last decisions.md entry, benchmarking/gr1-par2-20260923/oracle/acacia-lift-portfolio.py (current sequential design:
lift slice then execv B), wrapper.md, benchmarking/benchlib.py normalisation of Acacia output and
exit codes, and benchmarking/gr1-par2-20260923/oracle/acacia_lift/lifting/settings.py. NEVER stage or commit by any path. Do not
edit benchmarking/gr1-par2-20260923/campaign/ (another task works there). One job; scratch
build_scratch/concurrent/; never /tmp.
Change the wrapper so that, after the (unchanged, bounded) eligibility step, it starts TWO children
concurrently in their own process groups within the same cgroup (no nested cgroup): (a) B with its
exact original argv, stdin content and environment, and the full remaining deadline; (b) the
lifting/direct route (python -m acacia_lift.runner) with the full remaining deadline. The first
acceptable result wins: B's result is whatever B prints/exits (relay B's stdout/stderr and exit code
EXACTLY, byte for byte, as if B had run alone — capture and replay, or pass through only once B is
known to be the winner; design so no partial interleaved output ever reaches stdout); the route's
result is accepted only under the existing rules (verified, hash-bound, side-licensed evidence) and
is printed in Acacia's exact verdict format. A route decline/UNKNOWN/timeout just lets B continue.
If B finishes with UNKNOWN/error first, keep waiting for the route until the deadline; symmetric
for the route. On a winner, kill and reap the other group completely (reuse the existing
subreaper/kill-and-confirm logic) before exiting. Non-parametric, non-reducible inputs: skip the
route entirely (B alone, as before; no extra process). Add a global setting for the route arm's
memory ceiling (BDD node caps / checker caps already in settings.py — document how they bound the
arm's peak memory; add an RLIMIT_AS-style guard only if safe for Python+native code, else say
why not). Route record: winner = B | lifted-certified | direct-certified | none, both arms' elapsed,
exit and peak memory if available. Tests (stdlib/fake children): B wins while route still running
(route group killed); route wins while B running (B group killed; output is exactly one verdict
line); both fail -> B's non-answer relayed; B output byte-identical to B-alone for a set of fake
outputs incl. large stdout and stderr; stdin spooled once and given to both; deadline respected;
no orphan processes (grandchildren ignoring SIGTERM). Update wrapper.md. Run tests/pytest serially;
ruff. VERDICT at end.

## Addendum (owner): arm selection, including lifting only
Structure the wrapper around a small set of named ARMS instead of special cases: `b` (the frozen
Acacia with its original argv), `direct` (exact GR(1) solve + certificate check of the actual input,
Stage A) and `lift` (parametric lifting, Stage C; only for inputs with PARAMETERS). Add
`--arms LIST` (comma-separated; default `b,direct,lift`) so that, e.g., `--arms lift` tries ONLY
parameter lifting (no B, no direct: a declined/failed lift reports UNKNOWN in Acacia's format and
exit code), `--arms direct,lift` runs the certified routes without B, and `--arms b` is B alone.
Run the selected arms concurrently under the rules above (first acceptable answer wins; B's answer
as-is; certified arms only on verified, hash-bound, side-licensed evidence). If `direct` and `lift`
share a process today, split them into separately selectable arms (they may still share the input
snapshot/binding), or explain why a shared process with internal selection is cleaner. Record the
selected arms and per-arm outcomes in the route record. Tests cover `--arms lift`, `--arms b`,
`--arms direct,lift`, the default, and an invalid list.
