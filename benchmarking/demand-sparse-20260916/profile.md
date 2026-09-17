# P0 profile: conditional storage gates

**None of the sampled CPU gates fires.** Source: [p0-diag/analysis/profile.md](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/p0-diag/analysis/profile.md) (SHA-256 `863900b5f89f32f708fb5ff7e294fbc7f5e54a0381e1b28dff3f8404b44a7d9a`); exact counts: [p0-diag/analysis/gate-counts.tsv](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/p0-diag/analysis/gate-counts.tsv) (SHA-256 `a6b7a9f660373cfccf1e6af1f875af4191ef3d73ad4a7fe55c5e78fd0295cdb6`). The source links the existing sample ledger, stack groups, per-TID attribution and classification code. Driver: [scripts/p0-diag.sh](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/p0-diag.sh). This report reads the saved analysis; no profiling was run.

The release profiling twin `p0-4a04e882-prof` supplied one whole-portfolio `cycles:u` invocation per target, sampled at **199 Hz** with DWARF stacks. One sample is a stack event, not an inline frame. The denominator is only samples with the explicit sparse `Search::solve()` exploration-lambda ancestor; verification takes precedence and setup/destruction is excluded. Whole-portfolio period-weighted self/children percentages are different denominators.

| Target | Total samples | Sparse exploration n | b % [95% CI] | c % [95% CI] | d % [95% CI] | Verdict |
| --- | --- | --- | --- | --- | --- | --- |
| Alarm_a5f99bc6 | 13099 | 3379 | 76/3379 = 2.25% [1.80, 2.81] | 76/3379 = 2.25% [1.80, 2.81] | 111/3379 = 3.28% [2.74, 3.94] | b/c/d CPU: does not fire |
| F-G-contradiction-110 | 13544 | 2219 | 20/2219 = 0.90% [0.58, 1.39] | 47/2219 = 2.12% [1.60, 2.81] | 66/2219 = 2.97% [2.34, 3.77] | b/c/d CPU: does not fire |
| infinite-race-u12 | 13551 | 2543 | 48/2543 = 1.89% [1.43, 2.49] | 41/2543 = 1.61% [1.19, 2.18] | 53/2543 = 2.08% [1.60, 2.72] | b/c/d CPU: does not fire |
| robot-to-target-charging10 | 13553 | 312 | 1/312 = 0.32% [0.06, 1.79] | 4/312 = 1.28% [0.50, 3.25] | 10/312 = 3.21% [1.75, 5.80] | b/c/d CPU: does not fire |
| workstation_resupply_pb_3_pe_ | 9452 | 2150 | 5/2150 = 0.23% [0.10, 0.54] | 102/2150 = 4.74% [3.92, 5.73] | 116/2150 = 5.40% [4.52, 6.43] | b/c/d CPU: does not fire |

All five sparse denominators meet **n ≥ 300**, including robot-to-target-charging10 at 312. Thresholds are **10%** for P3b broad scan and P3c LossSet, **15%** for P3d payload allocation/copy. These select work; they do not establish §8 performance admission.

Attribution is disjoint: raw row generation, BDD runtime, rank-payload allocation/copy, LossSet, residual `enqueue_loss`, then other/unknown. P3b is a conservative upper bucket including indistinguishable broadcast bookkeeping. P3c includes small loop bookkeeping after payload allocation/copy has been removed. Tree/set allocation, sorting and lookup are not automatically rank-payload copying. No raw-row-generation sample occurs in these exploration denominators; this is not proof of zero row cost.

Even assigning **all identified exploration allocation/copy** to P3d gives only **246/3379 = 7.28%, 186/2219 = 8.38%, 240/2543 = 9.44%, 37/312 = 11.86%, 190/2150 = 8.84%**, in table order: still below 15%. Inclusive LossSet insert counts are **13, 32, 12, 1, 22**, respectively; they supported the small P3a screen but do not isolate survivor-copy time. Much residual sparse work is Oracle preimage/rank/key work.

P3c's additional requirement—a material fraction of queries with **≥128 live generators**—is **unevaluated**; an antichain peak is insufficient. P3d's alternative **≥2 normalized-content duplication and ≥25% of attributable retained native storage** is **unevaluated**, with no measured byte denominator. P3b's alternative repeatable replay-benefit route is also unmeasured. No P3b/c/d implementation is justified by these data.

Intervals are nominal two-sided **95% Wilson** intervals on unweighted event counts. They omit periodic-sampling correlation, single-invocation variation, inlining/unwinding ambiguity and profiler overhead. Zero reported lost samples does not establish complete unwinding. The workstation profiling twin returned REALIZABLE while its cohort observation timed out; it cannot replace the cohort's timing or create a new near-cap sentinel.
