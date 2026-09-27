# Brief P2c/P3a — orchestration split, retry policy, heavy-manager lifetimes, open warts

Read plan.md §2, §5.4 (all), §6.2, §6.4, §3 Tests (for eligibility), and decisions.md (2026-09-24
entries, esp. "Integration design decided" and the P2a watch items), integration-survey.md §4.
Code: generalize_gr1.py (run, generalize_once, run_unreal_direct, checker invocation/retry),
param-lift-campaign.py (subprocess/deadline, progress/censoring), request.py, buddy_veccompose.py,
native/build.py. Branch sprint/gr1-par2-20260923; I commit. One job; scratch build_scratch/p2c/.
Deliver separately reviewable steps (patch + note each in build_scratch/p2c/):
1. §5.4 orchestration: split into acquire_small_instances / learn_schema / instantiate /
   check_target with one absolute deadline threaded through; seed feasibility/arity analysis before
   building the large target monitor when target-independent; reuse seed games/solutions/schema
   inside the invocation; on CEGIS refinement solve only new seeds; a small probe, if kept, runs
   BEFORE expensive target work and reuses the same schema; a valid exact target certificate is
   sufficient even if the probe is UNKNOWN/CERT_FAILED — evidence distinguishes target_verified vs
   schema_validated_on_probes (never an all-n claim); never add the target as a seed.
2. Retry: remove "restart checker with doubled node capacity on every UNKNOWN". Retry only a
   diagnosed recoverable capacity failure (from the checker's --stats/exit), only if the remaining
   ABSOLUTE deadline and memory headroom allow it; timeouts never trigger a retry. Keep the
   checker's cache cap at its default (node cap) — do not pass --cache-cap yet.
3. §6.2 lifetimes: the supervisor (campaign/wrapper side) holds no BDD manager; the candidate-
   building child owns BuDDy, writes validated artifact paths/hashes and exits BEFORE the checker
   starts; CEGIS rounds hand a schema/artifact bundle across, never live handles. If the existing
   process structure already achieves this, show it with a test and say so.
4. Warts: (a) censored stage inside a long native call must report now - stage_start as its lower
   bound (persist start timestamps, not only elapsed-between-calls); (b) adapter sidecar binds
   content hashes (source sha256 etc.), not absolute paths, so an adapter built in another checkout
   of the same source validates; (c) measure and remove the ~0.15 s per-invocation overhead seen on
   prioritized_arbiter n=7 (1.02 → 1.17 s): profile startup (adapter load/validation, hashing,
   imports) with --diagnostics and python -X importtime on a tiny instance, fix the cause (e.g.
   hash once and cache by (path, size, mtime) with a content check only when changed), report numbers.
5. Eligibility decline order (integration-survey.md §4 "Cheapest fail-closed order"): parameters
   first, bucket capabilities by parameter-name tuple, shortlist before template instantiation;
   final equality contract unchanged. Tests: a non-parametric TLSF declines after exactly one
   tool call (count subprocesses); an unrelated n-parametric TLSF declines without instantiating
   templates of other parameter signatures; all §3 mutation tests still decline.
Semantic equivalence to HEAD on the generalizer suite instances (certificates/policies by BDD
equivalence or both-verify), full suites green, ruff clean. MACHINE MAY BE RUNNING TIMED
MEASUREMENTS: before compiling or running test_generalize_gr1.py / heavy work, wait until the file
named in the launch message is absent or contains a line starting "DONE" (poll every 60 s).
VERDICT at the end with per-step summary.
