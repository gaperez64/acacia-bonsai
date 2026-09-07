# Spot OTF sprint completion

Status: implementation and core correctness checks are complete. Isolated
discovery measurements, shipping-portfolio references, isolated held-out checks,
portfolio selection, native worker accounting, and same-provider replay are
complete. Production gates and the full closing comparison remain in progress.
No default has been promoted.

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
- [x] Compare isolated candidate arms on discovery and family-held-out cohorts.
- [ ] Select and validate candidate portfolios within four children.
- [ ] Run the final full-corpus comparison against all current shipping configurations.
- [ ] Publish per-instance results, losses, conflicts, PAR-2, CPU/memory, and decisions.

## Protocol

Timing uses one sequential invocation per systemd scope, a 17-second cap,
8 GiB memory, and zero swap. Full closing runs use a single `--caps 17` pass;
staged discovery results do not substitute for that measurement. Gains, losses,
major claimed speedups, and decisive runs above 13.6 seconds receive three
alternating paired repetitions. The 60-second cap is diagnostic only.

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
unchanged. The `otf_*` presets remain outside the shipping group pending the
production gates and full comparison below.

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
modules passed 553 tests, with one skip; additional campaign tests validate
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

## Shipping references and portfolio selection

On the 36 discovery cases, the frozen mainline contradiction portfolio answers
19 (15 real, four unreal; PAR-2 598.575 s), bboxtree answers 17 (15 real,
two unreal; 675.406 s), semantic MONA answers 12 (ten real, two unreal;
832.419 s), and plain MONA/any answers 12 (ten real, two unreal; 840.915 s).
Their union also contains 19 answers. None answers the AMBA encode or either
robot case reached by the new isolated treatments. The current three-forward-arm
reference answers 15 (13 real, two unreal; 744.149 s). These are measured
portfolios; adding isolated answers does not establish the coverage of a race.

The next selection manifest, frozen before held-out treatments finish, contains
ten four-child races. On the contradiction base, it tests guarded automaton-unreal
(dense and sparse), guarded formula-unreal, guarded on both unreal reductions,
and guarded automaton-unreal combined with a TAA real worker (dense/lazy,
dense/eager, sparse/lazy). The latter combination retains backward and forward
real workers and uses the fourth slot for TAA real. It is also tested on the
bboxtree, semantic MONA, and plain MONA/any component bases. All ten run on both
frozen cohorts. Selection prioritizes decisive coverage, reports every loss and
conflict, and uses PAR-2 to compare equal coverage; small timing differences need
paired repetitions before they support a claim. Formula-unreal TAA remains in
the held-out mechanism checks despite its negative discovery result.

Before the portfolio races began, the held-out formula-unreal guarded control
answered `GF-G-contradiction6`, `GF-G-contradiction7`, and `g-unreal-39`;
guarded automaton-unreal answered only the last. An eleventh race therefore
combines guarded formula-unreal with TAA real on the contradiction base. The
original ten-treatment manifest is retained alongside this recorded extension.
Neither the cohorts nor their membership changed. The held-out cohort informs
portfolio selection after testing transfer from the earlier discovery families;
it is not an untouched validation set for the resulting portfolio choice.
The sparse formula-unreal control subsequently retained those three answers at
12.340, 13.122, and 8.946 s, versus dense 13.627, 14.813, and 10.084 s.
This prompted sparse versions of the both-unreal and formula-unreal/TAA-real
races, bringing selection to 13 portfolios before any of those races began.
These first-pass differences motivate a comparison, not a stable speedup claim.

All 17 held-out treatments finished with zero cross-treatment verdict conflicts,
including comparisons on cases without an annotation. The mainline contradiction
and bboxtree portfolios answer two of 15 (both real), semantic MONA answers none,
and plain MONA/any answers one real case. The mainline union is two answers.

| isolated treatment | real answers | unreal answers | PAR-2 seconds |
|---|---:|---:|---:|
| C0 real | 2 | 0 | 454.717 |
| C3 real | 0 | 0 | 510.000 |
| C3s real | 1 | 0 | 485.880 |
| C0 formula-unreal / automaton-unreal | 0 | 0 | 510.000 each |
| C3 formula-unreal | 0 | 3 | 446.523 |
| C3 automaton-unreal | 0 | 1 | 486.290 |
| C3s formula-unreal | 0 | 3 | 442.407 |
| C3s automaton-unreal | 0 | 3 | 449.893 |
| C4 real | 1 | 0 | 476.980 |
| C5 real | 1 | 0 | 476.995 |
| C4 unreal / C5 unreal | 0 | 0 | 510.000 each |

