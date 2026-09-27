# S0 diagnostic summary

Source: `/home/gperez/GIT-repos/acacia-gr1-par2-timing/benchmarking/gr1-par2-20260923/s0/raw`

## Notes

- S0 run 2: L0 tools (build-L0, tlsf-tools 6b2507f), Acacia 3ac18759, timing worktree, serial, 120 s, 8 GiB

## Targets

| Target | Verdict | Exit | Total wall | cgroup memory.peak | Censored stage |
|---|---:|---:|---:|---:|---|
| arbiter_with_cancel-n8 | REALIZABLE | 0 | 17.195 s | 1.47 GiB | none |
| arbiter_with_cancel-n9 | REALIZABLE | 0 | 55.603 s | 1.91 GiB | none |
| arbiter_with_cancel-n10 | UNKNOWN | 2 | 88.175 s | 3.58 GiB | none |
| load_balancer_unreal2-n6 | UNREALIZABLE | 1 | 5.210 s | 1.83 GiB | none |
| load_balancer_unreal2-n7 | UNKNOWN | 2 | 118.203 s | 6.60 GiB | none |
| load_balancer-n8 | REALIZABLE | 0 | 18.674 s | 1.07 GiB | none |
| load_balancer-n9 | REALIZABLE | 0 | 57.010 s | 1.12 GiB | none |
| amba_decomposed_lock-n15 | REALIZABLE | 0 | 41.370 s | 2.26 GiB | none |
| arbiter_with_buffer-n8 | REALIZABLE | 0 | 15.495 s | 1.09 GiB | none |
| arbiter_with_buffer-n9 | UNKNOWN | 2 | 120.609 s | 1.78 GiB | instantiate ≥ 0.000 s |
| arbiter_on_inpchange-n6 | REALIZABLE | 0 | 52.153 s | 1.99 GiB | none |
| arbiter_on_inpchange-n7 | UNKNOWN | 2 | 120.235 s | 3.56 GiB | target_check ≥ 103.287 s |
| round_robin_arbiter_unreal2-n5 | UNREALIZABLE | 1 | 2.096 s | 1.35 GiB | none |
| round_robin_arbiter_unreal2-n6 | UNREALIZABLE | 1 | 7.807 s | 1.61 GiB | none |
| round_robin_arbiter_unreal2-n7 | UNREALIZABLE | 1 | 33.507 s | 2.74 GiB | none |
| collector_v1-n11 | UNKNOWN | 2 | 119.975 s | 2.10 GiB | canonicalize ≥ 119.675 s; target_monitor_construction ≥ 119.675 s |
| arbiter-n6 | REALIZABLE | 0 | 2.254 s | 963.12 MiB | none |
| prioritized_arbiter-n7 | REALIZABLE | 0 | 1.042 s | 931.68 MiB | none |

## Cross-target phase dominance

