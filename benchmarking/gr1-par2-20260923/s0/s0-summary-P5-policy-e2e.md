# S0 diagnostic summary

Source: `/home/gperez/GIT-repos/acacia-gr1-par2-timing/benchmarking/gr1-par2-20260923/s0/raw-p5-policy`

## Notes

- End-to-end: tlsf-tools build-P5-9212e2b + generalizer ba98fb39 + adapter 0043e97b, --real-check policy, timing worktree, serial, 120 s, 8 GiB

## Targets

| Target | Verdict | Exit | Total wall | cgroup memory.peak | Censored stage |
|---|---:|---:|---:|---:|---|
| arbiter_with_cancel-n8 | REALIZABLE | 0 | 5.120 s | 935.47 MiB | none |
| arbiter_with_cancel-n9 | REALIZABLE | 0 | 9.459 s | 1.21 GiB | none |
| arbiter_with_cancel-n10 | REALIZABLE | 0 | 24.731 s | 1.67 GiB | none |
| load_balancer_unreal2-n6 | UNREALIZABLE | 1 | 2.190 s | 1.32 GiB | none |
| load_balancer_unreal2-n7 | UNREALIZABLE | 1 | 11.089 s | 1.59 GiB | none |
| load_balancer-n8 | REALIZABLE | 0 | 9.269 s | 704.32 MiB | none |
| load_balancer-n9 | REALIZABLE | 0 | 23.264 s | 735.94 MiB | none |
| amba_decomposed_lock-n15 | REALIZABLE | 0 | 36.610 s | 1.84 GiB | none |
| arbiter_with_buffer-n8 | REALIZABLE | 0 | 15.214 s | 715.45 MiB | none |
| arbiter_with_buffer-n9 | REALIZABLE | 0 | 116.296 s | 1.81 GiB | none |
| arbiter_on_inpchange-n6 | REALIZABLE | 0 | 17.473 s | 1.38 GiB | none |
| arbiter_on_inpchange-n7 | UNKNOWN | 2 | 120.150 s | 1.76 GiB | target_check ≥ 113.014 s |
| round_robin_arbiter_unreal2-n5 | UNREALIZABLE | 1 | 2.023 s | 1.35 GiB | none |
| round_robin_arbiter_unreal2-n6 | UNREALIZABLE | 1 | 7.203 s | 1.55 GiB | none |
| round_robin_arbiter_unreal2-n7 | UNREALIZABLE | 1 | 31.201 s | 2.49 GiB | none |
| collector_v1-n11 | UNKNOWN | 2 | 120.035 s | 2.13 GiB | candidate_builder ≥ 119.819 s; generalize_gr1 ≥ 0.000 s |
| arbiter-n6 | REALIZABLE | 0 | 1.340 s | 688.68 MiB | none |
| prioritized_arbiter-n7 | REALIZABLE | 0 | 0.803 s | 681.64 MiB | none |

## Cross-target phase dominance