Sparse automaton-unreal reaches both contradiction cases at 15.295 and 16.699 s,
plus `g-unreal-39` at 9.899 s. Its margins are smaller than sparse formula-unreal's.
Sparse real reaches the already-solved round-robin arbiter at 9.880 s but adds
no answer; dense real loses both C0 real answers on this cohort.
Across both cohorts, the frozen guarded real controls have no new answers.
Their only first-pass improvements of at least 25% over C0 occur on one dense
case or two sparse cases, each with a C0 time below one second. They do not meet
the alternative requirement of five hard completed improvements. The candidate
portfolios therefore retain the two existing real workers.

Both TAA real controls add `amba_decomposed_lock_pb_14_pe_`, which all four
shipping configurations time out on: eager takes 0.980 s and lazy 0.995 s.
Together with the discovery encode case, this supports evaluating a TAA real
portfolio slot in two AMBA families. It does not establish a benefit from lazy
construction. Both TAA unreal controls have seven memory limits and eight
timeouts on held-out cases, with no answer. Worker and same-provider replay
accounting remain necessary before the generation decision.

The extra C0 demand reference now matches P1's outer wrapper without GNU time.
Both answer the same 15 of 36 with zero conflicts. First-pass answer-conditioned
time is 30.599 s for C0 and 34.826 s with instrumentation; PAR-2 is 744.599 versus
748.826 s, and the median paired time ratio is 1.213. Instrumentation is not
free. Three alternating paired repetitions on those same 15 completed cases
confirmed unchanged answers. Answer-conditioned C0/C1 totals were
30.341/34.577 s, 31.289/34.906 s, and 31.598/35.094 s; median paired time
ratios were 1.193, 1.187, and 1.132. All primary performance races use
binaries with diagnostics disabled and no worker capture.

## Actual portfolio discovery results

All 13 four-child races completed discovery with zero verdict conflicts. The
contradiction-base guarded-only variants all answer 21 of 36 (15 real, six
unreal), gaining both robot cases and retaining all 19 mainline answers. The
contradiction-base TAA real combinations all answer 22 (16 real, six unreal),
also gaining AMBA encode. These are actual races, not a virtual union.

| contradiction-base race | answered | gains / losses vs main | PAR-2 seconds |
|---|---:|---:|---:|
| dense guarded automaton-unreal | 21 | 2 / 0 | 537.880 |
| sparse guarded automaton-unreal | 21 | 2 / 0 | 537.229 |
| dense guarded formula-unreal | 21 | 2 / 0 | 543.939 |
| dense guarded both-unreal | 21 | 2 / 0 | 539.597 |
| sparse guarded both-unreal | 21 | 2 / 0 | 536.172 |
| dense guarded automaton-unreal + lazy TAA real | 22 | 3 / 0 | 512.972 |
| dense guarded automaton-unreal + eager TAA real | 22 | 3 / 0 | 512.476 |
| sparse guarded automaton-unreal + lazy TAA real | 22 | 3 / 0 | 511.359 |
| dense guarded formula-unreal + lazy TAA real | 22 | 3 / 0 | 518.947 |
| sparse guarded formula-unreal + lazy TAA real | 22 | 3 / 0 | 517.805 |

The bboxtree, semantic MONA, and plain MONA/any TAA combinations each answer
20 (16 real, four unreal), with three gains and two losses relative to mainline
contradiction. Their PAR-2 scores are 589.625, 585.380, and 589.638 s. They
improve their original component bases but trail the contradiction combinations
on discovery. Actual held-out races remain in progress; none of these numbers
substitutes for the closing full-corpus comparison or paired repetitions.

## Selected configurations

All 13 actual held-out races completed without verdict conflicts or losses
relative to mainline contradiction. Dense guarded-only variants and sparse
automaton-unreal each answer three of 15. Sparse guarded on both unreal
reductions answers four (two real, two unreal), including
`GF-G-contradiction6` at 16.208 s. Most TAA real combinations answer four
(three real, one unreal). The sparse formula-unreal plus lazy TAA real
combination answers five (three real, two unreal), retaining the contradiction
case at 16.500 s as well as both existing real answers and the new AMBA answer.
`GF-G-contradiction7` remains unresolved by every actual portfolio.

The following named presets are candidates with their measured arms compiled
as defaults; their role remains `sweep`, outside the shipping group:

