# Frozen target lists

Selection was frozen before treatments on 2026-09-17. Rules and per-instance evidence: [p0-diag/analysis/targets.md](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/p0-diag/analysis/targets.md) (SHA-256 `adbff8abd7c49e55798960bf76bab551ed8690f44e3fe9276453c1c71ef1e8c1`); machine-readable selection: [p0-diag/analysis/frozen-targets.json](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/p0-diag/analysis/frozen-targets.json) (SHA-256 `684a92f0e52f321a98341753a8c81b46fef6d2fd310c6711209279ff828b62ee`). Executed-list authority: [targets/SHA256SUMS](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/targets/SHA256SUMS) (SHA-256 `057a4fc6cdd2ee6d1c256fc3bc6400b9884980a79ef9be782e0e2dea21c7a9c4`). All four listed checksums were recomputed and matched. IDs below preserve each file's order and `.ltl` suffix; they are logical IDs mapped by the existing native TLSF source map.

The lists select bounded screens, not a new corpus or benchmark ground truth. Historical expected verdicts come from decisive records, never from names containing “real” or “unreal”. §8 still requires correctness, no validated regressions and an independently confirmed useful benefit.

## P1A.list

Ten targets: ended cumulative loss-check ms / ended cumulative exploration ms ≥10%, or active loss verification at cap. Prioritize the three cap-censored active checkers, then descending ended loss-check ms. Ratio and censoring caveats are in [phases.md](phases.md).

[targets/p1a.list](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/targets/p1a.list) (SHA-256 `1fa1f43c14e704d335b1f48d0f4eee5c6e8a4c0a8d38bf4b5f05f5a7192b4578`)

```text
heim-double-x-real.ltl
robot-to-target-charging7.ltl
taxi-service-real.ltl
thermostat-F-real.ltl
g-unreal-116.ltl
F-G-contradiction-110.ltl
workstation_resupply_pb_3_pe_.ltl
infinite-race-u12.ltl
GF-G-contradiction7.ltl
g-unreal-113.ltl
```

## P3A.list

Six targets: the four profiles with the most inclusive LossSet insert samples (F-G 32, workstation 22, Alarm 13, infinite-race 12), then two completed-attempt churn cases (GF-G: K=11, 463 inserts/342 removals; ordered-visits: K=99, 201/194). These are sample or last-attempt counts, not cumulative copy costs.

[targets/p3a.list](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/targets/p3a.list) (SHA-256 `de4cb64493c3d0955fb56473971972ec34e788fac6bda8e66f01e889fc4b2e08`)

```text
F-G-contradiction-110.ltl
workstation_resupply_pb_3_pe_.ltl
Alarm_a5f99bc6.ltl
infinite-race-u12.ltl
GF-G-contradiction7.ltl
ordered-visits-choice-real.ltl
```

## P2.list

Twenty-two targets: 15 affected cases + five historical small controls + two near-cap cohort controls. The affected set is four historically solved-real translation-cap cases, the sole sparse Boolean-discovery boundary, and ten other cap-censored translation cases spanning named families. Exclude translation snapshots cancelled early by another arm. Historical controls are selected from already solved isolated requested sparse-arm rows; their fast-path versus sparse-engine routing was not distinguished. Near-cap controls are GF-G-contradiction7 (16.624331 s) and load_balancer_unreal2_pb_5_pe_ (11.665967 s) in this cohort. The raw analysis lists 15+5 separately; the execution list includes both sentinels.

[targets/p2.list](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/targets/p2.list) (SHA-256 `5779ea54db76098b8e92c1c77beaf6bf008fb1f6772c2a75ba0f0dc8f0e3ec2b`)

