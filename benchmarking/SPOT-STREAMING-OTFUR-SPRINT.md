# Spot OTF sprint completion

Status: implementation and validation in progress. No new performance or
coverage claim has been established by this completion run.

## Scope and baseline

The September 7 completion request extends `otf.md`'s default-off scope: test
all useful new mechanisms and polarities against the current best configurations,
select a candidate default portfolio from measured evidence, then compare its
actual race with the mainline shipping configurations on all 1,524 SYNTCOMP26
logical entries. This supersedes the original restriction on changing defaults;
it does not waive correctness or admission gates.

The remote default branch is `master`. Its tip was verified over HTTPS and
frozen at `1c028f13490862bfdbed5256d0e2404ee4d74355`. The baseline worktree is
`/tmp/acacia-otf-main-1c028f13`. All four members of its `docker_default` group
have been rebuilt with the same release compiler profile (including its
`-Ofast -march=native` options), LTO, native TLSF,
the same installed Spot/BuDDy, and their own unchanged options:

- `best_four_arm_contradiction`
- `best_decomp_rank_bucketed_semantic_mona`
- `best_four_arm_bboxtree`
- `best_decomp_mona_any`

Full options and binary hashes are in
`_bm-logs.spot-otf-20260907/main-baselines.json`. The Posets, tlsf-tools, and
benchmark pins agree between mainline and the sprint. The corpus is the existing
materialized `tlsf-corpus`; no benchmark input or dependency is changed.

P7 was recovered without consuming stash
`b37ba53f042b11cd7a991f1fc33e079f75f7fc7a` (`p7-verify`). Its old 43-test result
was a debug build without native TLSF and is historical evidence only.

## Completion checklist

- [x] Restore P7 and freeze/rebuild current mainline shipping baselines.
- [x] Finish provider selection, native worker capture, and sparse frozen control.
- [x] Validate real/formula-unreal eager/lazy transformations and all certificate paths.
- [ ] Run unit/config/version, native TLSF, labelled, frozen, and sanitizer checks.
- [ ] Finish C0/C1 demand, C3/C3s, and real-worker/formula-unreal C4/C5 experiments.
- [ ] Compare candidate arms on discovery and family-held-out cohorts.
- [ ] Select and validate candidate portfolios within four children.
- [ ] Run the final full-corpus comparison against all current shipping configurations.
- [ ] Publish per-instance results, losses, conflicts, PAR-2, CPU/memory, and decisions.

## Protocol

Timing uses one sequential invocation per systemd scope, a 17-second cap,
8 GiB memory, and zero swap. Full closing runs use a single `--caps 17` pass;
staged discovery results do not substitute for that measurement. Gains, losses,
major claimed speedups, and runs above 13.6 seconds receive three alternating
paired repetitions. The 60-second cap is diagnostic only.

No timing campaigns overlap builds or other campaigns. Existing status
exceptions and exact family provenance remain authoritative. An incomplete or
censored graph has no utilization denominator. Fixed-K losses never become
top-level unrealizability answers without the existing worker reduction.

Each hypothesis will receive LAND DEFAULT-OFF ARM / KEEP RESEARCH TOOLING /
STOP / NOT ADMITTED, followed by a separate recommendation about the measured
default configuration. Admission decisions remain pending until the measurements
and gates finish.

## P7 integration

The runtime now accepts per-arm providers in
`--arms polarity:transform:backend[:provider]`. Frozen graphs support backward,
forward, dense guarded, and sparse guarded solving. The TAA eager and lazy
providers support real and formula-unreal guarded workers. Provider defaults
for a polarity fill only arms without an explicit provider; semantic duplicate
arms are rejected after this resolution. The automaton-unreal transform still
requires the existing frozen graph operation.

The sparse frozen control uses the same optimized graph and mixed coordinate
caps as dense guarded solving. The TAA eager and lazy controls share the P5
single-cursor construction and all-numeric sparse engine. Eager mode enumerates
that same provider before search; lazy mode requests rows during search and
verification. Provider rows survive K changes, while ranks, queries, proofs,
and verification state are rebuilt. A verified fixed-K loss remains
inconclusive at the LTL worker level.

Synthesis selects frozen/backward solving for real workers. Explicit fallback
releases a failed TAA attempt and rebuilds the frozen/backward route from the
captured transformed formula; it does not materialize the failed provider.
For frozen guarded failures, fallback reuses the already preprocessed graph.
Neither mode treats Kmax exhaustion as a resource failure. All new compile
gates default off, and enabling compilation alone leaves default arm selection
unchanged. The five `otf_*` presets are research configurations pending the
selection experiments below.