- `otf_sparse`: backward/forward real plus sparse guarded on
  both unreal reductions. Discovery 21/36, held-out 4/15; PAR-2 536.172 and
  418.626 s.
- `otf_taa_lazy`: backward/forward real plus sparse guarded
  formula-unreal and lazy TAA real. Discovery 22/36, held-out 5/15; PAR-2
  517.805 and 385.913 s.
- `otf_taa_eager`: the otherwise identical eager TAA control.
  This exact sparse-formula combination still needs its fresh-default checks.
  The previously measured dense-automaton eager/lazy twins have identical
  coverage on both cohorts and timing differences too small to settle the
  provider choice. Keeping an eager control through closing avoids choosing
  a lazy default from a construction gain alone.

The first two measured races retain all mainline answers on both cohorts.
Their first-pass near-cap gains and slowdowns still require repetitions.
Closing measurements will use these presets' actual defaults with empty
runtime flags, against all four frozen mainline shipping configurations.

The demand driver now accepts an explicit source revision for a frozen binary.
This prevents later repetitions of the original `864be8d5` diagnostics binary
from incorrectly recording the newer checkout revision; its binary hash is
also checked before the paired repetitions.

The isolated `prioritized_arbiter_unreal2_pb_30_pe_` error was reproduced with
the frozen mainline formula-unreal worker: both fail with Spot's 64-acceptance-set
limit. This is an inherited capacity limit. The library limit and binary build
remain fixed for the comparison.

### Native capture accounting

The native diagnostic run contains 66 isolated worker invocations, separate
from primary timings. Several successful guarded jobs pass through multiple K
bounds. The latest capture records the last completed attempt, and metrics from
that attempt can remain present while the next search is incomplete. It must
not be interpreted as whole-job preparation, search, or verification time.

`ACACIA_SPOT_CAPTURE_HISTORY=1`, together with `ACACIA_SPOT_CAPTURE_DIR`, now
retains milestone snapshots in an additional `.history.jsonl` file. Consumers
keep the complete JSON-line prefix if a worker is killed, and deduplicate
`verified-attempt` snapshots by worker record and K. They sum completed-attempt
phase times and take peak maxima across attempts; incomplete jobs provide only
lower bounds. The atomic latest `.json` record remains available. History is
opt-in and used only in explanatory reruns. Closing performance binaries will
be frozen and rebuilt after this accounting change.

The original 66-invocation capture campaign finished at source `e5e4757c`.
Twenty-nine successful worker jobs were then recaptured with history at
`e7061657`. The three selected default builds were rebuilt at that revision;
each passed all 43 unit/version checks. The previous binaries and manifests
are preserved in `_bm-logs.spot-otf-20260907/e5e4757-frozen-builds`.

[Native worker accounting](spot-otf-worker-accounting.tsv) joins each history
to its P1 record using the exact instance and worker PID. These are diagnostic
measurements, including instrumentation, and are not primary speed claims.
For frozen providers, row caches are rebuilt per K; the table does not present
a sum of generated rows as a union of distinct rows. For TAA, the provider cache
and its cumulative counters survive K changes.

| Sparse formula-unreal job | Completed K | Translation ms | Search ms, summed | Verification ms, summed |
|---|---|---:|---:|---:|
| robot-resource-2d16 | 2,5 | 4,326 | 5.2 | 11.2 |
| robot-to-target-charging3 | 2,5,8 | 3,393 | 87.8 | 12.2 |
| GF-G-contradiction6 | 2,5,8,11 | 3,807 | 5,110.7 | 3,409.2 |
| GF-G-contradiction7 | 2,5,8,11 | 3,891 | 5,346.8 | 3,543.7 |
| g-unreal-39 | 2,5,8,11,14 | 1,932 | 4,429.3 | 2,799.6 |

The robot gains remain dominated by translation. The harder contradiction
and g-unreal jobs spend substantial time in both search and independent
verification; reporting only their final K would hide most of that work.

### Same-provider replay and the lazy-generation hypothesis

Eight fresh, independent P6 invocations replay four actual captured worker
formulas: two AMBA wins, the easy `01` regression, and formula-unreal
`full_arbiter_unreal1_pb_2_8_pe_`. The replay consumes the already transformed
bad-language formula, without another negation. The captured transformed AP
partition is reordered into the replay tool's lexical AP order. Both providers
use the same K range and schedule. These runs use a 17-second replay cap,
8 GiB cgroup limit, zero swap, and an additional 8 GiB address-space limit;
they are explanatory checks, not substitutes for native whole-pipeline timing.