| Target | Dominant phase | Second | Third |
|---|---|---|---|
| arbiter_with_cancel-n8 | target_check — 12.616 s (73.4%) | bdd_projection_relabel — 0.944 s (5.5%) | instantiate_templates — 0.791 s (4.6%) |
| arbiter_with_cancel-n9 | target_check — 50.499 s (90.8%) | bdd_projection_relabel — 1.128 s (2.0%) | instantiate_templates — 1.071 s (1.9%) |
| arbiter_with_cancel-n10 | target_check — 82.178 s (93.2%) | instantiate_templates — 1.507 s (1.7%) | bdd_projection_relabel — 1.436 s (1.6%) |
| load_balancer_unreal2-n6 | target_check — 3.493 s (67.1%) | target_solve — 1.448 s (27.8%) | target_monitor_construction — 0.084 s (1.6%) |
| load_balancer_unreal2-n7 | target_check — 108.312 s (91.6%) | target_solve — 9.618 s (8.1%) | target_monitor_construction — 0.094 s (0.1%) |
| load_balancer-n8 | substitute_variables — 8.596 s (46.0%) | bdd_projection_relabel — 6.136 s (32.9%) | policy_construction_skolemization — 3.341 s (17.9%) |
| load_balancer-n9 | substitute_variables — 35.216 s (61.8%) | bdd_projection_relabel — 14.326 s (25.1%) | policy_construction_skolemization — 8.858 s (15.5%) |
| amba_decomposed_lock-n15 | bdd_projection_relabel — 22.137 s (53.5%) | policy_construction_skolemization — 11.640 s (28.1%) | substitute_variables — 8.136 s (19.7%) |
| arbiter_with_buffer-n8 | policy_construction_skolemization — 9.501 s (61.3%) | bdd_projection_relabel — 5.337 s (34.4%) | export — 1.382 s (8.9%) |
| arbiter_with_buffer-n9 | generalize_gr1 — 120.589 s (100.0%) | instantiate — ≥ 0.000 s (≥ 0.0%) | n/a |
| arbiter_on_inpchange-n6 | target_check — 36.723 s (70.4%) | from_aag — 2.975 s (5.7%) | bdd_projection_relabel — 2.033 s (3.9%) |
| arbiter_on_inpchange-n7 | target_check — ≥ 103.287 s (≥ 85.9%) | from_aag — 3.184 s (2.6%) | bdd_projection_relabel — 2.573 s (2.1%) |
| round_robin_arbiter_unreal2-n5 | target_check — 1.009 s (48.1%) | target_solve — 0.832 s (39.7%) | target_monitor_construction — 0.078 s (3.7%) |
| round_robin_arbiter_unreal2-n6 | target_check — 3.959 s (50.7%) | target_solve — 3.578 s (45.8%) | target_monitor_construction — 0.087 s (1.1%) |
| round_robin_arbiter_unreal2-n7 | target_check — 18.250 s (54.5%) | target_solve — 14.984 s (44.7%) | target_monitor_construction — 0.094 s (0.3%) |
| collector_v1-n11 | target_monitor_construction — ≥ 239.349 s (≥ 199.5%) | canonicalize — ≥ 119.675 s (≥ 99.7%) | seed_monitor_construction — 0.074 s (0.1%) |
| arbiter-n6 | bdd_projection_relabel — 0.343 s (15.2%) | seed_monitor_construction — 0.272 s (12.1%) | export — 0.267 s (11.9%) |
| prioritized_arbiter-n7 | seed_monitor_construction — 0.259 s (24.9%) | target_monitor_construction — 0.136 s (13.0%) | target_check — 0.093 s (8.9%) |

## Per-target detail

### arbiter_with_cancel-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 17.195 s
- cgroup `memory.peak`: 1.47 GiB
- Censored stages: none
- Checker `--stats`: unsupported
- Support width min/median/max: 25 / 25 / 25
- Owner-tuple arity histogram: 0: 16, 1: 589
- Subset reuse: 48 distinct; 48 reused; uses min/median/max 73 / 115 / 188; operations instantiation: 3950, projection: 1880
- Mask word-length histogram: 1: 1316, 2: 564
- Actual checker mode counts: 32: 1, 50: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 12.615522 s | 73.4% |
| bdd_projection_relabel | 9402 | 0.943787 s | 5.5% |
| instantiate_templates | 188 | 0.791360 s | 4.6% |
| export | 8874 | 0.612216 s | 3.6% |
| seed_solves | 6 | 0.596211 s | 3.5% |
| from_aag | 564 | 0.524175 s | 3.0% |
| seed_monitor_construction | 6 | 0.433770 s | 2.5% |
| policy_construction_skolemization | 2 | 0.332365 s | 1.9% |
| target_monitor_construction | 2 | 0.167132 s | 1.0% |
| projection_metadata | 4324 | 0.128992 s | 0.8% |
| support_extraction | 5830 | 0.113620 s | 0.7% |
| variable_cube_construction | 1692 | 0.037394 s | 0.2% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_cancel-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 55.603 s
- cgroup `memory.peak`: 1.91 GiB
- Censored stages: none
- Checker `--stats`: unsupported
- Support width min/median/max: 25 / 25 / 25
- Owner-tuple arity histogram: 0: 16, 1: 608
- Subset reuse: 56 distinct; 56 reused; uses min/median/max 73 / 129 / 202; operations instantiation: 5374, projection: 2020
- Mask word-length histogram: 1: 1414, 2: 606
- Actual checker mode counts: 32: 1, 56: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 50.498842 s | 90.8% |
| bdd_projection_relabel | 11232 | 1.127728 s | 2.0% |
| instantiate_templates | 202 | 1.070667 s | 1.9% |
| export | 10366 | 0.782271 s | 1.4% |
| seed_solves | 6 | 0.597326 s | 1.1% |
| from_aag | 606 | 0.567327 s | 1.0% |
| policy_construction_skolemization | 2 | 0.438075 s | 0.8% |
| seed_monitor_construction | 6 | 0.423373 s | 0.8% |
| target_monitor_construction | 2 | 0.165782 s | 0.3% |
| projection_metadata | 4646 | 0.139181 s | 0.3% |
| support_extraction | 7394 | 0.137064 s | 0.2% |
| variable_cube_construction | 1818 | 0.037905 s | 0.1% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_cancel-n10

