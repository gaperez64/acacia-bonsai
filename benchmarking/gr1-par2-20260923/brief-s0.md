# Brief S0 — one diagnostic instrument for P1/P2 targets (no optimisation)

Read plan.md §1.2, §5A S0, §6.4, §11.1 step 2. Branch sprint/gr1-par2-20260923; I commit. No
compiling. Python via .venv/bin; ruff.
Add an opt-in diagnostic mode to benchmarking/param-lift-20260922/generalize_gr1.py and
param-lift-campaign.py (flag --diagnostics PATH writing one JSON per invocation; OFF by default and
zero-cost when off — no per-call timers or counters on the hot path unless enabled; guard with a
single module-level boolean). Record:
- wall time per phase: seed monitor construction, seed solves, projection metadata, BDD
  projection/relabel, support extraction, variable-cube construction, substitute_variables (count of
  bdd_compose calls and total time), from_aag (count, gates traversed, time), instantiate_templates,
  policy construction/Skolemization, export (BDD→AIG walks: count, nodes visited), target check
  (subprocess time), plus an explicit censored-lower-bound record for any stage still open at
  cancellation/deadline (plan §6.4);
- S0 distributions: actual variable-support width per projected root, client count, ownership-tuple
  arity histogram, number of distinct subsets S and reuse count per subset, the word-length histogram
  a uint64 variable mask would have, mode count;
- BuDDy stats at stage boundaries if the binding exposes them (allocated/used nodes, gbc count);
- the invoking environment: interpreter, binding site/.so path, tlsf-tools build dir and binary
  sha256, compiler flags are NOT needed here.
Also record for the checker subprocess its --stats JSON if the configured tlsfcertcheck supports
--stats (newer build) — detect by probing `--help`, don't fail when absent.
The campaign already measures the whole invocation; make sure it records cgroup memory.peak when run
under systemd-run (read /sys/fs/cgroup/<own cgroup>/memory.peak at the end of the child from the
parent via the scope's cgroup path, or document how the caller supplies it). If the existing campaign
already records it, just point to it.
Tests: diagnostics off → artifacts byte-identical to diagnostics on (except the diagnostics file);
the JSON schema validates on a tiny instance; a cancelled stage produces a censored record.
Also write benchmarking/gr1-par2-20260923/s0/README.md with the exact serialized, cgroup-wrapped
commands (systemd-run --user --scope -p MemoryMax=8G -p MemorySwapMax=0, 120 s budget,
--tlsf-tools-build subprojects/tlsf-tools/build-L0, reproducer mode) that capture the diagnostic on
the §1.2 target list: arbiter_with_cancel n=8,9,10; load_balancer_unreal2 n=6,7; load_balancer
n=8,9; amba_decomposed_lock n=15; arbiter_with_buffer n=8,9; arbiter_on_inpchange n=6,7;
round_robin_arbiter_unreal2 n=5,6,7; collector_v1 n=11; plus controls arbiter n=6 and
prioritized_arbiter n=7. Output under benchmarking/gr1-par2-20260923/s0/raw/.
Finish with a VERDICT.