[Replay accounting](spot-otf-streaming-accounting.tsv) preserves completed
attempt totals, censored outcomes, provider rows, retained rank payloads, and
process memory. All four completed eager/lazy K comparisons agree and have
verified certificates: both AMBA jobs lose K=2 and win K=5. The other two jobs
are inconclusive, with no invented utilization denominator for the incomplete
full-arbiter enumeration.

| Real job | C4 total wrapper rows | C5 rows after K=2 | C5 rows at verified K=5 | Final utilization |
|---|---:|---:|---:|---:|
| AMBA encode20 | 78 | 53 | 78 | 100% |
| AMBA lock14 | 62 | 61 | 62 | 100% |

The native history puts AMBA encode20's eager enumeration at 7,580.6 ms,
with 213.5 ms search and 46.9 ms verification over both K attempts. Lazy
mode charges generation inside search: 8,014.5 ms search and 48.4 ms
verification. Those single diagnostic runs do not establish a speed winner.
They do establish that both positive examples eventually request every row.
The gains therefore support testing the TAA construction and guarded search
in a real arm; a lazy-generation preference still needs independent evidence.

The native negative captures locate both providers' `lift4` and
`GF-G-contradiction6` formula-unreal failures in the provider factory. The
full-arbiter eager run reaches enumeration, while lazy reaches its first
search without completing it. These phase milestones are censored observations.
The earlier scope journal identifies memory-limit failures separately from
timeouts; an absent counter is not zero work.

### Candidate-only and fallback attribution

Eighteen separate capped invocations compare natural and deliberately limited
candidates. Candidate-only and fallback both solve the two AMBA real positives
and the two selected sparse formula-unreal positives; both time out on TAA
real `01`. A forced zero-row budget returns UNKNOWN in candidate-only mode
for sparse frozen, TAA lazy, and TAA eager workers; each fallback recovers the
existing backward REALIZABLE answer. With fixed K=2, sparse frozen `01`
returns UNKNOWN in both modes and does not invoke fallback. There are no
opposite verdicts. Raw commands, stdout/stderr, captures, and binary provenance
are in `_bm-logs.spot-otf-20260907/fallback-attribution`.

## Production gates in progress

All three selected `e7061657` builds pass the 43 unit/version checks. The
existing frozen G1 gate passes for `otf_sparse` (40/40), and fails for both
TAA configurations (39/40), with zero opposite verdicts. Both lose
`syntcomp24/load_balancer_unreal25.ltl` at 17 seconds.

A fresh matched mainline run on the same 40 logical inputs solves 39/40:
`load_balancer_unreal25` finishes UNREALIZABLE in 12.204 seconds, while
`Morning_f2774e0b` times out. All three new configurations solve the latter.
Thus the TAA portfolio's load-balancer loss is a real tradeoff against current
main, despite equal 39/40 totals. It is not explained by the different old
preset that supplied the frozen expectations. The raw gate reports and exact
matched failure sets are in `_bm-logs.spot-otf-20260907/g1-selected`. The
matched helper uses the existing native-TLSF route for the 2025 portion and
the same vendored LTL inputs as Meson for the 2024 portion.

The G3 panel campaign is running. Each of the three candidates is compared
against the same fresh, byte-identical main reference on each 180-case panel.
The shared-reference file and binary hashes are recorded explicitly; old
panel timings are not substituted. G4's registered labelled set contains
624 cases (461 realizable and 163 unrealizable), at the existing 30-second
correctness timeout. No shipping default is promoted by these partial results.

The first G3 comparison is complete: on the SYNTCOMP25 panel, `otf_sparse`
solves 127/180 versus fresh mainline's 121/180, with six gains, zero losses,
and zero opposite verdicts. PAR-2 improves from 2,186.608 to 1,989.080 seconds;
the existing gate passes. All six gains are UNREALIZABLE: `Morning_f2774e0b`,
`GF-G-contradiction4`, `GF-G-contradiction6`, `ltl2dba_C2_unreal_pb_16_pe_`,
`ltl2dba_theta_pb_14_pe_`, and `robot-resource-2d17`. The GF-G6 and C2 jobs
finish near the cap (16.621 and 15.945 seconds), so their stability still
requires repetitions. Other panel comparisons remain in progress.