- Verdict/exit: UNKNOWN / 2
- Total wall: 88.175 s
- cgroup `memory.peak`: 3.58 GiB
- Censored stages: none
- Checker `--stats`: unsupported
- Support width min/median/max: 25 / 25 / 25
- Owner-tuple arity histogram: 0: 16, 1: 627
- Subset reuse: 65 distinct; 65 reused; uses min/median/max 73 / 143 / 216; operations instantiation: 7165, projection: 2160
- Mask word-length histogram: 1: 1512, 2: 648
- Actual checker mode counts: 32: 1, 62: 2

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 82.178331 s | 93.2% |
| instantiate_templates | 216 | 1.507039 s | 1.7% |
| bdd_projection_relabel | 13429 | 1.435917 s | 1.6% |
| export | 12018 | 1.077071 s | 1.2% |
| from_aag | 648 | 0.592656 s | 0.7% |
| policy_construction_skolemization | 2 | 0.584610 s | 0.7% |
| seed_solves | 6 | 0.583288 s | 0.7% |
| seed_monitor_construction | 6 | 0.454011 s | 0.5% |
| support_extraction | 9325 | 0.182774 s | 0.2% |
| target_monitor_construction | 2 | 0.172756 s | 0.2% |
| projection_metadata | 4968 | 0.150574 s | 0.2% |
| variable_cube_construction | 1944 | 0.046228 s | 0.1% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### load_balancer_unreal2-n6

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 5.210 s
- cgroup `memory.peak`: 1.83 GiB
- Censored stages: none
- Checker `--stats`: unsupported
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 12, 1: 46, 2: 70
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 4: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 3.493460 s | 67.1% |
| target_solve | 1 | 1.447882 s | 27.8% |
| target_monitor_construction | 1 | 0.083961 s | 1.6% |
| bdd_projection_relabel | 0 | 0.000000 s | 0.0% |
| export | 0 | 0.000000 s | 0.0% |
| from_aag | 0 | 0.000000 s | 0.0% |
| instantiate_templates | 0 | 0.000000 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| projection_metadata | 0 | 0.000000 s | 0.0% |
| seed_monitor_construction | 0 | 0.000000 s | 0.0% |
| seed_solves | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| support_extraction | 0 | 0.000000 s | 0.0% |
| variable_cube_construction | 0 | 0.000000 s | 0.0% |

### load_balancer_unreal2-n7

- Verdict/exit: UNKNOWN / 2
- Total wall: 118.203 s
- cgroup `memory.peak`: 6.60 GiB
- Censored stages: none
- Checker `--stats`: unsupported
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 12, 1: 53, 2: 96
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 4: 2

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 108.311703 s | 91.6% |
| target_solve | 1 | 9.617847 s | 8.1% |
| target_monitor_construction | 1 | 0.093732 s | 0.1% |
| bdd_projection_relabel | 0 | 0.000000 s | 0.0% |
| export | 0 | 0.000000 s | 0.0% |
| from_aag | 0 | 0.000000 s | 0.0% |
| instantiate_templates | 0 | 0.000000 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| projection_metadata | 0 | 0.000000 s | 0.0% |
| seed_monitor_construction | 0 | 0.000000 s | 0.0% |
| seed_solves | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| support_extraction | 0 | 0.000000 s | 0.0% |
| variable_cube_construction | 0 | 0.000000 s | 0.0% |