`ACACIA_SPOT_CAPTURE_DIR` enables atomic records at actual worker boundaries,
including the exact transformed formula and partition. Records flush at phase
boundaries and normal destruction. A killed worker's final file can therefore
contain an earlier milestone: its counters are not final censored measurements.
These records are collected in separate diagnostic runs rather than in primary
timing runs.

## Validation and frozen cohorts

The debug unit/version run passed all 45 tests. ASan/UBSan with leak detection
passed all eight relevant suites outside the tracing sandbox (LeakSanitizer
cannot operate under ptrace). Follow-up tests cover forced sparse budgets and
verifier failures. The dense and sparse engines agree on 5,000 generated mixed
rank games; corrupt certificates are rejected. Native Mealy, Moore, and strict
TLSF fixtures compare independently launched eager/lazy workers after the real
and formula-unreal transformations. The Python suite without optional extension
modules passed 526 tests, with one skip; additional campaign tests validate
resource accounting and reject resumes with changed binaries or treatments.

`spot-otf-cohorts/manifest.json` freezes discovery and held-out inputs before
new timing results. Discovery has 36 instances from 24 families: the existing
24 seeds plus their committed solved neighbors. The existing frontier selector
found 15 held-out targets from 13 families after excluding every family in all
82 earlier Spot letter measurements. Missing annotations are handled by the
existing status/exception protocol; they are not assumed to be realizable.

The implementation binaries are frozen at `864be8d5` (binary hashes and full
options in `candidate-builds.json` under the run log directory). The pre-OTF
source tree is byte-identical to the frozen mainline tree; the apparent Git
divergence before P0 consists only of merge commits. The release build with
Spot features disabled passed 43 tests, and Posets passed 18 tests. Release CLI
checks account for compiled-out verbose output and compare C3/C3s under the
same optional contradiction preprocessing.

P1 demand collection is complete: `spot-demand-results.tsv` records 108 workers on
36 instances. Fourteen workers that completed with an answer include 11 partial
row utilizations and three complete utilizations. This is utilization of an already
optimized frozen graph, not a measured translation saving. Incomplete or
portfolio-cancelled workers retain unavailable graph denominators. The compact
table retains each original status; the full unmodified rows, including action
profile IDs, are in the run directory's `spot-demand-raw.tsv`.

## Measurement accounting

Coverage normalization now honors a known memory-limit result before classifying
a negative return code as a crash. Ten eager formula-unreal discovery rows
originally marked CRASH are confirmed 8 GiB OOM kills in the user journal.
The raw rows are preserved; separate annotated TSVs recover final scope CPU,
memory, swap, and OOM accounting. The journal join prefers an exact scope ID;
older rows require matching solver arguments, input, and a unique end timestamp.
Missing or ambiguous accounting remains unavailable, and an actual zero stays
distinguishable from a missing value.

Comparisons charge every nondecisive result the full PAR-2 penalty, including
fast UNKNOWN and memory limits. Resource totals include observation counts.
The comparison tool rejects different cohorts and repeated instances, so staged
caps or repetitions cannot silently replace primary measurements.

The complete 22-treatment discovery sweep has zero cross-treatment verdict
disagreements. On either translation preference, C0 real answers 13 cases;
dense and sparse guarded real each answer seven (zero gains, six losses).
Formula-unreal rises from one answer to three, and automaton-unreal from two
to four, for both guarded representations. Both gains are the robot-resource
and robot-to-target-charging cases. Eager and lazy TAA real each answer the
same AMBA encode case, while losing all 13 C0 real answers in isolation.
Neither TAA unreal arm answers a case; eager has ten and lazy nine confirmed
OOM kills. Preferences `small` and `any` have identical coverage for every
matched treatment on this cohort. These are single-pass isolated controls,
before shipping-portfolio comparisons or admission decisions.

The Python suite after the accounting changes passed 553 tests with one skip;
the rebuilt debug suite again passed 45 tests. Future coverage rows record the
scope ID directly, and validated resumes can upgrade the previous header
without rerunning or inventing resource measurements. Frozen guarded capture
now includes preparation time and avoids formatting capture values when capture
is disabled. The 22 control binaries remain frozen at `864be8d5`; this later
diagnostic change does not rewrite or replace their measurements.
