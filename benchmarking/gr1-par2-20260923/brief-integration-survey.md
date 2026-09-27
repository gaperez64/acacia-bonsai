# Brief — integration design survey (READ-ONLY; write only the report)

> Historical research note: the Python wrapper and lifting package below are retained
> only as differential oracles. Current solver runs use native `acacia-bonsai` arms.

Write benchmarking/gr1-par2-20260923/integration-survey.md. Edit nothing else; no compiling, no
solver runs (another codex is editing generalize_gr1.py — don't touch it).
Read plan.md §0, §1.3, §9.2, §10.1, §11 (all), CLAUDE.md, benchmarking/README.md, and the witness-
lifting sprint's decisions (benchmarking/witness-lifting-20260918/decisions.md) for B/S definitions.
Answer with file:line evidence:
1. What exactly is B (the frozen deployed current Acacia configuration): which preset(s)/group
   (python3 scripts/acacia-config.py list-group docker_default), which frozen binary and its sha256
   as committed, how a campaign invokes it (run-syntcomp26-coverage.py / run-subset.py adapters,
   scripts/acacia-bonsai.sh), what input format (LTL via syfco conversion? native TLSF?), and how
   its per-instance deadline/cgroup/memory are applied. Is there already a multi-arm portfolio
   runner (run-portfolio-arms.py etc.) that can host a "route" arm?
2. How TACAS23 v1 and ltlsynt legs are invoked (adapters, conversion route, cached conversion
   timing) and what raw rows/CSVs a three-way uses (export-cactus, cactus-report.py). Where are
   the latest full-corpus legs and at which caps; which host regime/binaries (hashes) they used.
3. Integration options for N = "sound cheap existing routes → source-bound GR(1) lifting route
   under its own sub-budget → original Acacia portfolio (B) fallback": (a) a new route arm inside
   the existing coverage runner's tool-adapter layer; (b) a wrapper executable that the runner
   invokes like any Acacia binary (e.g. benchmarking/gr1-par2-20260923/oracle/acacia-lift-portfolio.py) that does eligibility,
   runs lifting under a sub-budget, then execs B with the remaining absolute deadline, all inside
   the same cgroup; (c) other. For each: how charged time/memory are measured (whole invocation
   incl. children), how UNKNOWN/timeouts/errors map to row categories, how per-route attribution
   is recorded, what changes in which files, and risks (double-counting conversion, cgroup nesting,
   deadline accounting). Recommend one, minimal, reusing existing infrastructure.
4. Eligibility cost: what does source-mode binding (request.py) cost per input on a NON-eligible
   SYNTCOMP26 file (e.g. an arbitrary LTL-only family) — estimate from code which steps run before
   declining (SyFCo calls? template instantiation?) and propose the cheapest sound decline order.
5. Sub-budget policy options for the lifting route at 120 s and at 17 s caps given the measured
   route times in s0/s0-summary-P2a-e2e.md and p0-replay/replay-L0.tsv (describe; don't decide).
6. §10.1 package extraction: propose the concrete module layout (paths) for the maintained lifting
   package given the current files (generalize_gr1.py ~2.8k lines, request.py, tool_config.py,
   s0_diagnostics.py, buddy_veccompose.py, native/, param-lift-campaign.py), what stays as the
   dated forwarding CLI, and which m4-*.tsv files the runtime still reads (plan §10.2).
Be concrete and short; tables welcome.
