# P2 provider isolation: blocked by first-row explosion

**P2 is blocked and is not integrated into the landing candidate.** Both closure variants complete no first row on **19 of the 22 targets**. Only two trivial controls solve. Per plan §5.9, code and tests stay on `research/ds-p2-closure-buchi` (`fd6d5a16`, including core `c1f60fff` and integration `64b961f0`); no partial-row oracle redesign is undertaken.

## Identity and scope

This is the §5.9 A/B/C diagnostic through **single-arm native worker runs in the existing coverage driver**, not a new replay timing campaign or the closing three-way comparison. All series use `p2-64b961f0`, 17 s, 8 GiB whole invocation, zero swap, no CPU quota and `verify-all`:

| Series | Arm / construction |
| --- | --- |
| A | `unreal:formula:spot-guarded-sparse`: optimized Spot **frozen-graph** route, not `spot-eager`/TAA |
| B | Same arm with `:closure-buchi-eager`: enumerate the new provider before search |
| C | Same arm with `:closure-buchi`: generate complete rows on demand |

B/C share the provider, transformed boundary, AP order/partition, cursor=0/rank=0 convention, Kmin=2/Kinc=3/Kmax=99, limits and verification policy; these recorded fields match for every corresponding worker. P1 hints were off. The [integration brief](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/codex/brief-p2int.txt) records inherited P3a code in the research stack: this is **not the clean P3a-off comparison originally requested by §5.9**, although its layout is common to A/B/C. The measured binary predates the retained research tip. Its SHA-256 is `c2a19293f04f00b67971ac6abe6574f8e07cf35f7e74ef05f2aad0946da32fed` in [bin/freeze-manifest.tsv](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/bin/freeze-manifest.tsv); [resolved options](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/bin/p2-64b961f0.options.txt) preserve the build identity.

Drivers: [scripts/diag-p2mech.sh](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/diag-p2mech.sh) (SHA-256 `28d5feacff550b1398300d882a8346d5283ae40b9256dcfa9a840311ae200f2e`) and [scripts/diag-p2eager.sh](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/diag-p2eager.sh) (SHA-256 `26ed956fc1725e8c759b35da7c31dbc97a93d9d75c6216a6afbd8ffbf9fabca2`). Frozen membership: [targets.md](targets.md). Small outcome summaries:

- A: [diag-p2mech/solo-unreal-spot-summary.tsv](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/diag-p2mech/solo-unreal-spot-summary.tsv) (SHA-256 `6c0f7530051fb3dc8180f2db60e4d52ea549dec80e58956f1886e67b13dd2d87`).
- B: [diag-p2mech/solo-unreal-closure-eager-summary.tsv](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/diag-p2mech/solo-unreal-closure-eager-summary.tsv) (SHA-256 `ef5380617e6f415c179c5f58f09f989ab9ac432ddb02fd9e2f8cdd3938fb048e`).
- C: [diag-p2mech/solo-unreal-closure-summary.tsv](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/diag-p2mech/solo-unreal-closure-summary.tsv) (SHA-256 `02dddad58011aaf94866d9c1503e9fb9fd4a197ba5f9c328f986dc22a83bf725`).

[p2-provider-data.tsv](p2-provider-data.tsv) retains full precision and every run/worker path and SHA-256; its hash is `791190f148db7a233e4930762690c41f4534638b5730e78cd55e7f8e0aee1fff`. Other evidence hashes are in [evidence-sha256.tsv](evidence-sha256.tsv). **NA is unavailable/inapplicable, never zero.** A/full_arbiter_unreal1_pb_3_2_pe_ has an outcome but no matching worker JSON. B/C ltl2dba_C2 each have **two worker records**; `[1]` and `[2]` retain both, paired by their identical boundary identities across B/C. Thus B and C each contain 23 worker records over 22 targets. Wall/scope values repeated in the data TSV belong to the whole invocation and must not be summed over workers.

## Outcome and whole-invocation wall time

One observation per target/series, rounded to 0.001 s. `U` = UNREALIZABLE, `?` = UNKNOWN, `T` = TIMEOUT, `E` = ERROR. Timeout cleanup can extend wall time beyond 17 s; A's prioritized_arbiter ERROR is preserved.

