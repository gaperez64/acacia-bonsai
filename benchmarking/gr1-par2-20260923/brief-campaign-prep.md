# Brief C — eligibility census and campaign commands (no timed runs by you)

Read decisions.md (all 2026-09-24 entries), integration-survey.md §1-3, wrapper.md,
plan.md §9.2, §11 (all). The lifting package is scripts/acacia_lift/ (P9, just committed); the
wrapper is scripts/acacia-lift-portfolio.py; the coverage runner is
benchmarking/run-syntcomp26-coverage.py (with --route-records). NEVER stage or commit by any path.
No compiling; no solver campaigns; you may run the eligibility step itself (it only calls
tlsfinfo/tlsf2tlsf/tlsf2ltl) serially.
1. Census: a script benchmarking/gr1-par2-20260923/campaign/eligibility-census.py that, for every
   ID in the established 1,524-ID SYNTCOMP26 list and its TLSF source map (find them from
   run-syntcomp26-coverage.py's defaults), runs EXACTLY the wrapper's eligibility/binding code
   (import it; do not reimplement) against the configured tlsf-tools build
   /home/gperez/GIT-repos/tlsf-tools/build-P5-9212e2b and writes eligibility.tsv: id, source sha256,
   decision (eligible/decline), capability, parameters, route kind, real_check, decline reason,
   number of tool calls, elapsed. Never consults any status/verdict column. Run it (serially) and
   commit-ready the TSV plus a short summary (counts per capability, decline reasons, eligibility
   cost distribution for eligible vs declined inputs).
2. Selection campaign commands (campaign/README.md): exact run-syntcomp26-coverage.py invocations
   (uniform cap, one row per ID, --route-records, 8 GiB / no swap, serial) that run N-a (lift
   budget cap/3) and N-b (2·cap/3) on EXACTLY the eligible IDs at 120 s and at 17 s, using the
   wrapper around the frozen B `build_w1_B/src/acacia-bonsai` (sha256 398a420b…; verify), the
   tlsf-tools build above, the adapter to be supplied as ADAPTER=<path> variable, bindings as
   today. Output directories under benchmarking/gr1-par2-20260923/campaign/selection/{120s,17s}/
   {N-a,N-b}/. Also the comparison procedure: join each to the existing B rows for the same IDs from
   benchmarking/witness-lifting-20260918/opening/{120s,17s}/epoch-1 (and epoch-2 to show B's own
   repeatability), using benchlib.par2 and the existing normalisation; report per-variant solved,
   PAR-2 on the eligible subset, gains/losses vs B per ID, route winner counts from route records.
   Write that comparison as campaign/compare-selection.py (reuse benchlib; no new framework).
3. Full-corpus commands (campaign/README.md, second section), parameterised by the chosen variant:
   N at 120 s and 17 s over all 1,524 IDs with --route-records; TACAS23 v1 at 120 s via the existing
   run-subset.py acacia1x adapter with the frozen binary
   _bm-logs.fmcad26-head-6dda2f3b-20260822/build/acacia-v1-best23/src/acacia-bonsai (sha256
   75fabd3c…; verify) and its existing conversion pairs; how to export-cactus each leg; the exact
   cactus-report.py three-way invocations for CAP=120 and CAP=17 per plan §11.4 with
   ltlsynt legs = benchmarking/witness-lifting-20260918/opening/{120s,17s}/ltlsynt-cap*.csv and
   TACAS23 17 s = benchmarking/plots/three-way-full-20260905/syntcomp26-full-v1.csv (check its
   provenance matches the frozen binary/flags; if not, say so and give the 17 s rerun command).
   Estimate wall time of each leg from the existing B/ltlsynt/v1 rows (sum of min(time, cap)).
Finish with VERDICT: census counts, anything that doesn't line up, and time estimates.
