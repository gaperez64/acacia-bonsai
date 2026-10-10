# Current results and open gaps

This page is the living summary. The [evidence index](evidence-index.tsv) names
the verified archives behind each historical claim. Fetch one with
`python3 -s scripts/acacia-evidence.py fetch --campaign ID --dest DIR`.
The [measurement protocol](README.md) defines caps, gates, and noise floors.

## 2026-10-08 — coverage-first P2b: admit the 3 s equivariance default (#224)

Evidence campaign `cov20261007-eqfull` (archived): fresh, matched, rotated
17 s races over all 1,524 SYNTCOMP26 inputs, no invocation deadline, with one
binary for both explicit treatments. `--equivariance-budget 3s` solves **1,206**
versus **1,205** for `unbounded`. The sole gain is `collector_v3_pb_9` at **9.76 s**;
there are **zero losses**, **zero verdict conflicts**, and **identical memory
outcomes**. Paired PAR-2 changes by **−23.3 s**, beyond the **12.1 s** noise floor.

Admit `3s` as the runtime default. Explicit `unbounded` retains the incumbent
behavior; fractions and durations remain selectable. The global allowance stays
in the CLI state rather than adding a compile-time registry option. See the
[allowance protocol](coverage-first/equivariance-budget.md) and
[decision record](coverage-first/decisions.md). These are driver-supplied measured
results; this default change does not rerun the campaign.

## 2026-10-07 — coverage-first screens: negative results (#201, #202, #204, #206, #209)

Screens without a deadline (shipping mode), serial, one frozen binary per comparison,
CPU cooled to 70 °C before each run. Archive `cov20261007-screens`.

- **Native structure guard (P2b).** With the precheck off, the native arm solved none
  of the 138 inputs it stops in the full-corpus race (60 s). Their first stop moved to
  the exact reducer's `mp-class` rejection (103), 60 s timeouts (17) or preparation
  stops; the precheck is a correct, cheap early exit, not the bottleneck.
- **Complementary GR(1) (P4a).** For all 103 `mp-class` inputs the dual game was
  built with move order preserved and the exact reducer rejected it as well; these
  specifications are outside exact GR(1) in both orientations.
- **U lifting (P5).** On the 7 parameterized UNREAL frontier inputs, 4 stop at
  reduction preparation before U and only one enters U.
- **Weakening (P3).** Extending the safety-core pre-pass to assume–guarantee objectives
  and positive G/F scopes makes 41 of the 82 UNREAL frontier inputs eligible, but no
  candidate proves UNREAL at 17 s even with 2 s per attempt and 8 s in total: 181 of
  215 attempts exhaust their allowance and the rest are inconclusive.
- **Variable order (P2a).** On 22 targets the incumbent, typed-interleaved and
  role-grouped orders solve 0/1/1 at 17 s and 1/0/0 with shuffled declarations (the
  same input flipping), and 4/4/4 at 60 s: runtime is order-sensitive, but no
  structural order is a robust improvement.
- **Typed roles in R (P2c).** No change on the 7 targets (0/0 at 17 s, 1/1 at 60 s)
  or on the 20 R-preservation inputs (19/19).

Most of the 136 instances that ltlsynt solves and Acacia misses are not GR(1) in
either orientation, so the remaining gap lies with the Spot-based legacy routes.

## 2026-10-07 — coverage-first P1: matched R × equivariance ablation (#207)

Fresh 17 s races on all 1,524 SYNTCOMP26 instances, serial and rotated, 8 GiB no-swap
scopes, one binary for the four switch legs (COVP1B, `319ee65a`) plus the frozen
`d9d3fd43` incumbent (COVBASE). Zero verdict conflicts and zero invocation errors.
Archive `cov20261006-p1ablation`; P0 exit panels `cov20261006-p0exit`.