| Instance | A outcome / s | B outcome / s | C outcome / s |
| --- | --- | --- | --- |
| 06 | T / 17.037 | ? / 0.358 | ? / 0.361 |
| 07 | T / 17.035 | ? / 0.384 | ? / 0.379 |
| arbiter-paper-unreal-unreal | U / 0.034 | ? / 0.255 | ? / 0.250 |
| chain-60 | T / 17.075 | ? / 0.222 | ? / 0.226 |
| CheckAlarm_6bea956e | U / 0.020 | U / 0.036 | U / 0.029 |
| collector_v1_pb_11_pe_ | T / 17.037 | ? / 1.954 | ? / 1.972 |
| Demo1_06e9cad4 | T / 17.053 | ? / 0.736 | ? / 0.739 |
| follow0 | T / 17.054 | ? / 0.302 | ? / 0.305 |
| full_arbiter_unreal1_pb_3_2_pe_ | U / 0.033 | ? / 0.672 | ? / 0.666 |
| GF-G-contradiction7 | U / 13.120 | ? / 0.174 | ? / 0.182 |
| lift_wrong_physics_unreal_pb_5_pe_ | U / 0.037 | ? / 0.323 | ? / 0.320 |
| load_balancer_unreal2_pb_5_pe_ | T / 17.053 | ? / 0.607 | ? / 0.616 |
| ltl2dba_C2_unreal_pb_100_pe_ | T / 17.087 | ? / 10.086 | ? / 10.138 |
| ltl2dba_R_pb_10_pe_ | T / 17.043 | ? / 1.434 | ? / 1.396 |
| ltl2dba_theta_pb_100_pe_ | T / 17.086 | ? / 3.954 | ? / 3.846 |
| OneCounterInRangeA2 | U / 0.019 | U / 0.031 | U / 0.028 |
| prioritized_arbiter_unreal2_pb_100_pe_ | E / 0.153 | ? / 1.199 | ? / 1.188 |
| robot_grid_pb_5_5_pe_ | T / 17.037 | ? / 0.379 | ? / 0.367 |
| robot_grid_pb_6_6_pe_ | T / 17.053 | ? / 0.396 | ? / 0.383 |
| robot-to-target-charging-unreal0 | T / 17.053 | ? / 0.202 | ? / 0.184 |
| SPIPureNext | T / 17.026 | ? / 1.035 | ? / 1.021 |
| thermostat-GF-unreal1 | T / 17.029 | ? / 0.262 | ? / 0.262 |

## Factory, normalization and search

Cells are **factory / normalization / first-row / cumulative search / cumulative verification**, in ms. Normalization is the actual field `closure_normalization_ms`, included within factory work. `first_row_ms` is a provider row duration, not a separately timed first useful game query. Cumulative search/verification cover ended attempts only; row generation is inclusive in C's search. Eager failures occur before K/search, so absent counters are not zero measurements. A does not publish closure factory/normalization/first-row metrics.

