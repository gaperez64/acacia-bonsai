# Rejected optimization experiments

These measurements are the durable record of explored ideas that did not meet
the landing gates.  G1 is the frozen 40-verdict regression gate, G2 the
advisory Posets microbenchmark, G2s the solver-profile proxy, and G3 the
per-instance landing bar.

- **Wide SIMD accumulation:** horizontal `int` reductions regressed the pinned CPre phase by 0.68% (10,568,578,870 to 10,640,514,908 cycles); rejected by G2.
- **One-way domination:** `dominated_by` regressed query by 1.22% and SIMD by 6.22%; rejected by G2.
- **Dominated-x intersection precheck:** intersection improved only 3.57%, below the fixed 5% target; rejected by G2.
- **Reusable shape-A meet storage:** intersection improved only 1.72%, below the fixed 5% target; rejected by G2.
- **CSR action tables:** gained `collector_v215.ltl` but lost `amba_decomposed_arbiter6.ltl` from a 2.045 s answer to timeout; rejected by G3.
- **Split forward/backward apply kernels:** PAR-2 moved 232.071 to 232.147 s on the 94-case critical panel; rejected before its parent CSR candidate failed G3.
- **Threshold and buffer hoists:** the Posets threshold changed target phases by at most 0.68%, while picker/CPre buffer reuse improved PAR-2 only 0.89% (232.071 to 229.995 s); rejected by G2/G2s.
- **Arena/SoA antichains:** isolated kernels improved 25--59%, but the integrated solver lost six G1 sentinels; rejected by G1.
- **Batched CPre:** pre-reducing the predecessor cloud lost four frozen answers to timeout; rejected by G1.
- **Cached vector sums:** coverage was unchanged but panel PAR-2 regressed 2878.480 to 2889.942 s and the cached-sum kernel regressed every measured hot phase; rejected by G2.
- **AVX-512 VPOPCNTDQ:** median CPre improved 8.50%, but intersection regressed 2.00%; shelved before the solver gate on Posets tag `archive/avx512-vpopcnt`.
- **Rebuilt Step-1 stack:** 2025 PAR-2 improved 2811.292 to 2783.750 s at unchanged 105/180 coverage, but `syntcomp25/load_balancer_unreal2_pb_5_pe_.ltl` moved from an 11.559 s UNREALIZABLE answer to a 17.038 s timeout.  The baseline is below 80% of the 17 s cap, so this is a genuine roughly 47% regression; rejected by G3 and reverted from Posets `main`.
- **Critical-picker portfolio:** gained no coverage and lost `collector_v215.ltl`, reducing 2024 coverage 99/174 to 98/174 and critical coverage 90/94 to 89/94; rejected by G3.
- **Step-3 cap census:** every one of 124,385 observed states had `cap[q] = K`, so 0% received a useful finite cap against a 25% threshold; stopped before implementation.
- **`surely_losing` isolation:** `syntcomp25/infinite-race-u4.ltl` moved from a 10.58 s UNREALIZABLE answer to timeout; rejected by G1.
- **Direct simulation:** both 2024 and 2025 `load_balancer7` variants lost their REALIZABLE answers, including an isolated 10.72 s answer becoming a 17.03 s timeout; rejected by G1.
- **Step A alphabet DAG collapse:** the median `paths/nodes` ratio was 1.13 against the fixed 4.0 threshold (1.26 even if every missing descent is infinite); **STOP BEFORE STEP B**.
- **Equivariant minimum clients 3 → 2:** G1 passed 40/40, but the existing block-layout proof conservatively rejects two-client groups because one transposition cannot recover unique slot identities. Coverage was unchanged on both landing panels: SYNTCOMP25 stayed 106/180 (PAR-2 2799.825 → 2797.068 s) and SYNTCOMP26 stayed 134/180 (1712.260 → 1713.968 s). G3 passed, but the relaxation gained no usable solver admissions or answers; rejected.
- **Equivariant maximum states 512 → 2048:** G1 passed 40/40 and the critical screen stayed 91/94 (114.7 → 106.2 s), but neither landing panel gained an answer. SYNTCOMP25 stayed 106/180 and moved from PAR-2 2778.691 to 2785.220 s. The capped SYNTCOMP26 run moved 134/180 → 133/180 and 1714.260 → 1731.718 s because `load_balancer_unreal2_pb_5_pe_.ltl` crossed the cap; G3's required 51 s remeasurement recovered the verdict (baseline 13.237 s, candidate 13.530 s), so formal G3 passed. With zero gains and extra recognition overhead, the relaxation was rejected. Its +6.529 s SYNTCOMP25 PAR-2 shift is inside the measured 21.134 s same-binary spread and is not independent rejection evidence.
- **Whole-letter action quotient:** canonicalizing concrete output letters by their complete transition-relation action passed the fixed M1 spike bar on all three targets (1,482,720 → 95,120 actions, 15.59×; 1,354,720 → 90,000, 15.05×; 1,070,280 → 75,568, 14.16×). Corrected same-configuration runs passed G1 at 40/40 (PAR-2 101.867 → 87.880 s), gained two SYNTCOMP25 G3 answers (109/180 → 111/180), and preserved 134/180 on SYNTCOMP26. G2s then found `round_robin_arbiter4.ltl` regressing from 32,306,372,637 to 252,184,896,339 median cycles (680.604%), with all three quotient runs reaching the 60-second cap. The quotient was rejected and removed. It is distinct from the earlier BDD-DAG descent memoization experiment.
- **Isotone dominance sketch for antichain downsets:** instrumentation on a
  `sketch-survival-counters` branch of the `posets` subproject counted, per downset
  structure, pairs considered, pairs rejected by the rank bound, full `partial_order`
  calls, and mean vector dimension. It is guarded by `POSETS_DOMINANCE_STATS` and
  compiles out when undefined; the Posets suite passed 14/14 both with and without it.
  Six SYNTCOMP instances from the census M2 downset bucket were measured with the
  shipping `rank_bucketed_vector_backed` configuration. The workload is bimodal in a
  way that leaves no room for the sketch. Where vector dimension is high, the antichain
  is tiny: `chain-simple-70-real` ran at dimension 4598 but performed only 2,947 pair
  comparisons in the whole run, `chain-simple-50-real` 2,127 at dimension 3318, and
  `chain-simple-30-real` 1,307 at dimension 2038. Where pair volume is large, dimension
  collapses below one AVX2 register at `signed char`: `simple_arbiter_with_hints6` did
  7,301,672,666 pair comparisons at dimension 19, and `load_balancer6` 47,930,606 at
  dimension 39. A 16- or 32-coordinate sketch cannot beat a SIMD compare at those
  dimensions. The rank bound was indeed weak, rejecting between 0.1% and 26.3% of
  pairs, confirming the proposal's premise that most pairs reach an exact comparison;
  but a weak rank filter does not imply that a sketch pays, because the payoff needs
  pair volume and dimension together. Exactly one measured instance occupied the
  targeted regime: `syntcomp24/round_robin_arbiter4.ltl`, whose dominant child ran at
  dimension 453 with 1,772,263,327 pairs considered, 26.3% rejected by rank, and
  766,464,322 surviving exact `partial_order` calls. At that dimension an exact compare
  is roughly 15 AVX2 registers against one for the sketch. The proposed
  `sketched_vector_backed` is not admitted as a default downset structure; if revisited,
  it must be gated on a runtime dimension-and-population guard rather than enabled
  globally, and validated specifically on `round_robin_arbiter4`. The counters perturb
  what they measure, taking `round_robin_arbiter4` from about 11 s to over 90 s, so these
  are structural counts, not timings.
