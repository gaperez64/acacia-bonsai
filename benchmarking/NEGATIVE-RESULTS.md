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

The corpus-wide diagnosis is a structural stall rather than a small constant
factor.  The strict solve rates that motivate the remaining translation,
letter-loop, and action-construction work are:

| corpus | Acacia solved | `ltlsynt` solved | `ltlsynt`-only | median `ltlsynt` time on its 2025-only set |
|---|---:|---:|---:|---:|
| SYNTCOMP 2024 | 864/1195 | 896/1195 | 66 | -- |
| SYNTCOMP 2025 | 1022/1579 | 1257/1579 | 266 | 0.0855 s |