| Instance | A ms | B ms | C ms |
| --- | --- | --- | --- |
| 06 | NA / NA / NA / NA / NA | 0.250 / 0.209 / NA / NA / NA | 0.255 / 0.213 / NA / 320.212 / 0.000 |
| 07 | NA / NA / NA / NA / NA | 0.260 / 0.218 / NA / NA / NA | 0.249 / 0.209 / NA / 354.532 / 0.000 |
| arbiter-paper-unreal-unreal | NA / NA / NA / 2.558 / 1.010 | 0.112 / 0.087 / NA / NA / NA | 0.105 / 0.082 / NA / 227.462 / 0.000 |
| chain-60 | NA / NA / NA / NA / NA | 0.515 / 0.410 / NA / NA / NA | 0.442 / 0.350 / NA / 202.039 / 0.000 |
| CheckAlarm_6bea956e | NA / NA / NA / 0.011 / NA | 0.086 / 0.060 / 0.454 / 6.474 / 1.485 | 0.047 / 0.029 / 0.310 / 7.231 / 1.460 |
| collector_v1_pb_11_pe_ | NA / NA / NA / NA / NA | 0.057 / 0.036 / 32.480 / NA / NA | 0.150 / 0.101 / 37.258 / 1940.310 / 0.000 |
| Demo1_06e9cad4 | NA / NA / NA / NA / NA | 0.311 / 0.205 / NA / NA / NA | 0.307 / 0.202 / NA / 710.082 / 0.000 |
| follow0 | NA / NA / NA / NA / NA | 0.601 / 0.511 / NA / NA / NA | 0.647 / 0.549 / NA / 240.601 / 0.000 |
| full_arbiter_unreal1_pb_3_2_pe_ | NA / NA / NA / NA / NA | 0.064 / 0.048 / NA / NA / NA | 0.061 / 0.043 / NA / 644.167 / 0.000 |
| GF-G-contradiction7 | NA / NA / NA / 5041.095 / 3693.108 | 0.380 / 0.306 / NA / NA / NA | 0.378 / 0.309 / NA / 145.577 / 0.000 |
| lift_wrong_physics_unreal_pb_5_pe_ | NA / NA / NA / 0.125 / 0.327 | 0.446 / 0.362 / NA / NA / NA | 0.292 / 0.235 / NA / 294.618 / 0.000 |
| load_balancer_unreal2_pb_5_pe_ | NA / NA / NA / NA / NA | 0.097 / 0.067 / NA / NA / NA | 0.171 / 0.121 / NA / 592.109 / 0.000 |
| ltl2dba_C2_unreal_pb_100_pe_ | NA / NA / NA / NA / NA | [1] 7.548 / 6.898 / NA / NA / NA<br>[2] 5.383 / 4.945 / NA / NA / NA | [1] 7.728 / 7.023 / NA / 9428.744 / 0.000<br>[2] 5.363 / 4.964 / NA / 493.376 / 0.000 |
| ltl2dba_R_pb_10_pe_ | NA / NA / NA / NA / NA | 0.076 / 0.054 / NA / NA / NA | 0.112 / 0.090 / NA / 1302.361 / 0.000 |
| ltl2dba_theta_pb_100_pe_ | NA / NA / NA / NA / NA | 0.376 / 0.237 / NA / NA / NA | 0.321 / 0.227 / NA / 3759.267 / 0.000 |
| OneCounterInRangeA2 | NA / NA / NA / 0.215 / 0.153 | 0.113 / 0.095 / 0.461 / 2.069 / 2.761 | 0.111 / 0.082 / 1.012 / 6.709 / 1.373 |
| prioritized_arbiter_unreal2_pb_100_pe_ | NA / NA / NA / NA / NA | 4.751 / 3.851 / NA / NA / NA | 4.591 / 3.759 / NA / 1074.976 / 0.000 |
| robot_grid_pb_5_5_pe_ | NA / NA / NA / NA / NA | 0.941 / 0.803 / NA / NA / NA | 0.292 / 0.243 / NA / 342.398 / 0.000 |
| robot_grid_pb_6_6_pe_ | NA / NA / NA / NA / NA | 0.786 / 0.664 / NA / NA / NA | 0.340 / 0.288 / NA / 341.878 / 0.000 |
| robot-to-target-charging-unreal0 | NA / NA / NA / NA / NA | 0.627 / 0.530 / NA / NA / NA | 0.511 / 0.412 / NA / 149.679 / 0.000 |
| SPIPureNext | NA / NA / NA / NA / NA | 0.343 / 0.274 / NA / NA / NA | 0.353 / 0.276 / NA / 997.010 / 0.000 |
| thermostat-GF-unreal1 | NA / NA / NA / NA / NA | 0.536 / 0.420 / NA / NA / NA | 0.495 / 0.412 / NA / 233.641 / 0.000 |

## Provider counters

Cells are **branches considered / guards generated / complete rows / states discovered / edges**, from the matching `closure_*` fields. They are cumulative per provider, including work on failed unpublished rows, not per-K costs. All are NA for A. No completed eager graph means its total reachable-row denominator is unknown; do not infer a lazy saved-row fraction.