- **Sharing-trie dispatch for large antichains:** a hybrid downset on Posets branch
  `hybrid-sharingtrie-dispatch` stored the antichain in `rank_bucketed_vector_backed`
  and migrated one-way to `sharingtrie_backed` once it reached the runtime threshold
  `POSETS_SHARINGTRIE_MIN_SIZE`, whose default is 4096. The motivation was a July 2026
  comparison in which `sharingtrie_backed` solved 18 instances that `vector_backed`
  missed, including the three largest measured antichains: `lift_unary_enc3` at
  `max_f = 18404`, `robot_grid2_2` at 9383, and `lift5` at 6915. Both binaries were
  built from one tree, differing only in `acacia_array_downset` and
  `acacia_vector_downset`, and every solver invocation ran in its own 8 GiB zero-swap
  cgroup at a 17 s cap. On SYNTCOMP25, base was 111/180 with PAR-2 2648.3 s versus
  hybrid 109/180 with PAR-2 2692.5 s; on SYNTCOMP26, base was 136/180 with PAR-2
  1642.2 s versus hybrid 135/180 with PAR-2 1665.4 s: zero gains and three losses. The
  median time ratio on mutually solved instances was only 1.01 to 1.04, but all three
  lost instances were already within 1.5 s of the cap: `infinite-race-u5` at 16.58 s,
  and `patrolling-alarm21` at 16.77 s and 15.68 s. Even a small dispatch tax therefore
  costs answers precisely there. A targeted rerun on the 18 expected-improvement
  instances gave 7/18 for both variants, again zero gains and zero losses. None of
  those 18 appears in either the 2025 or 2026 panel. Rejected and not landed.