### load_balancer-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 18.674 s
- cgroup `memory.peak`: 1.07 GiB
- Censored stages: none
- Checker `--stats`: unsupported
- Support width min/median/max: 0 / 20 / 20
- Owner-tuple arity histogram: 0: 96, 1: 249, 2: 46
- Subset reuse: 48 distinct; 48 reused; uses min/median/max 112 / 175 / 287; operations instantiation: 6020, projection: 2870
- Mask word-length histogram: 0: 984, 1: 1886
- Actual checker mode counts: 17: 1, 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| substitute_variables | 287 | 8.595909 s | 46.0% |
| bdd_projection_relabel | 15252 | 6.136353 s | 32.9% |
| policy_construction_skolemization | 2 | 3.340754 s | 17.9% |
| export | 518 | 0.852631 s | 4.6% |
| seed_monitor_construction | 6 | 0.421526 s | 2.3% |
| target_check | 1 | 0.402225 s | 2.2% |
| instantiate_templates | 287 | 0.354274 s | 1.9% |
| from_aag | 861 | 0.325545 s | 1.7% |
| seed_solves | 6 | 0.165859 s | 0.9% |
| projection_metadata | 6601 | 0.142637 s | 0.8% |
| target_monitor_construction | 2 | 0.141632 s | 0.8% |
| support_extraction | 9799 | 0.090147 s | 0.5% |
| variable_cube_construction | 2585 | 0.035783 s | 0.2% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### load_balancer-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 57.010 s
- cgroup `memory.peak`: 1.12 GiB
- Censored stages: none
- Checker `--stats`: unsupported
- Support width min/median/max: 0 / 20 / 20
- Owner-tuple arity histogram: 0: 96, 1: 256, 2: 48
- Subset reuse: 56 distinct; 56 reused; uses min/median/max 112 / 196 / 308; operations instantiation: 8176, projection: 3080
- Mask word-length histogram: 0: 1056, 1: 2024
- Actual checker mode counts: 17: 1, 29: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| substitute_variables | 308 | 35.215511 s | 61.8% |
| bdd_projection_relabel | 18082 | 14.326106 s | 25.1% |
| policy_construction_skolemization | 2 | 8.858363 s | 15.5% |
| export | 556 | 1.965464 s | 3.4% |
| target_check | 1 | 0.865476 s | 1.5% |
| instantiate_templates | 308 | 0.470875 s | 0.8% |
| seed_monitor_construction | 6 | 0.398065 s | 0.7% |
| from_aag | 924 | 0.363203 s | 0.6% |
| seed_solves | 6 | 0.166988 s | 0.3% |
| projection_metadata | 7084 | 0.156731 s | 0.3% |
| target_monitor_construction | 2 | 0.143092 s | 0.3% |
| support_extraction | 12230 | 0.122662 s | 0.2% |
| variable_cube_construction | 2774 | 0.037995 s | 0.1% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### amba_decomposed_lock-n15

- Verdict/exit: REALIZABLE / 0
- Total wall: 41.370 s
- cgroup `memory.peak`: 2.26 GiB
- Censored stages: none
- Checker `--stats`: unsupported
- Support width min/median/max: 7 / 7 / 7
- Owner-tuple arity histogram: 0: 80, 1: 190
- Subset reuse: 29 distinct; 29 reused; uses min/median/max 25 / 65 / 90; operations instantiation: 1100, projection: 810
- Mask word-length histogram: 1: 810
- Actual checker mode counts: 7: 1, 17: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| bdd_projection_relabel | 3896 | 22.136538 s | 53.5% |
| policy_construction_skolemization | 2 | 11.640432 s | 28.1% |
| substitute_variables | 90 | 8.135756 s | 19.7% |
| target_check | 1 | 7.848278 s | 19.0% |
| export | 202 | 1.511663 s | 3.7% |
| seed_monitor_construction | 6 | 0.393672 s | 1.0% |
| target_monitor_construction | 2 | 0.170623 s | 0.4% |
| seed_solves | 6 | 0.134306 s | 0.3% |
| instantiate_templates | 90 | 0.060408 s | 0.1% |
| support_extraction | 2276 | 0.036182 s | 0.1% |
| projection_metadata | 1890 | 0.023148 s | 0.1% |
| from_aag | 270 | 0.018750 s | 0.0% |
| variable_cube_construction | 812 | 0.007324 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_buffer-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 15.495 s
- cgroup `memory.peak`: 1.09 GiB
- Censored stages: none
- Checker `--stats`: unsupported
- Support width min/median/max: 4 / 4 / 4
- Owner-tuple arity histogram: 0: 16, 1: 248
- Subset reuse: 22 distinct; 22 reused; uses min/median/max 13 / 19 / 32; operations instantiation: 217, projection: 288
- Mask word-length histogram: 1: 288
- Actual checker mode counts: 7: 1, 10: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| policy_construction_skolemization | 2 | 9.501046 s | 61.3% |
| bdd_projection_relabel | 1276 | 5.337238 s | 34.4% |
| export | 133 | 1.381618 s | 8.9% |
| target_check | 1 | 0.409624 s | 2.6% |
| seed_monitor_construction | 6 | 0.391747 s | 2.5% |
| substitute_variables | 32 | 0.228198 s | 1.5% |
| target_monitor_construction | 2 | 0.139749 s | 0.9% |
| seed_solves | 6 | 0.133871 s | 0.9% |
| support_extraction | 700 | 0.021642 s | 0.1% |
| instantiate_templates | 32 | 0.007075 s | 0.0% |
| projection_metadata | 672 | 0.006774 s | 0.0% |
| from_aag | 96 | 0.006068 s | 0.0% |
| variable_cube_construction | 290 | 0.003867 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_buffer-n9

- Verdict/exit: UNKNOWN / 2
- Total wall: 120.609 s
- cgroup `memory.peak`: 1.78 GiB
- Censored stages: instantiate ≥ 0.000 s (absolute_deadline_exhausted)
- Checker `--stats`: no payload captured
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Legacy recorded mode counts (known inaccurate): none

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| generalize_gr1 | 1 | 120.588647 s | 100.0% |

### arbiter_on_inpchange-n6

- Verdict/exit: REALIZABLE / 0
- Total wall: 52.153 s
- cgroup `memory.peak`: 1.99 GiB
- Censored stages: none
- Checker `--stats`: unsupported
- Support width min/median/max: 39 / 39 / 39
- Owner-tuple arity histogram: 0: 16, 1: 667
- Subset reuse: 35 distinct; 35 reused; uses min/median/max 53 / 63 / 116; operations instantiation: 1475, projection: 1160
- Mask word-length histogram: 1: 348, 2: 812
- Actual checker mode counts: 22: 1, 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 36.722865 s | 70.4% |
| from_aag | 348 | 2.975195 s | 5.7% |
| bdd_projection_relabel | 4839 | 2.033405 s | 3.9% |
| export | 4510 | 1.455218 s | 2.8% |
| seed_solves | 6 | 1.157309 s | 2.2% |
| instantiate_templates | 116 | 0.949998 s | 1.8% |
| policy_construction_skolemization | 2 | 0.755092 s | 1.4% |
| seed_monitor_construction | 6 | 0.443263 s | 0.8% |
| target_monitor_construction | 2 | 0.166735 s | 0.3% |
| projection_metadata | 2668 | 0.127462 s | 0.2% |
| support_extraction | 2635 | 0.077788 s | 0.1% |
| variable_cube_construction | 1044 | 0.034480 s | 0.1% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_on_inpchange-n7