| Instance | A | B counters | C counters |
| --- | --- | --- | --- |
| 06 | NA / NA / NA / NA / NA | 1723642 / 2000020 / 0 / 1 / 0 | 1723642 / 2000020 / 0 / 1 / 0 |
| 07 | NA / NA / NA / NA / NA | 1723640 / 2000020 / 0 / 1 / 0 | 1723640 / 2000020 / 0 / 1 / 0 |
| arbiter-paper-unreal-unreal | NA / NA / NA / NA / NA | 968539 / 2000024 / 0 / 1 / 0 | 968539 / 2000024 / 0 / 1 / 0 |
| chain-60 | NA / NA / NA / NA / NA | 381556 / 2000070 / 0 / 1 / 0 | 381556 / 2000070 / 0 / 1 / 0 |
| CheckAlarm_6bea956e | NA / NA / NA / NA / NA | 1175 / 2143 / 131 / 131 / 518 | 1175 / 2143 / 131 / 131 / 518 |
| collector_v1_pb_11_pe_ | NA / NA / NA / NA / NA | 10766202 / 24516555 / 1024 / 26625 / 26624 | 10766202 / 24516555 / 1024 / 26625 / 26624 |
| Demo1_06e9cad4 | NA / NA / NA / NA / NA | 1875157 / 1845291 / 0 / 1 / 0 | 1875157 / 1845291 / 0 / 1 / 0 |
| follow0 | NA / NA / NA / NA / NA | 860680 / 2000050 / 0 / 1 / 0 | 860680 / 2000050 / 0 / 1 / 0 |
| full_arbiter_unreal1_pb_3_2_pe_ | NA / NA / NA / NA / NA | 2000000 / 1923752 / 0 / 1 / 0 | 2000000 / 1923752 / 0 / 1 / 0 |
| GF-G-contradiction7 | NA / NA / NA / NA / NA | 657996 / 2000044 / 0 / 1 / 0 | 657996 / 2000044 / 0 / 1 / 0 |
| lift_wrong_physics_unreal_pb_5_pe_ | NA / NA / NA / NA / NA | 2000000 / 2000014 / 0 / 1 / 0 | 2000000 / 2000014 / 0 / 1 / 0 |
| load_balancer_unreal2_pb_5_pe_ | NA / NA / NA / NA / NA | 2000000 / 1945944 / 0 / 1 / 0 | 2000000 / 1945944 / 0 / 1 / 0 |
| ltl2dba_C2_unreal_pb_100_pe_ | NA / NA / NA / NA / NA | [1] 2000000 / 1145714 / 0 / 1 / 0<br>[2] 2000000 / 1148961 / 0 / 1 / 0 | [1] 2000000 / 1145714 / 0 / 1 / 0<br>[2] 2000000 / 1148961 / 0 / 1 / 0 |
| ltl2dba_R_pb_10_pe_ | NA / NA / NA / NA / NA | 1747760 / 611746 / 0 / 200000 / 0 | 1747760 / 611746 / 0 / 200000 / 0 |
| ltl2dba_theta_pb_100_pe_ | NA / NA / NA / NA / NA | 2000000 / 1000253 / 0 / 1 / 0 | 2000000 / 1000253 / 0 / 1 / 0 |
| OneCounterInRangeA2 | NA / NA / NA / NA / NA | 2732 / 5734 / 123 / 123 / 489 | 2312 / 4836 / 97 / 123 / 391 |
| prioritized_arbiter_unreal2_pb_100_pe_ | NA / NA / NA / NA / NA | 2000000 / 2000097 / 0 / 1 / 0 | 2000000 / 2000097 / 0 / 1 / 0 |
| robot_grid_pb_5_5_pe_ | NA / NA / NA / NA / NA | 1791691 / 2000034 / 0 / 1 / 0 | 1791691 / 2000034 / 0 / 1 / 0 |
| robot_grid_pb_6_6_pe_ | NA / NA / NA / NA / NA | 1781129 / 2000034 / 0 / 1 / 0 | 1781129 / 2000034 / 0 / 1 / 0 |
| robot-to-target-charging-unreal0 | NA / NA / NA / NA / NA | 891270 / 2000046 / 0 / 1 / 0 | 891270 / 2000046 / 0 / 1 / 0 |
| SPIPureNext | NA / NA / NA / NA / NA | 2000000 / 1959498 / 0 / 1 / 0 | 2000000 / 1959498 / 0 / 1 / 0 |
| thermostat-GF-unreal1 | NA / NA / NA / NA / NA | 1393388 / 2000042 / 0 / 1 / 0 | 1393388 / 2000042 / 0 / 1 / 0 |