- **K-bounded search terminates inconclusively on the lift/robot_grid family:** on five
  large-antichain instances (`lift5`, `lift_unary_enc3`, `robot_grid2_2`,
  `robot_grid4_4`, and `tmp_13cfc6f2`), the sharing-trie representation is more than
  ten times faster: it completes the bounded search and returns `UNKNOWN` in 4.66 to
  9.05 s, while the vector representation cannot finish within 90 s. The attempt to raise
  the sweep ceiling from the default `-M 2 -K 99 -I 3` to `-M 2 -K 300 -I 3` and
  `-M 2 -K 300 -I 10` was invalid: `-K 300` silently narrowed to a signed char, so those
  runs executed at `k=44`, below the default 99. `VECTOR_ELT_T` caps `k` at 127 regardless,
  so whether a larger `k` decides this family is OPEN, pending a build with a wider element
  type. On the same family, `ltlsynt` solves seven of nine:
  `lift4` in 0.04 s, `lift_unary_enc3` in 0.04 s, `lift5` in 0.13 s,
  `finding_nemo_1` in 0.02 s, `robot_grid2_2` in 0.63 s, `lift_unary_enc4` in 0.63 s,
  and `tmp_13cfc6f2` in 1.05 s. Every answer is `REALIZABLE`, while both the PR head
  and Acacia 1.x time out on all nine. Since K-boundedness is complete for realizable
  specifications, a sweep that converges to `UNKNOWN` on a realizable input points to
  how the bound is applied rather than solver throughput. This remains an open lead
  and is the next thing to investigate for the M2 downset bucket.

The corrected 261-row residual census shows a structural gap rather than a small constant factor. Its
four-mechanism breakdown replaces the older two-corpus solve-rate summary:

| census set | M1 letter-loop | M2 downset | M3 translation-stall | M4 one-sided-race | mixed | total |
|---|---:|---:|---:|---:|---:|---:|
| all losses and slow rows | 112 | 66 | 48 | 9 | 26 | 261 |
| `ltlsynt_only` losses | 47 | 37 | 48 | 9 | 12 | 153 |

The preceding ablation campaign attributed 17 losses to `ltlsynt`'s syntactic bypass, and Acacia's
imported bypass captured all 17. In the corrected residual loss set, 86/153 (56%) are answered by
`ltlsynt` in under 0.2 s. They concentrate in parameterized arbiter, lift, and AMBA families;
the census separates fixed-point/action stalls from translation failures instead of treating that
cluster as one tuning problem.