| Target | Dominant phase | Second | Third |
|---|---|---|---|
| arbiter_with_cancel-n8 | target_check — 2.513 s (49.1%) | candidate_builder — 2.371 s (46.3%) | bdd_projection_relabel — 0.866 s (16.9%) |
| arbiter_with_cancel-n9 | target_check — 6.203 s (65.6%) | candidate_builder — 3.035 s (32.1%) | bdd_projection_relabel — 1.208 s (12.8%) |
| arbiter_with_cancel-n10 | target_check — 20.971 s (84.8%) | candidate_builder — 3.533 s (14.3%) | bdd_projection_relabel — 1.484 s (6.0%) |
| load_balancer_unreal2-n6 | target_solve — 1.471 s (67.2%) | target_check — 0.418 s (19.1%) | target_monitor_construction — 0.085 s (3.9%) |
| load_balancer_unreal2-n7 | target_solve — 9.435 s (85.1%) | target_check — 1.333 s (12.0%) | target_monitor_construction — 0.091 s (0.8%) |
| load_balancer-n8 | candidate_builder — 8.862 s (95.6%) | bdd_projection_relabel — 5.582 s (60.2%) | policy_construction_skolemization — 3.215 s (34.7%) |
| load_balancer-n9 | candidate_builder — 22.765 s (97.9%) | bdd_projection_relabel — 14.222 s (61.1%) | policy_construction_skolemization — 8.677 s (37.3%) |
| amba_decomposed_lock-n15 | candidate_builder — 29.384 s (80.3%) | bdd_projection_relabel — 22.201 s (60.6%) | policy_construction_skolemization — 12.022 s (32.8%) |
| arbiter_with_buffer-n8 | candidate_builder — 14.852 s (97.6%) | policy_construction_skolemization — 9.663 s (63.5%) | bdd_projection_relabel — 5.589 s (36.7%) |
| arbiter_with_buffer-n9 | candidate_builder — 115.780 s (99.6%) | policy_construction_skolemization — 98.276 s (84.5%) | bdd_projection_relabel — 20.534 s (17.7%) |
| arbiter_on_inpchange-n6 | target_check — 12.086 s (69.2%) | candidate_builder — 5.171 s (29.6%) | bdd_projection_relabel — 2.839 s (16.2%) |
| arbiter_on_inpchange-n7 | target_check — ≥ 113.014 s (≥ 94.1%) | candidate_builder — 6.933 s (5.8%) | bdd_projection_relabel — 4.015 s (3.3%) |
| round_robin_arbiter_unreal2-n5 | target_check — 0.907 s (44.8%) | target_solve — 0.820 s (40.5%) | target_monitor_construction — 0.081 s (4.0%) |
| round_robin_arbiter_unreal2-n6 | target_solve — 3.463 s (48.1%) | target_check — 3.442 s (47.8%) | target_monitor_construction — 0.085 s (1.2%) |
| round_robin_arbiter_unreal2-n7 | target_check — 15.940 s (51.1%) | target_solve — 14.945 s (47.9%) | target_monitor_construction — 0.095 s (0.3%) |
| collector_v1-n11 | candidate_builder — ≥ 239.642 s (≥ 199.6%) | n/a | n/a |
| arbiter-n6 | candidate_builder — 0.989 s (73.8%) | bdd_projection_relabel — 0.231 s (17.3%) | export — 0.166 s (12.4%) |
| prioritized_arbiter-n7 | candidate_builder — 0.504 s (62.7%) | seed_monitor_construction — 0.129 s (16.1%) | target_check — 0.089 s (11.1%) |

## Per-target detail