Across the **20 worker records belonging to the 19 first-row-failing targets**, branch counts span **381,556–2,000,000**, median **1,786,410**, identically in B/C. Each ltl2dba_C2 worker hits 2,000,000, so that instance expends **4,000,000 branches across two unsuccessful providers**. Branch/guard limits are **2,000,000 per raw row**; cumulative guards also include factory literals, explaining totals slightly above that limit. `branch_limit` also covers the separate **10,000,000 step** limit, so the branch counter need not reach 2,000,000. Per target, B/C have 10 guard-limit outcomes, nine branch-limit outcomes, one state-limit outcome and two successes. See [provider options](/home/gperez/GIT-repos/acacia-wt-demand-sparse/src/solver/closure_buchi_provider.hh:40) and [row budgets](/home/gperez/GIT-repos/acacia-wt-demand-sparse/src/solver/closure_buchi_provider.cc:302).

collector_v1 is the sole nonsolved target with completed rows: **1,024 rows, 26,625 states, 26,624 edges**, then a branch limit, with **10,766,202 cumulative branches** across rows. ltl2dba_R discovers **200,000 states but completes zero rows/edges**; discovery is not row publication. Only CheckAlarm and OneCounter solve: CheckAlarm has **131 rows / 518 edges** in both modes; OneCounter has B **123 / 489** versus C **97 / 391**, with 123 discovered states in both. Reducing rows on a trivial control is mechanism evidence, not useful end-to-end admission evidence.

## Highest K, attempts, reasons and memory

Cells are **highest evidenced K; started/completed attempts; worker_reason**. `SG` = `solve-game`, `FP` = `spot-fast-path`, `BI` = `closure-buchi-eager-inconclusive`, `BW` = `closure-buchi-eager-verified-win`, `CI` = `closure-buchi-inconclusive`, `CW` = `closure-buchi-verified-win`. A counts its pre-K Spot attempt; B/C do not. CheckAlarm/A is unbounded. Eager failures do not enter K. Each C worker ends one K=2 attempt, including UNKNOWN; ltl2dba_C2 therefore has two such attempts in separate workers.

| Instance | A | B | C | Scope peak bytes A / B / C |
| --- | --- | --- | --- | --- |
| 06 | NA; NA/NA; NA | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |
| 07 | NA; NA/NA; NA | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |
| arbiter-paper-unreal-unreal | 5; 3/3; SG | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |
| chain-60 | NA; NA/NA; NA | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |
| CheckAlarm_6bea956e | NA; 1/1; FP | 2; 1/1; BW | 2; 1/1; CW | NA / NA / NA |
| collector_v1_pb_11_pe_ | NA; 1/1; NA | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |
| Demo1_06e9cad4 | NA; NA/NA; NA | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |
| follow0 | NA; NA/NA; NA | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |
| full_arbiter_unreal1_pb_3_2_pe_ | NA; NA/NA; NA | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |
| GF-G-contradiction7 | 11; 5/5; SG | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |
| lift_wrong_physics_unreal_pb_5_pe_ | 2; 2/2; SG | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |
| load_balancer_unreal2_pb_5_pe_ | NA; NA/NA; NA | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |
| ltl2dba_C2_unreal_pb_100_pe_ | NA; NA/NA; NA | [1] NA; NA/NA; BI<br>[2] NA; NA/NA; BI | [1] 2; 1/1; CI<br>[2] 2; 1/1; CI | NA / NA / NA |
| ltl2dba_R_pb_10_pe_ | NA; NA/NA; NA | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |
| ltl2dba_theta_pb_100_pe_ | NA; NA/NA; NA | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |
| OneCounterInRangeA2 | 2; 2/2; SG | 2; 1/1; BW | 2; 1/1; CW | 12234752 / NA / NA |
| prioritized_arbiter_unreal2_pb_100_pe_ | NA; NA/NA; NA | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |
| robot_grid_pb_5_5_pe_ | NA; NA/NA; NA | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |
| robot_grid_pb_6_6_pe_ | NA; NA/NA; NA | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |
| robot-to-target-charging-unreal0 | NA; NA/NA; NA | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |
| SPIPureNext | NA; NA/NA; NA | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |
| thermostat-GF-unreal1 | NA; NA/NA; NA | NA; NA/NA; BI | 2; 1/1; CI | NA / NA / NA |

Only A/OneCounter records scope peak memory (**12,234,752 bytes**). Every other A/B/C scope value is unavailable. The data TSV preserves GNU-time process RSS where present; that is not whole-invocation scope peak memory. No actual-memory benefit is established, and a quick UNKNOWN is not a speedup on a solved benchmark.