```text
06.ltl
07.ltl
arbiter-paper-unreal-unreal.ltl
chain-60.ltl
CheckAlarm_6bea956e.ltl
collector_v1_pb_11_pe_.ltl
Demo1_06e9cad4.ltl
follow0.ltl
full_arbiter_unreal1_pb_3_2_pe_.ltl
GF-G-contradiction7.ltl
lift_wrong_physics_unreal_pb_5_pe_.ltl
load_balancer_unreal2_pb_5_pe_.ltl
ltl2dba_C2_unreal_pb_100_pe_.ltl
ltl2dba_R_pb_10_pe_.ltl
ltl2dba_theta_pb_100_pe_.ltl
OneCounterInRangeA2.ltl
prioritized_arbiter_unreal2_pb_100_pe_.ltl
robot_grid_pb_5_5_pe_.ltl
robot_grid_pb_6_6_pe_.ltl
robot-to-target-charging-unreal0.ltl
SPIPureNext.ltl
thermostat-GF-unreal1.ltl
```

## P4.list

Ten targets from reuse.md “Decision inputs → P4”: five proposed gains (SPIPureNext, heim-double-x-real, ordered-visits-choice-real, robot_grid_pb_5_5_pe_, thermostat-F-real); the displaced forward-real unique panel solve workstation_resupply_pb_3_pe_; prior five-arm loss sentinel sort50; and protected backward-real wins amba_decomposed_arbiter_pb_5_pe_, collector_v2_pb_12_pe_, Alarm_a5f99bc6. Screen the single substitution real:small:forward → real:small:spot-guarded-sparse, retaining backward-real and both unreal arms. Historical solo/union/five-arm evidence selects the list; it does not measure this four-arm configuration.

[targets/p4.list](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/targets/p4.list) (SHA-256 `9eab657c8c936403b5a2102f45beb868bb292a3acef49dfa5d12ddaf3b4e7e2a`)

```text
SPIPureNext.ltl
heim-double-x-real.ltl
ordered-visits-choice-real.ltl
robot_grid_pb_5_5_pe_.ltl
thermostat-F-real.ltl
workstation_resupply_pb_3_pe_.ltl
sort50.ltl
amba_decomposed_arbiter_pb_5_pe_.ltl
collector_v2_pb_12_pe_.ltl
Alarm_a5f99bc6.ltl
```

## P1b selection, not executed

The raw analysis freezes ten targets with ≥2 K starts and a positive observed repeated-row lower bound, ordered by the largest ended-attempt row-generation ms. There is **no `targets/p1b.list` or P1b entry in `targets/SHA256SUMS`**. The SHA-256 of `frozen-targets.json` above identifies this selection; no execution-file hash is invented.

```text
workstation_resupply_pb_3_pe_.ltl
heim-double-x-real.ltl
F-G-contradiction-110.ltl
GF-G-contradiction7.ltl
g-unreal-116.ltl
ordered-visits-choice-real.ltl
g-unreal-113.ltl
robot-to-target-charging10.ltl
infinite-race-u12.ltl
thermostat-F-real.ltl
```

## Diagnostic subsets

[targets/p2-mech.list](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/targets/p2-mech.list) (SHA-256 `16fe7f0690414a0d14903ffc7681e925f49de7fb71638658f1fcc2909d2fb533`): `06.ltl`, `07.ltl`, `GF-G-contradiction7.ltl`; selected to attribute the exploratory changed outcomes. [targets/gfg7.list](/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.20260916-demand-sparse/targets/gfg7.list) (SHA-256 `70da32d39c2c66744b4014e5b894d7eb5554b9946039ab9a747a86c3bdc136a5`): `GF-G-contradiction7.ltl` only, for the longer-cap P3a diagnostic. These hashes are computed here; neither subset is listed in the supplied `SHA256SUMS`.

P3b/P3c/P3d have no treatment lists because their measured CPU gates did not fire; the unevaluated alternatives remain explicit in [profile.md](profile.md). P4 selection details and historical displacement caveats remain in [reuse.md](reuse.md#p4--one-evidence-selected-four-arm-screen). P4 outcomes are **PENDING — screen running**.