- Verdict/exit: UNKNOWN / 2
- Total wall: 120.235 s
- cgroup `memory.peak`: 3.56 GiB
- Censored stages: target_check ≥ 103.287 s (absolute_deadline_exhausted, sigterm)
- Checker `--stats`: unsupported
- Support width min/median/max: 39 / 39 / 39
- Owner-tuple arity histogram: 0: 16, 1: 690
- Subset reuse: 41 distinct; 41 reused; uses min/median/max 53 / 73 / 126; operations instantiation: 2063, projection: 1260
- Mask word-length histogram: 1: 378, 2: 882
- Actual checker mode counts: 22: 1, 30: 2

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| from_aag | 378 | 3.184425 s | 2.6% |
| bdd_projection_relabel | 5717 | 2.572956 s | 2.1% |
| export | 5334 | 2.021330 s | 1.7% |
| instantiate_templates | 126 | 1.375398 s | 1.1% |
| seed_solves | 6 | 1.169725 s | 1.0% |
| policy_construction_skolemization | 2 | 1.048525 s | 0.9% |
| seed_monitor_construction | 6 | 0.442640 s | 0.4% |
| target_monitor_construction | 2 | 0.173733 s | 0.1% |
| projection_metadata | 2898 | 0.120163 s | 0.1% |
| support_extraction | 3323 | 0.098927 s | 0.1% |
| variable_cube_construction | 1134 | 0.037196 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_check | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### round_robin_arbiter_unreal2-n5

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 2.096 s
- cgroup `memory.peak`: 1.35 GiB
- Censored stages: none
- Checker `--stats`: unsupported
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 60, 2: 40
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 16: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 1.008882 s | 48.1% |
| target_solve | 1 | 0.831901 s | 39.7% |
| target_monitor_construction | 1 | 0.078350 s | 3.7% |
| bdd_projection_relabel | 0 | 0.000000 s | 0.0% |
| export | 0 | 0.000000 s | 0.0% |
| from_aag | 0 | 0.000000 s | 0.0% |
| instantiate_templates | 0 | 0.000000 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| projection_metadata | 0 | 0.000000 s | 0.0% |
| seed_monitor_construction | 0 | 0.000000 s | 0.0% |
| seed_solves | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| support_extraction | 0 | 0.000000 s | 0.0% |
| variable_cube_construction | 0 | 0.000000 s | 0.0% |

### round_robin_arbiter_unreal2-n6

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 7.807 s
- cgroup `memory.peak`: 1.61 GiB
- Censored stages: none
- Checker `--stats`: unsupported
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 72, 2: 60
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 19: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 3.959088 s | 50.7% |
| target_solve | 1 | 3.578344 s | 45.8% |
| target_monitor_construction | 1 | 0.086661 s | 1.1% |
| bdd_projection_relabel | 0 | 0.000000 s | 0.0% |
| export | 0 | 0.000000 s | 0.0% |
| from_aag | 0 | 0.000000 s | 0.0% |
| instantiate_templates | 0 | 0.000000 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| projection_metadata | 0 | 0.000000 s | 0.0% |
| seed_monitor_construction | 0 | 0.000000 s | 0.0% |
| seed_solves | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| support_extraction | 0 | 0.000000 s | 0.0% |
| variable_cube_construction | 0 | 0.000000 s | 0.0% |

### round_robin_arbiter_unreal2-n7

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 33.507 s
- cgroup `memory.peak`: 2.74 GiB
- Censored stages: none
- Checker `--stats`: unsupported
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 84, 2: 84
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 22: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 18.249909 s | 54.5% |
| target_solve | 1 | 14.984439 s | 44.7% |
| target_monitor_construction | 1 | 0.093598 s | 0.3% |
| bdd_projection_relabel | 0 | 0.000000 s | 0.0% |
| export | 0 | 0.000000 s | 0.0% |
| from_aag | 0 | 0.000000 s | 0.0% |
| instantiate_templates | 0 | 0.000000 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| projection_metadata | 0 | 0.000000 s | 0.0% |
| seed_monitor_construction | 0 | 0.000000 s | 0.0% |
| seed_solves | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| support_extraction | 0 | 0.000000 s | 0.0% |
| variable_cube_construction | 0 | 0.000000 s | 0.0% |