The SYNTCOMP26 primary panel gives a counterexample to promoting both guarded
unreal arms: `otf_sparse` solves 139/180 versus main's 140/180. It gains
`robot-to-target-charging6` (7.170 s), but loses `g-unreal-113` (main 7.525 s)
and `workstation_resupply_pb_3_pe_` (main 16.539 s). PAR-2 is 1,458.276
versus 1,457.338 seconds, within the documented noise floor. The first loss
is well away from the cap; the second requires the gate's longer diagnostic
and paired repetitions. Primary 17-second coverage remains 139 versus 140
regardless of a longer diagnostic answer. These findings motivate a targeted
mixed portfolio retaining one old unreal worker before final selection.

### Panel runner failure and matched remeasurement

The original shared-scope lazy TAA SYNTCOMP25 campaign stopped before writing
a complete candidate CSV. Unit `acacia-landing-campaign-378438.scope` hit its
8 GiB limit; the journal records `oom-kill` and termination of the scope that
contained the panel driver. Its 133 logged results are a censored prefix, not
a valid 180-case score. The raw log and the exact journal are preserved.

The existing landing wrapper now supports `--scope-mode instance`, using
`run-subset.py`'s existing per-solver scope support. Each invocation remains
limited to 17 seconds, 8 GiB, and zero swap. The driver survives a solver OOM
and records its resource-limit outcome. Changing modes rejects reuse of old
CSVs; both binaries' panel measurements will be refreshed under this mode.
The previous shared-scope results above remain separate preliminary evidence.
Forty-four relevant Python tests pass, including resource-limit continuation,
unique scopes and bounds per invocation, and rejection of mismatched or
missing scope provenance on resume.

### Which old unreal arm must survive?

Eight isolated 17-second runs attribute the two hard losses using the exact
native SYNTCOMP26 input and the original G1 LTL input respectively:

| Job | Old formula | Old automaton | Sparse guarded formula | Sparse guarded automaton |
|---|---:|---:|---:|---:|
| g-unreal-113 (native) | U, 6.598 s | U, 6.589 s | timeout | timeout |
| load_balancer_unreal25 (G1 LTL) | timeout | U, 9.849 s | timeout | U, 10.141 s |

Either old unreal worker can preserve g-unreal-113. The load-balancer case
needs the automaton transform in this experiment. The resulting focused
selection compares both mixed portfolios on 13 observed gains, losses, and
held-out targets. Both retain backward and forward real workers and exactly
four children; each retains one existing forward unreal worker and uses one
sparse guarded unreal worker. The original larger panels now inform this
selection and are not described as untouched validation of the new mixture.

The two mixed variants each answer 10/13 in the focused pass. The
automaton-guarded mixture adds C2-unreal16, while the formula-guarded mixture
preserves the near-cap workstation real answer. Three alternating repetitions
confirm C2 for automaton-guarded (3/3, 15.634–16.715 s) and not formula-guarded
(0/3). Workstation succeeds 2/3 with automaton-guarded and 3/3 with
formula-guarded. Both mixtures therefore remain under consideration.

The existing real workers were also isolated using **main's exact contradiction
configuration**, avoiding a comparison to the earlier pure-F preset with
different options. On the 17 main-answered real cases from the two selection
cohorts, backward answers 10 and forward 15; their union is 17.
`arbiter_with_cancel_pb_5_pe_` and `collector_v1_pb_9_pe_` require backward
in these capped runs. Seven other cases require forward. Both real workers
are retained. Each existing unreal worker answers all four main-answered
unreal cases in those cohorts; this is a local observation, not a full-corpus
redundancy claim. The G1 load-balancer observation favors retaining the
existing automaton-unreal worker in the TAA-containing mixture.

Four revised actual-default presets are frozen for the fresh gates:

- `otf_sparse_automaton`: B-real, F-real, F-formula-unreal, sparse guarded automaton-unreal.
- `otf_sparse_formula`: B-real, F-real, sparse guarded formula-unreal, F-automaton-unreal.
- `otf_taa_real_lazy`: B-real, F-real, F-automaton-unreal, lazy TAA real.
- `otf_taa_real_eager`: the otherwise identical eager TAA control.

The TAA mixtures now test the TAA addition separately from replacing the
remaining unreal worker with guarded solving. Their fresh gates must establish
whether the retained old worker preserves the wider mainline coverage. The
initial both-guarded and guarded-plus-TAA recipes remain recorded with their
failures; they are not silently relabelled as these revised configurations.
All presets remain outside the shipping group. Solver C++ code is unchanged
from `e7061657`; the next freeze contains configuration, runner, and reporting
changes. The existing diagnostic and earlier performance binaries are retained.