### arbiter_with_cancel-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 5.120 s
- cgroup `memory.peak`: 935.47 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-8` (node cap 33554432): `{"aig_gates_visited":70321,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":7667713,"policy_counter_constants":2450,"policy_cross_mode_root_reuses":8000,"policy_dependent_gates_per_mode":985,"policy_full_cache_modes":0,"policy_independent_gates":17821,"policy_independent_roots":160,"policy_mode_builds":50,"policy_specialized_gates":49250,"policy_unspecialized_gates":0,"proof_seconds":2.407453563,"requested_roots":3468,"setup_seconds":0.08264183,"successor_applications":282,"successor_substitutions":50}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 50: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 2.513331 s | 49.1% |
| candidate_builder | 1 | 2.371049 s | 46.3% |
| bdd_projection_relabel | 6555 | 0.865742 s | 16.9% |
| instantiate_templates | 115 | 0.589564 s | 11.5% |
| export | 6195 | 0.472991 s | 9.2% |
| seed_solves | 3 | 0.298758 s | 5.8% |
| policy_construction_skolemization | 1 | 0.252792 s | 4.9% |
| seed_monitor_construction | 3 | 0.217859 s | 4.3% |
| target_monitor_construction | 1 | 0.088444 s | 1.7% |
| projection_metadata | 2645 | 0.048874 s | 1.0% |
| support_extraction | 2313 | 0.043295 s | 0.8% |
| from_aag | 4 | 0.003909 s | 0.1% |
| variable_cube_construction | 9 | 0.000216 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_cancel-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 9.459 s
- cgroup `memory.peak`: 1.21 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-9` (node cap 33554432): `{"aig_gates_visited":95432,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":17825793,"policy_counter_constants":3080,"policy_cross_mode_root_reuses":11088,"policy_dependent_gates_per_mode":1216,"policy_full_cache_modes":0,"policy_independent_gates":23310,"policy_independent_roots":198,"policy_mode_builds":56,"policy_specialized_gates":68096,"policy_unspecialized_gates":0,"proof_seconds":6.084818593,"requested_roots":4296,"setup_seconds":0.082338776,"successor_applications":316,"successor_substitutions":56}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 56: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 6.203217 s | 65.6% |
| candidate_builder | 1 | 3.035411 s | 32.1% |
| bdd_projection_relabel | 8385 | 1.208306 s | 12.8% |
| instantiate_templates | 129 | 0.903013 s | 9.5% |
| export | 7687 | 0.739223 s | 7.8% |
| policy_construction_skolemization | 1 | 0.413122 s | 4.4% |
| seed_solves | 3 | 0.293191 s | 3.1% |
| seed_monitor_construction | 3 | 0.214098 s | 2.3% |
| target_monitor_construction | 1 | 0.092552 s | 1.0% |
| projection_metadata | 2967 | 0.054839 s | 0.6% |
| support_extraction | 2593 | 0.052331 s | 0.6% |
| from_aag | 4 | 0.004220 s | 0.0% |
| variable_cube_construction | 9 | 0.000229 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_cancel-n10

- Verdict/exit: REALIZABLE / 0
- Total wall: 24.731 s
- cgroup `memory.peak`: 1.67 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-10` (node cap 33554432): `{"aig_gates_visited":125759,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":31195137,"policy_counter_constants":3782,"policy_cross_mode_root_reuses":14880,"policy_dependent_gates_per_mode":1471,"policy_full_cache_modes":0,"policy_independent_gates":29678,"policy_independent_roots":240,"policy_mode_builds":62,"policy_specialized_gates":91202,"policy_unspecialized_gates":0,"proof_seconds":20.841501013,"requested_roots":5212,"setup_seconds":0.081196738,"successor_applications":350,"successor_substitutions":62}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 62: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 20.971147 s | 84.8% |
| candidate_builder | 1 | 3.532943 s | 14.3% |
| bdd_projection_relabel | 10582 | 1.483985 s | 6.0% |
| instantiate_templates | 143 | 1.189191 s | 4.8% |
| export | 9339 | 0.907488 s | 3.7% |
| policy_construction_skolemization | 1 | 0.478472 s | 1.9% |
| seed_solves | 3 | 0.294374 s | 1.2% |
| seed_monitor_construction | 3 | 0.218670 s | 0.9% |
| target_monitor_construction | 1 | 0.097909 s | 0.4% |
| projection_metadata | 3289 | 0.060160 s | 0.2% |
| support_extraction | 2873 | 0.055008 s | 0.2% |
| from_aag | 4 | 0.004385 s | 0.0% |
| variable_cube_construction | 9 | 0.000216 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### load_balancer_unreal2-n6

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 2.190 s
- cgroup `memory.peak`: 1.32 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 67108864): `{"aig_gates_visited":7831,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":786433,"policy_counter_constants":12,"policy_cross_mode_root_reuses":8,"policy_dependent_gates_per_mode":41,"policy_full_cache_modes":0,"policy_independent_gates":20,"policy_independent_roots":2,"policy_mode_builds":4,"policy_specialized_gates":164,"policy_unspecialized_gates":0,"proof_seconds":0.190722754,"requested_roots":1189,"setup_seconds":0.165870595,"successor_applications":2176,"successor_substitutions":4}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 12, 1: 46, 2: 70
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 4: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 1.471239 s | 67.2% |
| target_check | 1 | 0.417765 s | 19.1% |
| target_monitor_construction | 1 | 0.084537 s | 3.9% |
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

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 11.089 s
- cgroup `memory.peak`: 1.59 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 67108864): `{"aig_gates_visited":15334,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":3670017,"policy_counter_constants":12,"policy_cross_mode_root_reuses":8,"policy_dependent_gates_per_mode":41,"policy_full_cache_modes":0,"policy_independent_gates":20,"policy_independent_roots":2,"policy_mode_builds":4,"policy_specialized_gates":164,"policy_unspecialized_gates":0,"proof_seconds":0.997313325,"requested_roots":1494,"setup_seconds":0.1864053,"successor_applications":2752,"successor_substitutions":4}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 12, 1: 53, 2: 96
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 4: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 9.434814 s | 85.1% |
| target_check | 1 | 1.332564 s | 12.0% |
| target_monitor_construction | 1 | 0.090670 s | 0.8% |
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
- Total wall: 9.269 s
- cgroup `memory.peak`: 704.32 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-8` (node cap 33554432): `{"aig_gates_visited":78581,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":131073,"policy_counter_constants":650,"policy_cross_mode_root_reuses":11102,"policy_dependent_gates_per_mode":1887,"policy_full_cache_modes":0,"policy_independent_gates":27238,"policy_independent_roots":427,"policy_mode_builds":26,"policy_specialized_gates":49062,"policy_unspecialized_gates":0,"proof_seconds":0.05922321,"requested_roots":1641,"setup_seconds":0.090466975,"successor_applications":344,"successor_substitutions":26}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 8.862318 s | 95.6% |
| bdd_projection_relabel | 10527 | 5.582141 s | 60.2% |
| policy_construction_skolemization | 1 | 3.215060 s | 34.7% |
| substitute_variables | 175 | 0.971772 s | 10.5% |
| export | 316 | 0.755926 s | 8.2% |
| seed_monitor_construction | 3 | 0.200309 s | 2.2% |
| target_check | 1 | 0.178557 s | 1.9% |
| instantiate_templates | 175 | 0.171924 s | 1.9% |
| seed_solves | 3 | 0.085167 s | 0.9% |
| target_monitor_construction | 1 | 0.072584 s | 0.8% |
| projection_metadata | 4025 | 0.066841 s | 0.7% |
| support_extraction | 4124 | 0.056583 s | 0.6% |
| from_aag | 4 | 0.005253 s | 0.1% |
| variable_cube_construction | 14 | 0.000189 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### load_balancer-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 23.264 s
- cgroup `memory.peak`: 735.94 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-9` (node cap 33554432): `{"aig_gates_visited":154106,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":196609,"policy_counter_constants":812,"policy_cross_mode_root_reuses":23664,"policy_dependent_gates_per_mode":3198,"policy_full_cache_modes":0,"policy_independent_gates":58760,"policy_independent_roots":816,"policy_mode_builds":29,"policy_specialized_gates":92742,"policy_unspecialized_gates":0,"proof_seconds":0.093965274,"requested_roots":2286,"setup_seconds":0.098277936,"successor_applications":384,"successor_substitutions":29}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 29: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 22.765481 s | 97.9% |
| bdd_projection_relabel | 13357 | 14.222178 s | 61.1% |
| policy_construction_skolemization | 1 | 8.676988 s | 37.3% |
| substitute_variables | 196 | 3.513377 s | 15.1% |
| export | 354 | 1.890033 s | 8.1% |
| target_check | 1 | 0.262292 s | 1.1% |
| instantiate_templates | 196 | 0.234536 s | 1.0% |
| seed_monitor_construction | 3 | 0.199384 s | 0.9% |
| seed_solves | 3 | 0.084009 s | 0.4% |
| support_extraction | 4609 | 0.083708 s | 0.4% |
| projection_metadata | 4508 | 0.076504 s | 0.3% |
| target_monitor_construction | 1 | 0.074148 s | 0.3% |
| from_aag | 4 | 0.005517 s | 0.0% |
| variable_cube_construction | 14 | 0.000196 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### amba_decomposed_lock-n15

- Verdict/exit: REALIZABLE / 0
- Total wall: 36.610 s
- cgroup `memory.peak`: 1.84 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-15` (node cap 67108864): `{"aig_gates_visited":397545,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":17170433,"policy_counter_constants":272,"policy_cross_mode_root_reuses":833,"policy_dependent_gates_per_mode":171,"policy_full_cache_modes":0,"policy_independent_gates":393593,"policy_independent_roots":49,"policy_mode_builds":17,"policy_specialized_gates":2907,"policy_unspecialized_gates":0,"proof_seconds":6.760403004,"requested_roots":523,"setup_seconds":0.162838777,"successor_applications":153,"successor_substitutions":17}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 17: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 29.384003 s | 80.3% |
| bdd_projection_relabel | 2993 | 22.201149 s | 60.6% |
| policy_construction_skolemization | 1 | 12.021510 s | 32.8% |
| target_check | 1 | 7.002555 s | 19.1% |
| substitute_variables | 65 | 4.565868 s | 12.5% |
| export | 146 | 1.549516 s | 4.2% |
| seed_monitor_construction | 3 | 0.196580 s | 0.5% |
| target_monitor_construction | 1 | 0.105832 s | 0.3% |
| seed_solves | 3 | 0.066714 s | 0.2% |
| instantiate_templates | 65 | 0.034105 s | 0.1% |
| support_extraction | 1454 | 0.018474 s | 0.1% |
| projection_metadata | 1365 | 0.016030 s | 0.0% |
| from_aag | 4 | 0.002265 s | 0.0% |
| variable_cube_construction | 10 | 0.000068 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_buffer-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 15.214 s
- cgroup `memory.peak`: 715.45 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-8` (node cap 33554432): `{"aig_gates_visited":13346,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":1,"policy_counter_constants":90,"policy_cross_mode_root_reuses":2720,"policy_dependent_gates_per_mode":883,"policy_full_cache_modes":0,"policy_independent_gates":4124,"policy_independent_roots":272,"policy_mode_builds":10,"policy_specialized_gates":8830,"policy_unspecialized_gates":0,"proof_seconds":0.005745703,"requested_roots":612,"setup_seconds":0.089820467,"successor_applications":50,"successor_substitutions":10}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 10: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 14.852159 s | 97.6% |
| policy_construction_skolemization | 1 | 9.663229 s | 63.5% |
| bdd_projection_relabel | 782 | 5.588734 s | 36.7% |
| export | 80 | 1.468055 s | 9.6% |
| seed_monitor_construction | 3 | 0.193413 s | 1.3% |
| substitute_variables | 19 | 0.145150 s | 1.0% |
| target_check | 1 | 0.136785 s | 0.9% |
| target_monitor_construction | 1 | 0.070831 s | 0.5% |
| seed_solves | 3 | 0.068425 s | 0.4% |
| support_extraction | 462 | 0.018269 s | 0.1% |
| projection_metadata | 399 | 0.003733 s | 0.0% |
| instantiate_templates | 19 | 0.003398 s | 0.0% |
| from_aag | 4 | 0.001182 s | 0.0% |
| variable_cube_construction | 10 | 0.000124 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_buffer-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 116.296 s
- cgroup `memory.peak`: 1.81 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-9` (node cap 33554432): `{"aig_gates_visited":26959,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":1,"policy_counter_constants":110,"policy_cross_mode_root_reuses":5830,"policy_dependent_gates_per_mode":1669,"policy_full_cache_modes":0,"policy_independent_gates":8145,"policy_independent_roots":530,"policy_mode_builds":11,"policy_specialized_gates":18359,"policy_unspecialized_gates":0,"proof_seconds":0.009705128,"requested_roots":938,"setup_seconds":0.125560317,"successor_applications":55,"successor_substitutions":11}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 11: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 115.779626 s | 99.6% |
| policy_construction_skolemization | 1 | 98.276324 s | 84.5% |
| bdd_projection_relabel | 886 | 20.534449 s | 17.7% |
| export | 89 | 4.729093 s | 4.1% |
| substitute_variables | 21 | 0.941466 s | 0.8% |
| target_check | 1 | 0.272971 s | 0.2% |
| seed_monitor_construction | 3 | 0.193274 s | 0.2% |
| target_monitor_construction | 1 | 0.071008 s | 0.1% |
| seed_solves | 3 | 0.066070 s | 0.1% |
| support_extraction | 511 | 0.064811 s | 0.1% |
| instantiate_templates | 21 | 0.004423 s | 0.0% |
| projection_metadata | 441 | 0.004206 s | 0.0% |
| from_aag | 4 | 0.001466 s | 0.0% |
| variable_cube_construction | 10 | 0.000128 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_on_inpchange-n6

- Verdict/exit: REALIZABLE / 0
- Total wall: 17.473 s
- cgroup `memory.peak`: 1.38 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 33554432): `{"aig_gates_visited":59768,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":22675457,"policy_counter_constants":650,"policy_cross_mode_root_reuses":2496,"policy_dependent_gates_per_mode":403,"policy_full_cache_modes":0,"policy_independent_gates":37435,"policy_independent_roots":96,"policy_mode_builds":26,"policy_specialized_gates":10478,"policy_unspecialized_gates":0,"proof_seconds":11.955812065,"requested_roots":1206,"setup_seconds":0.08356714,"successor_applications":154,"successor_substitutions":26}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 12.086039 s | 69.2% |
| candidate_builder | 1 | 5.170643 s | 29.6% |
| bdd_projection_relabel | 2772 | 2.838537 s | 16.2% |
| instantiate_templates | 63 | 1.536464 s | 8.8% |
| export | 2611 | 0.947756 s | 5.4% |
| seed_solves | 3 | 0.581362 s | 3.3% |
| policy_construction_skolemization | 1 | 0.487573 s | 2.8% |
| seed_monitor_construction | 3 | 0.223143 s | 1.3% |
| target_monitor_construction | 1 | 0.086118 s | 0.5% |
| projection_metadata | 1449 | 0.041980 s | 0.2% |
| support_extraction | 1275 | 0.040017 s | 0.2% |
| from_aag | 4 | 0.022313 s | 0.1% |
| variable_cube_construction | 9 | 0.000447 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_on_inpchange-n7

- Verdict/exit: UNKNOWN / 2
- Total wall: 120.150 s
- cgroup `memory.peak`: 1.76 GiB
- Censored stages: target_check ≥ 113.014 s (absolute_deadline_exhausted, sigterm)
- Checker `--stats`: no payload captured
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 30: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 6.933397 s | 5.8% |
| bdd_projection_relabel | 3650 | 4.014637 s | 3.3% |
| instantiate_templates | 73 | 2.496788 s | 2.1% |
| export | 3435 | 1.472201 s | 1.2% |
| policy_construction_skolemization | 1 | 0.749901 s | 0.6% |
| seed_solves | 3 | 0.592107 s | 0.5% |
| seed_monitor_construction | 3 | 0.223196 s | 0.2% |
| target_monitor_construction | 1 | 0.090220 s | 0.1% |
| projection_metadata | 1679 | 0.047225 s | 0.0% |
| support_extraction | 1475 | 0.043891 s | 0.0% |
| from_aag | 4 | 0.021615 s | 0.0% |
| variable_cube_construction | 9 | 0.000390 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_check | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### round_robin_arbiter_unreal2-n5

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 2.023 s
- cgroup `memory.peak`: 1.35 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-5` (node cap 67108864): `{"aig_gates_visited":141862,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":196609,"policy_counter_constants":240,"policy_cross_mode_root_reuses":270,"policy_dependent_gates_per_mode":132039,"policy_full_cache_modes":16,"policy_independent_gates":326,"policy_independent_roots":270,"policy_mode_builds":16,"policy_specialized_gates":0,"policy_unspecialized_gates":132039,"proof_seconds":0.480969567,"requested_roots":2754,"setup_seconds":0.171913965,"successor_applications":4736,"successor_substitutions":16}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 60, 2: 40
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 16: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 0.907084 s | 44.8% |
| target_solve | 1 | 0.819677 s | 40.5% |
| target_monitor_construction | 1 | 0.080681 s | 4.0% |
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
- Total wall: 7.203 s
- cgroup `memory.peak`: 1.55 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 67108864): `{"aig_gates_visited":734342,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":1310721,"policy_counter_constants":342,"policy_cross_mode_root_reuses":1043,"policy_dependent_gates_per_mode":714747,"policy_full_cache_modes":19,"policy_independent_gates":1112,"policy_independent_roots":1043,"policy_mode_builds":19,"policy_specialized_gates":0,"policy_unspecialized_gates":714747,"proof_seconds":2.495845135,"requested_roots":5059,"setup_seconds":0.192988051,"successor_applications":7744,"successor_substitutions":19}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 72, 2: 60
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 19: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 3.462874 s | 48.1% |
| target_check | 1 | 3.441562 s | 47.8% |
| target_monitor_construction | 1 | 0.085300 s | 1.2% |
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
- Total wall: 31.201 s
- cgroup `memory.peak`: 2.49 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 67108864): `{"aig_gates_visited":3646651,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":6553601,"policy_counter_constants":462,"policy_cross_mode_root_reuses":4120,"policy_dependent_gates_per_mode":3609146,"policy_full_cache_modes":22,"policy_independent_gates":4202,"policy_independent_roots":4120,"policy_mode_builds":22,"policy_specialized_gates":0,"policy_unspecialized_gates":3609146,"proof_seconds":13.907284053,"requested_roots":10210,"setup_seconds":0.230653156,"successor_applications":11832,"successor_substitutions":22}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 84, 2: 84
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 22: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 15.940253 s | 51.1% |
| target_solve | 1 | 14.945149 s | 47.9% |
| target_monitor_construction | 1 | 0.094595 s | 0.3% |
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
- Total wall: 120.035 s
- cgroup `memory.peak`: 2.13 GiB
- Censored stages: candidate_builder ≥ 119.819 s (sigterm); generalize_gr1 ≥ 0.000 s (absolute_deadline_exhausted)
- Checker `--stats`: no payload captured
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: none

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 119.822079 s | 99.8% |
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
| target_check | 0 | 0.000000 s | 0.0% |
| target_monitor_construction | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |
| variable_cube_construction | 0 | 0.000000 s | 0.0% |

### arbiter-n6

- Verdict/exit: REALIZABLE / 0
- Total wall: 1.340 s
- cgroup `memory.peak`: 688.68 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 33554432): `{"aig_gates_visited":19687,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":131073,"policy_counter_constants":650,"policy_cross_mode_root_reuses":2340,"policy_dependent_gates_per_mode":403,"policy_full_cache_modes":0,"policy_independent_gates":7632,"policy_independent_roots":90,"policy_mode_builds":26,"policy_specialized_gates":10478,"policy_unspecialized_gates":0,"proof_seconds":0.05326695,"requested_roots":1140,"setup_seconds":0.078750044,"successor_applications":154,"successor_substitutions":26}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 0.989184 s | 73.8% |
| bdd_projection_relabel | 2646 | 0.231370 s | 17.3% |
| export | 2611 | 0.165947 s | 12.4% |
| target_check | 1 | 0.137539 s | 10.3% |
| seed_monitor_construction | 2 | 0.136075 s | 10.2% |
| instantiate_templates | 63 | 0.127620 s | 9.5% |
| seed_solves | 2 | 0.092072 s | 6.9% |
| policy_construction_skolemization | 1 | 0.089656 s | 6.7% |
| target_monitor_construction | 1 | 0.072636 s | 5.4% |
| projection_metadata | 1260 | 0.023817 s | 1.8% |
| support_extraction | 1144 | 0.019230 s | 1.4% |
| from_aag | 3 | 0.002839 s | 0.2% |
| variable_cube_construction | 9 | 0.000161 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### prioritized_arbiter-n7

- Verdict/exit: REALIZABLE / 0
- Total wall: 0.803 s
- cgroup `memory.peak`: 681.64 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 33554432): `{"aig_gates_visited":19751,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":1,"policy_counter_constants":272,"policy_cross_mode_root_reuses":2499,"policy_dependent_gates_per_mode":982,"policy_full_cache_modes":0,"policy_independent_gates":2776,"policy_independent_roots":147,"policy_mode_builds":17,"policy_specialized_gates":16694,"policy_unspecialized_gates":0,"proof_seconds":0.005888236,"requested_roots":704,"setup_seconds":0.077918596,"successor_applications":116,"successor_substitutions":17}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 17: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 0.503686 s | 62.7% |
| seed_monitor_construction | 2 | 0.129232 s | 16.1% |
| target_check | 1 | 0.088841 s | 11.1% |
| target_monitor_construction | 1 | 0.068156 s | 8.5% |
| seed_solves | 2 | 0.044515 s | 5.5% |
| bdd_projection_relabel | 1323 | 0.028441 s | 3.5% |
| policy_construction_skolemization | 1 | 0.017610 s | 2.2% |
| instantiate_templates | 40 | 0.009651 s | 1.2% |
| export | 135 | 0.007151 s | 0.9% |
| projection_metadata | 640 | 0.006432 s | 0.8% |
| support_extraction | 767 | 0.006392 s | 0.8% |
| substitute_variables | 40 | 0.003384 s | 0.4% |
| from_aag | 3 | 0.000776 s | 0.1% |
| variable_cube_construction | 8 | 0.000092 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |
