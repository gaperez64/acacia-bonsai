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