| Leg | R pre-pass | Equivariance | REAL | UNREAL | Solved | PAR-2 (s) |
|---|---|---|---:|---:|---:|---:|
| A | off | off | 556 | 648 | 1,204 | 11,752 |
| B | off | on | 555 | 648 | 1,203 | 11,777 |
| C | on | off | 559 | 647 | 1,206 | 11,701 |
| D (shipping) | on | on | 558 | 647 | 1,205 | 11,729 |
| INC (frozen) | on | on | 559 | 647 | 1,206 | 11,711 |

Three-repetition 17 s adjudication reruns (with a 70 °C cooldown before each run;
the host reached 100 °C and throttled heavily during the primary campaign):

- **R stays on.** It adds three REAL solves that never occur with R off (9/9 vs 0/6),
  and costs one near-cap UNREAL solve where seed discovery (~0.8 s) pushes the direct
  route past the cap.
- **Equivariance gives no marginal solve at 17 s** and loses one REAL input in both
  contrasts (ordinary backward ~6 s; equivariance-on 0/9). Paired PAR-2 +24.9 s and
  +27.4 s, above the 12.1 s noise floor. Pursued as a bounded pre-pass budget, not by
  disabling the feature. Removing the backward worker loses one solve on the original
  22-instance activation set.
- **D matches the incumbent.** Its single primary loss was a coin flip at the cap in
  every leg on rerun; the R × equivariance interaction (+2.5 s) is below the noise floor.

Attribution delivery is complete on 1,517–1,519 of 1,524 rows per leg and scope
memory peaks on 7,664/7,664 observations. The refreshed #207 sets equal the closing
campaign's: 136 ltlsynt-only (54 REAL, 82 UNREAL), 76 Acacia-only, against the
historical ltlsynt 2.16 rows (a different session, not a matched comparison). In
fresh D, 138 of 319 unsolved inputs stop first at a native structure-budget guard
(92 of the 136 ltlsynt-only). The safety-core weakening pre-pass is ineligible on
81 of the 82 UNREAL ltlsynt-only inputs because their objective is an implication,
not a top-level conjunction.

## Coverage-first P3 weakening experiment, not admitted

P3 step 2 adds selectable `--weakening basic|extended|off`; basic remains the default.
Basic runs the existing safety-core pre-pass; extended adds assume-guarantee and positive
G/F-scope weakening; off skips the pre-pass. The option rename preserves solver behavior.
The experimental formula UNREAL route retains exact assume-guarantee antecedents, tries lazy
singletons and at most four structural dependency groups, and bounds/reaps each candidate before
original fallback. A checked derived-game proof plus a replayed AST weakening derivation is
required for an original UNREAL verdict. Strict weak-until contexts decline safely.
Limits and proof boundaries are in [the decision record](coverage-first/weakening.md).
The census starvation correction reclassifies the supplied never-started legacy fallback;
no fresh coverage or performance campaign was run. #201 admission/closure remains pending.


P3 step 3 adds absolute research allowances (`--weakening-attempt-ms`, `--weakening-total-ms`),
retaining the 250 ms/1 s no-deadline defaults and existing invocation deadline reservations.
The supplied 82-input, 17 s no-deadline screen produced zero gains: 136 of 149 extended attempts
were cancelled and none proved UNREAL. Read-only source diagnosis of 52 conjunction declines
found two positive `G(A -> conjunction)` shapes and 10 positive F contexts; the planner now
exposes them by direct monotonicity with exact replay, retaining the scope of F. The other 40
remain declined. These are structural and
engineering results; effectiveness and admission still require the driver's fresh matched screen.

## 2026-10-06 — P3 step 1: incumbent weakening instrumentation

