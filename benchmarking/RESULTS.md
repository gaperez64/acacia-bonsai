# Current results and open gaps

This page is the living summary. The [evidence index](evidence-index.tsv) names
the verified archives behind each historical claim. Fetch one with
`python3 -s scripts/acacia-evidence.py fetch --campaign ID --dest DIR`.
The [measurement protocol](README.md) defines caps, gates, and noise floors.

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