## Exploratory portfolio mechanism

The **one-round exploratory** [control summary](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/screens/p2-explore/r1-control-summary.tsv) and [treatment summary](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/screens/p2-explore/r1-treatment-summary.tsv) show 06/07 TIMEOUT → REALIZABLE at **15.219/15.336 s**, but GF-G-contradiction7 UNREALIZABLE at **16.322 s** → TIMEOUT. The same binary and four-arm portfolio were used, replacing only the unreal sparse arm's provider. Driver: [scripts/seq-p2explore.sh](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/scripts/seq-p2explore.sh). These are unconfirmed observations, not admission evidence; §8 does not permit net gains to compensate for lost solves.

The later **diagnostic** [four-default.tsv](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/diag-p2mech/four-default.tsv), [four-closure.tsv](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/diag-p2mech/four-closure.tsv) and [worker records](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/diag-p2mech/wrec) explain 06/07. In default portfolios the Spot unreal arm stays `before-translation`; real arms remain at action construction at cap. The closure unreal arm returns UNKNOWN after **339.836/426.056 ms** of worker end-to-end time (roughly 0.4 s), then **forward-real wins at K=2**; portfolio walls are **15.561/15.181 s**. Reduced contention after early closure exit is the supported mechanism; closure itself does not solve either target, and contention was not separately profiled. On GF-G, closure fails at K=2 without a completed row, while solo Spot A reaches a verified K=11 win in **13.120 s**. Both later four-arm GF-G diagnostic runs time out; the default arm is already at K=11/search. These later rows do not erase the exploratory loss or supply five-pair confirmation.

## Correctness evidence carried by tests and reviews

These are **saved review results**, not tests rerun for this documentation. [Core review](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/codex/p2core-review.log): PASS; the independent original-formula fixpoint oracle covers **420 lassos/formula × 50 = 21,000**, plus **100 Spot equivalence checks** over direct materialization and the adapter. All **eight mutations** were caught, including U postponement, cursor/wrap acceptance, acceptance-sensitive merging, done-set handling, R obligations and normalization dualities. A language-preserving acceptance-frequency mutation fails structurally because it can alter finite K. Transactions were checked at **26 fault positions** plus exceptions after **12 wrapped BDD operations**. ASan/UBSan passed; leak scanning was unavailable under tracing, with explicit dictionary-lifetime checks retained.

[Integration review](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/codex/p2int-review.log): PASS; **192 lazy/eager finite-K comparisons** (eight specs × both polarities × K=1–3 × four choice semantics), **1,920 successors**, **eight certificate corruptions**, **13 failure injections**, and **42 invalid configurations**. All **six mutations** failed: incomplete row accepted, acceptance dropped, AP partition swapped, initial rank changed, automaton-unreal allowed, and fallback after closure decline. Fresh Reader/Oracle reconstruction preserves independent certificate checks. The review reports **50 unit and 39 Python tests passed**. These correctness checks neither establish performance nor require equal successful K to optimized Spot.

## P4-relevant diagnostic and disposition

[diag-p2mech/solo-real-spot-summary.tsv](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/diag-p2mech/solo-real-spot-summary.tsv) (SHA-256 `988ab84c37bf4196a035ba230272a7cc87a50fb90a739182582be59e8bd170f3`) records four REALIZABLE solves: **06 14.847 s; 07 14.541 s; robot_grid_pb_5_5_pe_ 7.800 s; SPIPureNext 0.458 s**; the remaining outcomes are 14 TIMEOUT and four UNKNOWN. This is **single-arm diagnostic evidence** for sparse-real, not a measured P4 portfolio. [diag-p2mech/solo-real-closure-summary.tsv](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/diag-p2mech/solo-real-closure-summary.tsv) (SHA-256 `1461c23bb38b082d0ad43fcbaed44f157b7550de9ec466c9952d798f30a0ee1e`) has **zero solves: 18 UNKNOWN, four TIMEOUT**.

The reproducible affected-benchmark benefit required before integration by [plan §5.9](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/plan.md) is absent, and the exploratory portfolio contains a lost solve. Keep code/tests and negative evidence on the named research branch; **blocked, not integrated, no partial-row redesign**. Scope-memory comparisons, A/full_arbiter internals, unfinished eager-graph denominators and a separately timed first useful query remain unavailable.