The existing UNREAL safety-core pre-pass now records structural eligibility,
generated/started/completed candidates, proof binding, failed-attempt cost,
exception/cancellation, and full-original fallback budget through the #210
transport. Candidate selection/order, the existing 64-child/eight-candidate
thresholds, solver verdicts and deadline behavior are unchanged. The
[attribution notes](coverage-first/attribution.md#existing-unreal-safety-core-pre-pass-p3-step-1)
describe the events and `benchmarking/weakening-census.py`, which retains UNKNOWN
for incomplete telemetry. Review fixes distinguish absent global conjunctions from
threshold rejection, record the largest conjunction size, join proof IDs to actual
attempts, and classify fallback starvation from available budget. Exact cancellation
and generation counts stay UNKNOWN for incomplete census rows, with confirmed
observations reported separately. Generated correctness tests cover the loop and real
CLI proof/fallback behavior. No coverage campaign or performance gain is claimed;
#201's eligibility/budget changes remain future work.

## 2026-10-06 — coverage-first P0 attribution (#210)

Worker lifecycle events and parent-accepted winners now accompany the existing
nonblocking phase records. A versioned tlsf-tools observer records seed discovery,
R, U, direct fallback, and checking before their work runs. Parent snapshots identify
killed workers; producer drops, incomplete lifecycles, and writer delivery failures
are separate from algorithmic declines. Requested/effective backends and original/
transformed proof polarity remain separate.

The debugoptimized correctness build passes all 63 unit tests, including complete
small R/U/direct attribution, interruption at each route/check boundary, cooperative
checker cancellation, late-answer rejection, winner attribution, synthesis coercion,
512-byte packets, slow/broken/full destinations, and original preparation/reduction
contract checks through every native caller. All 182 tlsf-tools functional tests pass;
the two existing integrated metadata-context tests pass separately in their required
contexts. FULL pytest passes 1,900 tests, with 31 skips and 23 passing subtests.

Preparation and reduction expose versioned diagnostic causes independently of their
legacy statuses. Failed alternatives retain integrity causes; trusted-seed and U
game/policy invariants, invalid checker verdicts and corrupted provider contracts
stop with error. Genuine structural inapplicability and completed logical certificate
rejection remain declines. The [generated cause table](coverage-first/decline-causes.md)
now enumerates 734 checks/failure origins with independent per-origin assertions and
a fault/corruption-hook census across all attribution layers. Runtime contract and
cause-conversion panels complement those source assertions; this is not a full
branch-coverage claim. All 15 diagnostics-off behavior comparisons and 72 legacy API
comparisons match the saved review evidence, including 64 common-master API cases.
The charter and owner hardcoding guard remain byte-identical; the generic lifting
guard and release-hook isolation pass.
No new performance or coverage measurements are claimed. [Implementation and reproduction notes](coverage-first/attribution.md)
describe completeness requirements and the cross-repository API change (tlsf-tools#62, merged as `f6b5ecc`).

## 2026-10-06 — P0 gate and memory implementation

The gate corrections for #211 have regression coverage, including real generated
Mealy, Moore and strict-semantics conversions. #214 now uses the existing runners
with an external v2 memory observer and a retained invocation cgroup; reports state
per-statistic completeness and suppress partial medians. Reviewer follow-up keeps
parent journal peaks separate, validates collection provenance, retains master's
primary TSV columns with memory sidecars, preserves signal crashes, and restores
the previous OOM rule for every nonzero exit. The second re-review preserves owner
errors even with a cgroup path and excludes peak/events from failed or unverified drains,
with 20 new regressions. Validation passes 719 pytest tests
(3 optional skips), 52 unit tests, and ruff on 29 touched Python files. This is
implementation validation, not a new performance campaign or issue-closure claim. The real Linux
normal/timeout/child-OOM/invocation-OOM panel passed outside the sandbox on 2026-10-06
(archive `cov20261006-p0exit`). Commands, file ownership and limitations are in
[the P0 decision record](coverage-first/p0-gates-memory.md). Historical rows below
retain their original measured/derived meaning, including incomplete memory evidence.

## 2026-10-04 — P4 closing campaign and shipping default

The 1,524-instance SYNTCOMP26 close measured all five series at 17 s and
derived their 60 s views from validated recycled and rerun rows. The current
first `docker_default` member is `otf_sparse_formula_gr1_real_prepass` after
`b784b4c3`; `otf_sparse_formula` remains in the group. Resolve the live group
through the configuration registry. The older three-way and E5 sections below
record their status **before** this completed close. The full report, the
joined rows, provenance, validation and the cactus plots are in archive
`opt20260927-p4close`.

The 60 s columns are **derived** (see below). Values are solved instances and
PAR-2 totals in seconds.

| Series | 60 s solved | 60 s PAR-2 (s) | 17 s solved | 17 s PAR-2 (s) |
|---|---:|---:|---:|---:|
| ltlsynt 2.16 | 1,286 | 29,758.035 | 1,265 | 9,302.164 |
| Final candidate, 1234G-R | 1,238 | 36,118.463 | 1,205 | 11,652.535 |
| E5 | 1,228 | 37,415.465 | 1,194 | 12,027.355 |
| S4, master's four-arm default | 1,206 | 39,856.456 | 1,177 | 12,548.461 |
| TACAS23 | 847 | 83,039.001 | 811 | 24,788.493 |

Paired against the final candidate, final versus S4 is **+32/−0** solves and
−3,737.993 s PAR-2 at derived 60 s, and **+31/−3** and −895.926 s at measured
17 s. Final versus E5 is **+10/−0** and −1,297.002 s at 60 s, and **+11/−0**
and −374.820 s at 17 s. The three 17 s S4-only solves finished near the cap.
The 17 s PAR-2 differences exceed the measured 12.1 s SYNTCOMP26 noise floor.

For the 60 s view, conclusive cap-independent 17 s rows were reused;
TIMEOUT, UNKNOWN, ERROR and other unverifiable rows were rerun at 60 s. A seeded
64-row validation per series matched all 320 outcomes. The three Acacia races
could be validated only by outcome because legacy arms write no per-arm race
records (#210). Acacia parsed TLSF inside the clock; ltlsynt and TACAS23 used
SyFCo-converted pairs with conversion outside it, and seven conversion
failures per legacy series counted unsolved. TACAS23 predates the fix for
signal-killed workers appearing REALIZABLE, so its solved count is an upper
bound. The gate investigation reclassified G4 as a pass with permitted
timeouts and G5 as a pass with known conversion exceptions (#211).

Native U lifting was **not admitted** (#209): on N4's 64 relevant rows it tied
direct G with no paired gain, and its five answers were slower than direct G.
Other sprint stops were lift policy-export changes and early owner release,
the lift import memo, critical-picker scratch reuse, sparse backward vectors,
backward rank reuse/SIMD, a global Spot acceptance-limit increase, and the
equivariant representative path. The negative decisions and their measurements
are in [decisions.md](optimize-20260927/decisions.md) and archives
`opt20260927-p4n4`, `opt20260927-p4n6`, and `opt20260927-p4gates`.

Frozen binaries: final `binary-FINAL-2d29aa8c` (revision `54032834`), S4
`binary-S4-2e10b4fe` (revision `50384cf6`), E5 `binary-E5-65530fb4`, and
TACAS23 `binary-TACAS23-v1-75fabd3c`. Follow-up work is tracked in issues
#209–#214.

## Latest released three-way

The v2.4.2 full 1,524-case SYNTCOMP26 comparison used a 17 s cap, sequential
8 GiB scopes and zero swap. v2.4.3 added no newer checked three-way.
Archive: `plots-spot-otf-threeway-20260909`.

| Series | Solved | PAR-2 total (s) |
|---|---:|---:|
| ltlsynt 2.15.1.dev | 1,257 | 9,515.826 |
| Acacia `otf_sparse_formula` (arriving) | 1,171 | 12,736.344 |
| Acacia `best_four_arm_contradiction` (departing) | 1,123 | 14,254.294 |
| Acacia virtual best of those two presets | 1,173 | 12,684.211 |
| Acacia 1.x | 811 | 24,785.471 |

The virtual best is a derived per-instance union, not an executable or measured
concurrent race. Acacia 1.x predates the fix for signal-killed workers being
misreported as REALIZABLE; its apparent coverage may be high. The arriving
preset trails ltlsynt by 86 solved instances. The earlier September 5 full
three-way is in `plots-three-way-full-20260905`.

## E5 selection and per-arm legs, unreleased

The selected five-arm **E5** invocation adds `real:gr1:oxidd` to the four
physical arms `real:small:backward`, `real:small:forward`,
`unreal:formula:spot-guarded-sparse`, and `unreal:automaton:forward`.
Its frozen binary is pinned in [baselines.tsv](baselines.tsv). These figures
are the **virtual best of isolated 60 s legs**, not the result of running that
five-arm portfolio on the full corpus:

| Isolated-leg subset | Solved / 1,524 | PAR-2 total (s), rounded |
|---|---:|---:|
| Previous four arms {1,2,3,4} | 1,217 | 38,787 |
| Best four {1,2,3,5} | 1,225 | 38,006 |
| Selected E5 {1,2,3,4,5} | 1,238 | 36,260 |
| Runner-up five {1,2,3,5,6} | 1,235 | 36,789 |

The seven legs' exclusive-solve counts were: arm 1 **15**, arm 2 **17**, arm 3
**99**, arm 4 **10**, arm 5 **16**, arm 6 **7**, arm 7 **1**. Arm 7
(`real:param-lift:oxidd`) returned 19 REALIZABLE, 1,354 UNKNOWN and 151
TIMEOUT on its standalone leg. The deciding reruns left E5 selected. A
50-input concurrent smoke solved 36, matching its isolated-leg prediction.
The owner stopped the subsequent obfuscated full run after 80 rows; those
partial rows were not retained. There is **no completed full-corpus E5 result**,
17 s leg, TACAS23 leg, or joined new three-way. Archives:
`gr1-par2-20260923-perarm-m1`, `gr1-par2-20260923`.

## O5 optimized checkpoint, unreleased

**O5** is the optimization sprint's five-arm development checkpoint. It uses E5's arms and
order, contains the admitted P1 and P2 packages, and is built against Spot 2.16. It was
screened with no 17 s full-corpus leg yet, on a 152-case development screen: 60 s, serial,
8 GiB no-swap.

| Binary | Spot | Solved | PAR-2 total (s) |
|---|---|---:|---:|
| E5 (frozen) | 2.15.1.dev fork runtime | 115 | 4,653.542 |
| C5s216 (C5 source, same Spot as O5) | 2.16 | 116 | 4,589.171 |
| O5 | 2.16 | 116 | 4,543.499 |

- **Verdicts:** no pair has an opposing verdict.
- **O5 against C5s216:** the same solved set.
  - The PAR-2 change comes mostly from two checked-GR(1) rows sped up by the checker-order
    package.
  - One 8 GiB MEMOUT became a timeout.
- **The extra solve over E5 is not credited.** It is cap-sensitive in repeats.
- **Standalone optimized REAL lifting** keeps its 19 development successes.
- **Archives:** the binaries are `binary-O5-1b216a40` and `binary-C5s216-1c3b09ff`. The
  screen rows will be archived with the sprint's evidence.

## Rejected or deferred ideas

- Sparse forward move compaction: a confirmed near-cap regression defeated the
  five-pair gate. Archive `demand-sparse-20260916`.
- Scheduling hints in sparse unreal search: no confirmed deployed gain and a
  validated `g-unreal-115` slowdown. Archive `demand-sparse-20260916`.
- Replacing the real forward arm with real Spot sparse: five gains could not
  offset the systematic workstation loss. Archive `demand-sparse-20260916`.
- Fixed 14-family lifting registry: name/family-dependent selection contaminated
  the evaluation cohort; withdrawn. Archives `param-lift-20260922`,
  `gr1-par2-20260923`.
- Sequential Python lifting as the solver route: withdrawn for the source-bound
  native generic path; Python remains a differential oracle. Archive
  `gr1-par2-20260923`.
- Broad structural witness selector: confirmed regressions at 17 s and 120 s.
  Archive `witness-lifting-20260918`.
- Symbolic-row treatment: no solver change was admitted after confirmation.
  Archive `symbolic-rows-20260917`.
- Semantic whole-letter quotient: G1 gains were outweighed by a severe G2s
  cycle regression. Archive `benchmarking-root-legacy`.

- Optimization sprint P2 packages stopped at pre-registered gates:
  - critical-picker scratch reuse, for no useful gain;
  - sparse backward vectors (#196): 67% of coordinates are non-bottom, and the model reads
    2.56 times as much;
  - backward rank reuse and SIMD: rank work is 5.8% of cycles;
  - raising Spot's acceptance-set limit (#201): the specs need up to 5,052 sets, and
    translation still times out.

  Record: `optimize-20260927/decisions.md`; the archive follows at sprint close.

The current optimization and UNREAL lifting work is tracked in
[the active sprint record](optimize-20260927/plan.md). Results from selected
subsets or censored higher-cap observations retain those labels when cited.

## Legacy worker phase observation

The legacy routes now expose entry, completion and stopped observations through
`ACACIA_PHASE_RECORDS`, including per-subjob/K search and independent verification.
Frozen sparse rebuilt-row demand is sampled after checking. Payload byte estimates
remain distinct from measured process/cgroup memory. The schema and offline readers
are documented in [legacy-phase-records.md](legacy-phase-records.md).

The seeded panel builder refreshes membership from explicit fresh Acacia rows and
historical comparator rows, preserving their different provenance. Instrumentation
and generated-spec correctness checks do not establish a bottleneck, speedup or
performance admission; panel measurements remain the next external-driver step.

The review follow-up caches observation outside action/K loops and returns before
PID, clock, resource or exception sampling when records are disabled. Regression
probes assert zero observation samples. The panel ranks structural/evidence keys
with source-token digest ties, and merged chronology and delivery checks preserve
uncertainty. Censor bounds apply only to the active stage occurrence; captures
require complete invocation identities and matching completed-stage evidence.

Fresh Spot captures can now bind `invocation` through `ACACIA_PHASE_INVOCATION`,
using the same label supplied to the phase table's `--invocation`. The existing
capture snapshot/history path emits the label without additional file operations.
The release producer/CLI regression joins both decomposed frozen sparse subjobs
at K=2 and K=5, rejects a foreign invocation, and checks search/verifier metric
separation. Generated release comparisons retain exact stdout, stderr and exit
codes with records off/on and against the pre-binding executable; records-off
PID/clock/resource-call counts also agree. These remain correctness observations.

## 2026-10-10 — MONA input streaming (#200, pending admission)

MONA now feeds complete input classes into the unchanged sorted action-table
construction and releases decoded sets after each class. Frozen old-code tests
compare tables, action IDs, picker/application traces, RNG state and generated
forward/backward solve results byte-for-byte. Unit tests: 58 passed. Full pytest:
2,210 passed, 40 skipped, 23 subtests passed. The hardcoding guard passed.

Serial 8 GiB/no-swap generated memory diagnostics reduced worker peak RSS from
218.3 to 18.2 MiB (1,024 input paths) and 820.9 to 18.1 MiB (4,096 paths).
Decoded endpoints peaked at 196 KiB in both streaming runs; final action payload
remained 413.3 KiB. The single-arm corpus diagnostic still hit 8 GiB: action
construction stopped at input class 1,320, with 7.914 GiB of retained action
payload and only 2,820 bytes of peak decoded payload estimated. Its complete
table was not built. One incomplete telemetry capture required a corrective
repeat. No timed campaign or admission claim was made.

The implementation, measurement definitions, capture limitation, checks and
reproduction details are in [mona-streaming.md](mona-streaming.md). Raw observations
remain locally under `build_scratch/i200/_bm-logs.memory/`; the driver owns paired
admission and durable publication. No commits were made.

Offline follow-up on the three cooled Morning2s repetitions identifies a real
60 s preparation-phase peak increase from earlier allocation of the required
final action table, while decoding continues. The affected 348-state forward
worker never starts search in either binary; no decoded-buffer leak or table
copy was found. Streaming retains 0.919–1.071 GB of action payload with at most
53.3 MB of decoded payload. Old interrupted decode counts and final per-worker
RSS are unrecorded, so no measured equal-progress RSS claim is made. Details
and same-prefix payload projections are in [mona-streaming.md](mona-streaming.md).
No solver changes or new diagnostic runs were made; admission remains pending.
