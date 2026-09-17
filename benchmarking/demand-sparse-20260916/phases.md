# P0 phases: diagnostic evidence

The single cohort pass contains **46 instances × four requested arms = 184 worker records**: 34 TIMEOUT, nine UNREALIZABLE, two REALIZABLE and one UNKNOWN. It used `p0-4a04e882`, native TLSF, a 17 s whole-invocation cap, 8 GiB, zero swap and no CPU quota. The five profiling-twin invocations are separate observations. This is diagnostic evidence, not a headline timing campaign.

Source: [p0-diag/analysis/phases.md](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/p0-diag/analysis/phases.md) (SHA-256 `dda3c48bacb6887c055978e0d2e3a653fe0fea00b5fd9189776ad1f408012a38`). Its per-instance table links every worker snapshot and cohort/phases TSV row. The condensed conclusions are also in [p0-diag/analysis/SUMMARY.txt](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/p0-diag/analysis/SUMMARY.txt) (SHA-256 `a3dfb2f33e19f8e69f0a4c6a58874612f707f98b1b63bc23325cb8547902c255`). Driver: [scripts/p0-diag.sh](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/p0-diag.sh); binary/options and other small-source hashes are in [evidence-sha256.tsv](evidence-sha256.tsv).

## Observed boundaries

| Requested arm | Translation boundary | of which portfolio cap | Preprocessing boundary | of which portfolio cap | Search-reaching |
| --- | --- | --- | --- | --- | --- |
| unreal:formula:spot-guarded-sparse | 29 | 20 | 1 | 1 | 16 |
| real:small:backward | 24 | 19 | 5 | 5 | 17 |
| real:small:forward | 24 | 19 | 4 | 4 | 18 |
| unreal:automaton:forward | 16 | 15 | 11 | 9 | 19 |

Every arm has denominator 46. A boundary is the last observed activity, not elapsed time attributable to that activity. Sparse Search itself was reached on **15/46**; the requested sparse arm reached some search on **16/46**, including FelixSpecFixed2's `spot-fast-det` route. Of its 29 translation-boundary snapshots, 20 were cap-censored and nine were cancelled after another arm solved. The sole sparse preprocessing boundary is collector_v1's Boolean discovery. Its earlier declined fast-path attempt is not sparse search. Automaton-forward returned `input-push-limit` on 06, 07 and robot_grid_pb_5_5_pe_; portfolio timeout does not imply those workers stayed busy.

## Cumulative sparse-worker evidence

Times below are ms, rounded to 0.001. **Cumulative timers cover ended attempts only**; interrupted exploration and unfinished checker time are absent. Started/completed attempt counts include the pre-K Spot attempt, including declines. Highest K is the highest evidenced bound in the monotone segment; an unbounded fast path has no K. `L` means losing checks; `W` is total verification minus losing verification only where the effective sparse backend supports that split. Rows are inclusive within search/verification and must not be added to wall time. `NA` is unrecorded, never zero. Evidence labels resolve in the linked raw analysis.

