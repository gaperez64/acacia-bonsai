# s0-phases: worker failure phases

184 worker records; 46 instances. One row per instance/arm at its largest recorded cap. Campaign seconds describe the whole invocation.

## 1. Routing table

Sparse guarded arm: `unreal/formula/spot-guarded-sparse/frozen-graph`.

| Furthest stage | A | B | C | Total |
| --- | --- | --- | --- | --- |
| before-translation | 2 | 11 | 16 | 29 |
| preprocessing | 0 | 0 | 2 | 2 |
| search | 2 | 5 | 7 | 14 |
| verified-attempt | 1 | 0 | 0 | 1 |
| Total | 5 | 16 | 25 | 46 |

31/46 workers stopped before search: the game was never reached. `before-translation` (or `fallback-translation`) is translation-bound; `preprocessing`, `factory`, or `enumeration` is preprocessing-bound. These are last observed boundaries, not completed phase durations.

## 2. Verification share

Share = 100 × verification_ms / (search_ms + verification_ms). Search includes `verification` and `verified-attempt`. Timings and counters in a `search` snapshot may survive from the preceding completed K attempt; the recorded K can already refer to the new, unfinished attempt. These are snapshot distributions, not whole-run totals or measurements of the unfinished attempt.

Defined shares: n=13 of 15 search-reaching instances.

| Instance | Tier | search_ms | verification_ms | Verification share |
| --- | --- | --- | --- | --- |
| F-G-contradiction-110.ltl | C | 4011.954294 | 3435.350125 | 46.1% |
| thermostat-F-real.ltl | A | 3309.224572 | 2727.119834 | 45.2% |
| robot-to-target-charging7.ltl | B | 759.267908 | 624.307775 | 45.1% |
| GF-G-contradiction7.ltl | B | 4408.142223 | 3557.351500 | 44.7% |
| heim-double-x-real.ltl | A | 2243.556299 | 1796.049500 | 44.5% |
| robot-to-target-charging10.ltl | C | 305.322740 | 238.422936 | 43.8% |
| g-unreal-113.ltl | B | 1010.710226 | 733.422222 | 42.1% |
| g-unreal-116.ltl | C | 2620.373706 | 1896.512492 | 42.0% |
| infinite-race-u12.ltl | C | 5991.595360 | 2872.635752 | 32.4% |
| workstation_resupply_pb_3_pe_.ltl | B | 1799.633345 | 718.616318 | 28.5% |
| taxi-service-real.ltl | B | 27.846947 | 9.246449 | 24.9% |
| ordered-visits-choice-real.ltl | A | 5.362348 | 1.372579 | 20.4% |
| Alarm_a5f99bc6.ltl | C | 8.912095 | 2.206659 | 19.8% |

Share distribution (n=13): minimum 19.8%, median 42.1%, maximum 46.1%. No average of percentages is used.

Present-but-uninstrumented: n=2. Missing timings are absent, not zero, and are excluded from the share distribution.

| Instance | Tier | Stage | k | search_ms | verification_ms |
| --- | --- | --- | --- | --- | --- |
| Morning2_06e9cad4.ltl | C | search | 2 | absent | absent |
| Morning2s_06cf2373.ltl | C | search | 2 | absent | absent |

## 3. Mechanism table

Population: 15 sparse workers that reached search. Each statistic uses only present fields; n applies to both median and max. Choices-per-node is guarded_choices / game_states for each instance, requiring both counters and game_states > 0. K is the recorded K, including workers without timing counters.

| Metric | n | Median | Max |
| --- | --- | --- | --- |
| game_states | 13 | 2750 | 14217 |
| guarded_choices | 13 | 5078 | 14216 |
| choices-per-node | 13 | 1.001899 | 4.769091 |
| subsumption_scans | 13 | 2024 | 14217 |
| subsumption_nodes_checked | 13 | 2900598 | 101246304 |
| reopen_enqueues | 13 | 3117 | 14216 |
| search_bdd_operations | 13 | 15135328 | 42542060 |
| search_queries | 13 | 89248 | 298542 |
| search_preimage_hits | 13 | 427404 | 4331794 |
| wrapper_rows_generated | 13 | 67 | 194 |
| wrapper_states_discovered | 13 | 89 | 201 |
| k | 15 | 17 | 99 |

## 4. Censoring note

35/46 campaign invocations timed out, covering 140/184 captured workers. Among sparse guarded workers, 35/35 in timed-out invocations (35/46 overall) lack a terminal attempt snapshot. These are cap-censored observations: their last stage is not a completed stage duration. This counts absent terminal evidence at the cap; the snapshots alone do not prove the individual process was alive when the kill occurred.

1/46 sparse workers have terminal attempt evidence. `verified-attempt` marks completion of one K attempt, not automatically worker completion: terminal evidence requires WIN_K, UNKNOWN/RESOURCE_LIMIT in candidate-only mode, or LOSE_K with k >= kmax. Retained timings or a LOSE_K status at stage `search` do not mark completion of the worker.

In total, 45/46 sparse workers lack terminal evidence; 10 of these are outside timed-out invocations. Of those, 10 belong to decisive portfolio runs, consistent with cancellation after another arm answered. They are excluded from the cap-censored count.

Backward and forward workers emit no final-record marker. Of their 138 records, 105 belong to capped invocations and 33 to uncapped invocations. The capped count establishes exposure to a campaign timeout, not how many individual backward/forward workers were killed or failed to finish. Their last capture can remain at preprocessing even if the game ran. Absence of a marker cannot distinguish normal return from termination for these arms, so no exact all-arm killed-worker count is identifiable from these files. Treat their last stages as potentially censored, not durations.
