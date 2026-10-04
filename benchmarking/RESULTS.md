# Current results and open gaps

This page is the living summary. The [evidence index](evidence-index.tsv) names
the verified archives behind each historical claim. Fetch one with
`python3 -s scripts/acacia-evidence.py fetch --campaign ID --dest DIR`.
The [measurement protocol](README.md) defines caps, gates, and noise floors.

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
