# Brief W — the N wrapper executable and the coverage-runner hook

Read benchmarking/gr1-par2-20260923/decisions.md ("Integration design decided") and
integration-survey.md §1, §3 (design B, "Minimal runtime protocol") — they are the spec; plan.md §2,
§9.2, §11.2-11.4; CLAUDE.md. Another codex is editing benchmarking/param-lift-20260922/
generalize_gr1.py right now: do NOT touch any file in benchmarking/param-lift-20260922/. NEVER
stage or commit by any path (including /usr/bin/git). One job; scratch build_scratch/wrapper/.
Deliver:
1. scripts/acacia-lift-portfolio.py (stdlib only, ruff clean): Acacia-compatible CLI —
   `acacia-lift-portfolio.py [wrapper options] -- <B executable> [B args] -T <tlsf>` (or a design
   that keeps B's argv verbatim; justify). Wrapper options: --lift-budget-fraction F (default 1/3)
   or --lift-budget-seconds S; --lift-entry PATH (the lifting route entry point, today
   benchmarking/param-lift-20260922/param-lift-campaign.py in --request-mode source) plus its tool
   configuration pass-through (--tlsf-tools-build, --bindings-python, --bindings-site,
   --buddy-adapter, --real-check); --route-record PATH (also from env ACACIA_ROUTE_RECORD);
   absolute deadline from env ACACIA_OUTER_DEADLINE_MONOTONIC (CLOCK_MONOTONIC seconds) else from
   --cap relative to its own start (documented as less exact).
   Behaviour: read the TLSF once (spool stdin to a private temp file under the wrapper's own
   scratch, removed on exit); run the lifting entry in its own process group with timeout
   min(lift budget, deadline - reserve - now); buffer its output; if it returns a verified decisive
   answer (inspect its evidence JSON: target_verified true and verdict REALIZABLE/UNREALIZABLE — do
   not trust exit code alone) print exactly Acacia's verdict line format and exit 0/1; on decline /
   UNKNOWN / timeout / expected failure kill+reap the group and `os.execv` B with its original argv
   (so B inherits the same cgroup and remaining time; B's own deadline handling is unchanged); a
   wrapper-internal error that prevents fallback exits with a non-contract code. Before exec,
   atomically write the route record JSON (input sha256, capability/binding reason, lift argv,
   exit, elapsed, evidence path/hash, stage censoring, fallback start time and remaining time,
   winner = "lifting" | "fallback-pending"). No nested cgroup; no threads racing B.
   Find out B's exact verdict output format and exit contract from the code
   (benchmarking/benchlib.py normalization, src/) and match it byte-for-byte for the lifting win.
2. benchmarking/run-syntcomp26-coverage.py hook: compute outer_deadline = monotonic now + cap
   immediately before run_systemd_scope and pass ACACIA_OUTER_DEADLINE_MONOTONIC and a unique
   ACACIA_ROUTE_RECORD in the child env (ensure systemd-run --scope passes env; memory: scopes do
   not inherit the caller env — use --setenv or equivalent), and copy selected route-record fields
   (winner, lift_elapsed, fallback_start) into the raw TSV as new trailing columns without changing
   existing columns or export-cactus. Only when a flag like --route-records DIR is given; default
   runs are byte-identical to today.
3. Tests (stdlib unittest, fast, with fake lift entry and fake B scripts): lifting win emits exactly
   B's verdict format and exit; decline → exec B with identical argv/stdin content and remaining
   deadline; lifting timeout kills the whole process group before B starts; lifting evidence
   claiming a verdict without target_verified → fallback; a lying exit code without evidence →
   fallback; outer deadline already passed → exec B immediately; stdin input spooled once;
   route record written atomically; coverage hook default path unchanged (compare argv/env to
   HEAD), hook-on path passes env through systemd-run.
4. scripts/README or a section in benchmarking/gr1-par2-20260923/wrapper.md documenting usage.
VERDICT at end.