| Instance | Attempts started/completed | Highest K | Search ms | L calls/ms | W ms | Rows ms/count | Worker end | Peak memory | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Alarm_a5f99bc6 | 3/2 | 5 | 8.780 | 1 / 2.400 | 0.000 | 0.049 / 19 | unobserved | NA | P13; 49399-1 |
| F-G-contradiction-110 | 7/6 | 17 | 6962.040 | 5 / 5555.593 | 0.000 | 2.690 / 354 | unobserved | NA | P21; 49435-1 |
| FelixSpecFixed2_fa4d4ce3 | 1/1 | — | 0.025 | NA / NA | NA | NA / NA | returned | NA | P25; 49449-1 |
| GF-G-contradiction7 | 5/5 | 11 | 5861.470 | 3 / 3955.668 | 118.084 | 2.279 / 345 | returned | NA | P29; 49235-1 |
| Morning2_06e9cad4 | 2/1 | 2 | NA | NA / NA | NA | NA / NA | unobserved | NA | P33; 49471-1 |
| Morning2s_06cf2373 | 2/1 | 2 | NA | NA / NA | NA | NA / NA | unobserved | NA | P37; 49497-1 |
| g-unreal-113 | 8/7 | 20 | 3027.855 | 6 / 2176.039 | 0.000 | 1.286 / 319 | unobserved | NA | P69; 49268-1 |
| g-unreal-116 | 10/9 | 26 | 8924.088 | 8 / 6346.647 | 0.000 | 2.242 / 494 | unobserved | NA | P73; 49570-1 |
| heim-double-x-real | 8/7 | 20 | 6918.487 | 6 / 5759.843 | 0.000 | 3.068 / 1146 | unobserved | NA | P77; 49155-1 |
| infinite-race-u12 | 5/4 | 11 | 8293.061 | 3 / 4194.778 | 0.000 | 1.198 / 178 | unobserved | NA | P85; 49604-1 |
| ordered-visits-choice-real | 35/35 | 99 | 96.724 | 34 / 31.667 | 0.000 | 1.717 / 773 | returned | NA | P125; 49165-1 |
| robot-to-target-charging10 | 7/6 | 17 | 1129.133 | 5 / 911.674 | 0.000 | 1.277 / 260 | unobserved | NA | P137; 49721-1 |
| robot-to-target-charging7 | 6/5 | 14 | 400.189 | 4 / 309.877 | 0.000 | 0.794 / 201 | unobserved | NA | P141; 49340-1 |
| taxi-service-real | 3/2 | 5 | 26.052 | 1 / 9.249 | 0.000 | 0.221 / 89 | unobserved | NA | P173; 49367-1 |
| thermostat-F-real | 8/7 | 20 | 7814.805 | 6 / 6430.010 | 0.000 | 1.017 / 285 | unobserved | NA | P177; 49187-1 |
| workstation_resupply_pb_3_pe_ | 11/10 | 29 | 10451.182 | 9 / 4393.110 | 0.000 | 3.670 / 1276 | unobserved | NA | P185; 49378-1 |

All **13 historical multi-K targets** repeat K starts; raw attempt counts above are not K-only counts. Four open attempts reached losing verification. Adding their durable completed-search/row snapshots exactly once gives lower bounds:

| Instance | Observed search ms | Observed loss calls | Loss ms | Observed row-generation ms | Observed rows | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| g-unreal-113 | 4472.169 | 7 | 2176.039 + NA current | 1.523 | 378 | P69 |
| heim-double-x-real | 9688.222 | 7 | 5759.843 + NA current | 3.640 | 1340 | P77 |
| robot-to-target-charging7 | 1192.490 | 5 | 309.877 + NA current | 1.307 | 262 | P141 |
| taxi-service-real | 14963.613 | 2 | 9.249 + NA current | 0.726 | 181 | P173 |

The current losing call's unfinished ms remain unavailable. Three of these workers were in losing verification at the portfolio cap (heim-double-x-real, robot-to-target-charging7, taxi-service-real); g-unreal-113 was cancelled by the portfolio. P1a's selection ratio is ended loss-check ms / ended exploration ms, **not** a whole-run percentage or a share of search plus verification.

Frozen RowStore construction repeats inside K; the lazy worker already retains immutable rows across K. Ended row-generation time is at most **3.670 ms per multi-K target**, far below §8's **50 ms** single-instance runtime floor even if all that measured work disappeared. This is a work-selection argument for not attempting P1b, not an upper bound on unrecorded work or a measured speedup. Exact duplicate row counts usually cannot be reconstructed without per-K source-ID sets. The observed bounds are in the source; g-unreal-113 has exactly **319 duplicate generation events among the observed rows** (319 ended + 59 current − 59 distinct), still a lower bound on full-invocation work. Alarm's second K start establishes a recreation opportunity but no durable positive repeat count.

No actual whole-invocation peak memory or worker peak bytes were recorded in the cohort. Logical rank/cache capacity estimates do not establish peak RSS, normalized-content duplication or attributable retained-storage share. Earlier verification may remain the furthest observed phase even when the final snapshot is a new search; `attempt-end` is an outcome boundary.