### collector_v1-n11

- Verdict/exit: UNKNOWN / 2
- Total wall: 119.975 s
- cgroup `memory.peak`: 2.10 GiB
- Censored stages: canonicalize ≥ 119.675 s (subprocess_timeout); target_monitor_construction ≥ 119.675 s (subprocess_timeout)
- Checker `--stats`: unsupported
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 42, 1: 12
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: none

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_monitor_construction | 1 | 119.674578 s | 99.7% |
| seed_monitor_construction | 1 | 0.073946 s | 0.1% |
| seed_solves | 1 | 0.043889 s | 0.0% |
| bdd_projection_relabel | 0 | 0.000000 s | 0.0% |
| export | 0 | 0.000000 s | 0.0% |
| from_aag | 0 | 0.000000 s | 0.0% |
| instantiate_templates | 0 | 0.000000 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| projection_metadata | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| support_extraction | 0 | 0.000000 s | 0.0% |
| target_check | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |
| variable_cube_construction | 0 | 0.000000 s | 0.0% |

### arbiter-n6

- Verdict/exit: REALIZABLE / 0
- Total wall: 2.254 s
- cgroup `memory.peak`: 963.12 MiB
- Censored stages: none
- Checker `--stats`: unsupported
- Support width min/median/max: 19 / 19 / 19
- Owner-tuple arity histogram: 0: 12, 1: 325
- Subset reuse: 34 distinct; 34 reused; uses min/median/max 53 / 63 / 116; operations instantiation: 1475, projection: 1044
- Mask word-length histogram: 1: 1044
- Actual checker mode counts: 22: 1, 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| bdd_projection_relabel | 4607 | 0.343454 s | 15.2% |
| seed_monitor_construction | 4 | 0.272050 s | 12.1% |
| export | 4510 | 0.267230 s | 11.9% |
| instantiate_templates | 116 | 0.228141 s | 10.1% |
| target_check | 1 | 0.206679 s | 9.2% |
| from_aag | 232 | 0.203081 s | 9.0% |
| seed_solves | 4 | 0.183305 s | 8.1% |
| policy_construction_skolemization | 2 | 0.150024 s | 6.7% |
| target_monitor_construction | 2 | 0.145693 s | 6.5% |
| projection_metadata | 2320 | 0.056548 s | 2.5% |
| support_extraction | 2519 | 0.038779 s | 1.7% |
| variable_cube_construction | 1044 | 0.015927 s | 0.7% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### prioritized_arbiter-n7

- Verdict/exit: REALIZABLE / 0
- Total wall: 1.042 s
- cgroup `memory.peak`: 931.68 MiB
- Censored stages: none
- Checker `--stats`: unsupported
- Support width min/median/max: 7 / 9 / 9
- Owner-tuple arity histogram: 0: 60, 1: 156
- Subset reuse: 19 distinct; 19 reused; uses min/median/max 30 / 40 / 70; operations instantiation: 430, projection: 490
- Mask word-length histogram: 1: 490
- Actual checker mode counts: 13: 1, 17: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| seed_monitor_construction | 4 | 0.259290 s | 24.9% |
| target_monitor_construction | 2 | 0.135678 s | 13.0% |
| target_check | 1 | 0.092636 s | 8.9% |
| seed_solves | 4 | 0.090917 s | 8.7% |
| bdd_projection_relabel | 2256 | 0.044761 s | 4.3% |
| instantiate_templates | 70 | 0.022599 s | 2.2% |
| policy_construction_skolemization | 2 | 0.020191 s | 1.9% |
| from_aag | 140 | 0.014537 s | 1.4% |
| projection_metadata | 1120 | 0.013812 s | 1.3% |
| support_extraction | 1276 | 0.011184 s | 1.1% |
| export | 236 | 0.010816 s | 1.0% |
| variable_cube_construction | 492 | 0.005348 s | 0.5% |
| substitute_variables | 70 | 0.004135 s | 0.4% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |
