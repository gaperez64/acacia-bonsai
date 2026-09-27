# S0 diagnostic summary

Source: `/home/gperez/GIT-repos/acacia-gr1-par2-timing/benchmarking/gr1-par2-20260923/s0/raw-p2d-region`

## Notes

- End-to-end: tlsf-tools build-P5-9212e2b + generalizer 17b2b209 + adapter d5ccf8f9, --real-check region, timing worktree, serial, 120 s, 8 GiB

## Targets

| Target | Verdict | Exit | Total wall | cgroup memory.peak | Censored stage |
|---|---:|---:|---:|---:|---|
| arbiter_with_cancel-n8 | REALIZABLE | 0 | 20.963 s | 1.17 GiB | none |
| arbiter_with_cancel-n9 | UNKNOWN | 2 | 120.091 s | 1.83 GiB | target_check ≥ 0.000 s |
| arbiter_with_cancel-n10 | UNKNOWN | 2 | 120.178 s | 1.79 GiB | target_check ≥ 117.466 s |
| load_balancer_unreal2-n6 | UNREALIZABLE | 1 | 2.181 s | 1.32 GiB | none |
| load_balancer_unreal2-n7 | UNREALIZABLE | 1 | 11.275 s | 1.59 GiB | none |
| load_balancer-n8 | REALIZABLE | 0 | 7.259 s | 930.29 MiB | none |
| load_balancer-n9 | REALIZABLE | 0 | 17.792 s | 1.33 GiB | none |
| amba_decomposed_lock-n15 | REALIZABLE | 0 | 12.852 s | 1.55 GiB | none |
| arbiter_with_buffer-n8 | REALIZABLE | 0 | 2.954 s | 721.16 MiB | none |
| arbiter_with_buffer-n9 | REALIZABLE | 0 | 9.123 s | 1.20 GiB | none |
| arbiter_on_inpchange-n6 | UNKNOWN | 2 | 99.436 s | 1.62 GiB | none |
| arbiter_on_inpchange-n7 | UNKNOWN | 2 | 96.450 s | 1.64 GiB | none |
| round_robin_arbiter_unreal2-n5 | UNREALIZABLE | 1 | 2.038 s | 1.35 GiB | none |
| round_robin_arbiter_unreal2-n6 | UNREALIZABLE | 1 | 7.233 s | 1.55 GiB | none |
| round_robin_arbiter_unreal2-n7 | UNREALIZABLE | 1 | 31.309 s | 2.49 GiB | none |
| collector_v1-n11 | UNKNOWN | 2 | 119.972 s | 2.13 GiB | none |
| arbiter-n6 | REALIZABLE | 0 | 1.320 s | 692.46 MiB | none |
| prioritized_arbiter-n7 | REALIZABLE | 0 | 0.819 s | 681.26 MiB | none |

## Cross-target phase dominance

| Target | Dominant phase | Second | Third |
|---|---|---|---|
| arbiter_with_cancel-n8 | target_check — 18.966 s (90.5%) | candidate_builder — 1.780 s (8.5%) | instantiate_templates — 0.371 s (1.8%) |
| arbiter_with_cancel-n9 | n/a | n/a | n/a |
| arbiter_with_cancel-n10 | target_check — ≥ 117.466 s (≥ 97.7%) | candidate_builder — 2.505 s (2.1%) | instantiate_templates — 0.750 s (0.6%) |
| load_balancer_unreal2-n6 | target_solve — 1.456 s (66.7%) | target_check — 0.419 s (19.2%) | target_monitor_construction — 0.085 s (3.9%) |
| load_balancer_unreal2-n7 | target_solve — 9.545 s (84.7%) | target_check — 1.384 s (12.3%) | target_monitor_construction — 0.093 s (0.8%) |
| load_balancer-n8 | target_check — 3.620 s (49.9%) | candidate_builder — 3.409 s (47.0%) | substitute_variables — 0.929 s (12.8%) |
| load_balancer-n9 | target_check — 9.908 s (55.7%) | candidate_builder — 7.643 s (43.0%) | substitute_variables — 3.398 s (19.1%) |
| amba_decomposed_lock-n15 | target_check — 6.574 s (51.2%) | candidate_builder — 6.054 s (47.1%) | substitute_variables — 4.104 s (31.9%) |
| arbiter_with_buffer-n8 | candidate_builder — 2.404 s (81.4%) | export — 1.335 s (45.2%) | target_check — 0.326 s (11.0%) |
| arbiter_with_buffer-n9 | candidate_builder — 7.750 s (84.9%) | export — 4.950 s (54.3%) | target_check — 1.128 s (12.4%) |
| arbiter_on_inpchange-n6 | target_check — 95.656 s (96.2%) | candidate_builder — 3.561 s (3.6%) | bdd_projection_relabel — 1.497 s (1.5%) |
| arbiter_on_inpchange-n7 | target_check — 92.127 s (95.5%) | candidate_builder — 4.107 s (4.3%) | bdd_projection_relabel — 1.766 s (1.8%) |
| round_robin_arbiter_unreal2-n5 | target_check — 0.915 s (44.9%) | target_solve — 0.826 s (40.5%) | target_monitor_construction — 0.080 s (3.9%) |
| round_robin_arbiter_unreal2-n6 | target_solve — 3.484 s (48.2%) | target_check — 3.442 s (47.6%) | target_monitor_construction — 0.087 s (1.2%) |
| round_robin_arbiter_unreal2-n7 | target_check — 16.039 s (51.2%) | target_solve — 14.954 s (47.8%) | target_monitor_construction — 0.096 s (0.3%) |
| collector_v1-n11 | candidate_builder — 119.761 s (99.8%) | n/a | n/a |
| arbiter-n6 | candidate_builder — 0.821 s (62.2%) | target_check — 0.280 s (21.2%) | seed_monitor_construction — 0.136 s (10.3%) |
| prioritized_arbiter-n7 | candidate_builder — 0.515 s (62.9%) | seed_monitor_construction — 0.129 s (15.7%) | target_check — 0.090 s (11.0%) |

## Per-target detail

### arbiter_with_cancel-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 20.963 s
- cgroup `memory.peak`: 1.17 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-8` (node cap 33554432): `{"aig_gates_visited":3250,"cache_cap":33554432,"final_status":"REGION_VERIFIED","node_cap":33554432,"peak_live_nodes_sample":16711681,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":18.847429787,"region_certificate_roots":278,"region_game_roots":180,"region_layer_seconds":1.344324875,"region_layers":166,"region_mode_seconds":18.846923279,"region_modes":50,"requested_roots":458,"setup_seconds":0.079511996,"successor_applications":166,"successor_substitutions":50}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 50: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 18.965972 s | 90.5% |
| candidate_builder | 1 | 1.779687 s | 8.5% |
| instantiate_templates | 115 | 0.371430 s | 1.8% |
| seed_solves | 3 | 0.292607 s | 1.4% |
| bdd_projection_relabel | 6555 | 0.248367 s | 1.2% |
| export | 3212 | 0.241007 s | 1.1% |
| bdd_relabel_rename | 5520 | 0.239326 s | 1.1% |
| seed_monitor_construction | 3 | 0.214898 s | 1.0% |
| support_extraction | 7718 | 0.148030 s | 0.7% |
| target_monitor_construction | 1 | 0.088467 s | 0.4% |
| projection_metadata | 2645 | 0.056279 s | 0.3% |
| from_aag | 4 | 0.003987 s | 0.0% |
| bdd_existential_quantification | 1035 | 0.002658 s | 0.0% |
| variable_cube_construction | 9 | 0.000212 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_cancel-n9

- Verdict/exit: UNKNOWN / 2
- Total wall: 120.091 s
- cgroup `memory.peak`: 1.83 GiB
- Censored stages: target_check ≥ 0.000 s (absolute_deadline_exhausted)
- Checker `--stats`: no payload captured
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Legacy recorded mode counts (known inaccurate): none

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| n/a | 0 | 0.000000 s | 0.0% |

### arbiter_with_cancel-n10

- Verdict/exit: UNKNOWN / 2
- Total wall: 120.178 s
- cgroup `memory.peak`: 1.79 GiB
- Censored stages: target_check ≥ 117.466 s (absolute_deadline_exhausted, sigterm)
- Checker `--stats`: no payload captured
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 62: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 2.504652 s | 2.1% |
| instantiate_templates | 143 | 0.750241 s | 0.6% |
| export | 4812 | 0.467514 s | 0.4% |
| bdd_projection_relabel | 10582 | 0.428571 s | 0.4% |
| bdd_relabel_rename | 9295 | 0.414727 s | 0.3% |
| seed_solves | 3 | 0.291671 s | 0.2% |
| support_extraction | 12025 | 0.232563 s | 0.2% |
| seed_monitor_construction | 3 | 0.216435 s | 0.2% |
| target_monitor_construction | 1 | 0.094849 s | 0.1% |
| projection_metadata | 3289 | 0.069972 s | 0.1% |
| from_aag | 4 | 0.004316 s | 0.0% |
| bdd_existential_quantification | 1287 | 0.003362 s | 0.0% |
| variable_cube_construction | 9 | 0.000226 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_check | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### load_balancer_unreal2-n6

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 2.181 s
- cgroup `memory.peak`: 1.32 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 67108864): `{"aig_gates_visited":7831,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":786433,"policy_counter_constants":12,"policy_cross_mode_root_reuses":8,"policy_dependent_gates_per_mode":41,"policy_full_cache_modes":0,"policy_independent_gates":20,"policy_independent_roots":2,"policy_mode_builds":4,"policy_specialized_gates":164,"policy_unspecialized_gates":0,"proof_seconds":0.194127218,"requested_roots":1189,"setup_seconds":0.165753834,"successor_applications":2176,"successor_substitutions":4}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 12, 1: 46, 2: 70
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 4: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 1.455550 s | 66.7% |
| target_check | 1 | 0.419135 s | 19.2% |
| target_monitor_construction | 1 | 0.085133 s | 3.9% |
| bdd_existential_quantification | 0 | 0.000000 s | 0.0% |
| bdd_projection_relabel | 0 | 0.000000 s | 0.0% |
| bdd_relabel_rename | 0 | 0.000000 s | 0.0% |
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
- Total wall: 11.275 s
- cgroup `memory.peak`: 1.59 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 67108864): `{"aig_gates_visited":15334,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":3670017,"policy_counter_constants":12,"policy_cross_mode_root_reuses":8,"policy_dependent_gates_per_mode":41,"policy_full_cache_modes":0,"policy_independent_gates":20,"policy_independent_roots":2,"policy_mode_builds":4,"policy_specialized_gates":164,"policy_unspecialized_gates":0,"proof_seconds":1.034852944,"requested_roots":1494,"setup_seconds":0.206477139,"successor_applications":2752,"successor_substitutions":4}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 12, 1: 53, 2: 96
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 4: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 9.544668 s | 84.7% |
| target_check | 1 | 1.384245 s | 12.3% |
| target_monitor_construction | 1 | 0.093137 s | 0.8% |
| bdd_existential_quantification | 0 | 0.000000 s | 0.0% |
| bdd_projection_relabel | 0 | 0.000000 s | 0.0% |
| bdd_relabel_rename | 0 | 0.000000 s | 0.0% |
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
- Total wall: 7.259 s
- cgroup `memory.peak`: 930.29 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-8` (node cap 33554432): `{"aig_gates_visited":2281,"cache_cap":33554432,"final_status":"REGION_VERIFIED","node_cap":33554432,"peak_live_nodes_sample":7471105,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":3.493021275,"region_certificate_roots":258,"region_game_roots":98,"region_layer_seconds":0.235480617,"region_layers":206,"region_mode_seconds":3.492710062,"region_modes":26,"requested_roots":356,"setup_seconds":0.086048948,"successor_applications":344,"successor_substitutions":26}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 3.620445 s | 49.9% |
| candidate_builder | 1 | 3.409054 s | 47.0% |
| substitute_variables | 175 | 0.929490 s | 12.8% |
| export | 283 | 0.716831 s | 9.9% |
| instantiate_templates | 175 | 0.354331 s | 4.9% |
| seed_monitor_construction | 3 | 0.199035 s | 2.7% |
| bdd_projection_relabel | 10477 | 0.194996 s | 2.7% |
| bdd_relabel_rename | 8902 | 0.182121 s | 2.5% |
| support_extraction | 12801 | 0.118629 s | 1.6% |
| seed_solves | 3 | 0.088048 s | 1.2% |
| projection_metadata | 4025 | 0.081686 s | 1.1% |
| target_monitor_construction | 1 | 0.072894 s | 1.0% |
| from_aag | 4 | 0.005317 s | 0.1% |
| bdd_existential_quantification | 1575 | 0.003307 s | 0.0% |
| variable_cube_construction | 13 | 0.000916 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### load_balancer-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 17.792 s
- cgroup `memory.peak`: 1.33 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-9` (node cap 33554432): `{"aig_gates_visited":2604,"cache_cap":33554432,"final_status":"REGION_VERIFIED","node_cap":33554432,"peak_live_nodes_sample":18612225,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":9.709274598,"region_certificate_roots":289,"region_game_roots":108,"region_layer_seconds":0.598901777,"region_layers":230,"region_mode_seconds":9.708958498,"region_modes":29,"requested_roots":397,"setup_seconds":0.09646386,"successor_applications":384,"successor_substitutions":29}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 29: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 9.907645 s | 55.7% |
| candidate_builder | 1 | 7.642642 s | 43.0% |
| substitute_variables | 196 | 3.398500 s | 19.1% |
| export | 317 | 1.698381 s | 9.5% |
| instantiate_templates | 196 | 0.496269 s | 2.8% |
| bdd_projection_relabel | 13301 | 0.394643 s | 2.2% |
| bdd_relabel_rename | 11537 | 0.378464 s | 2.1% |
| seed_monitor_construction | 3 | 0.200327 s | 1.1% |
| support_extraction | 15894 | 0.146342 s | 0.8% |
| projection_metadata | 4508 | 0.089039 s | 0.5% |
| seed_solves | 3 | 0.084243 s | 0.5% |
| target_monitor_construction | 1 | 0.075734 s | 0.4% |
| from_aag | 4 | 0.005324 s | 0.0% |
| bdd_existential_quantification | 1764 | 0.003705 s | 0.0% |
| variable_cube_construction | 13 | 0.000170 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### amba_decomposed_lock-n15

- Verdict/exit: REALIZABLE / 0
- Total wall: 12.852 s
- cgroup `memory.peak`: 1.55 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-15` (node cap 67108864): `{"aig_gates_visited":1045,"cache_cap":67108864,"final_status":"REGION_VERIFIED","node_cap":67108864,"peak_live_nodes_sample":8192001,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":6.356277346,"region_certificate_roots":113,"region_game_roots":72,"region_layer_seconds":0.817557195,"region_layers":85,"region_mode_seconds":6.356185961,"region_modes":17,"requested_roots":185,"setup_seconds":0.175759455,"successor_applications":153,"successor_substitutions":17}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 17: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 6.574347 s | 51.2% |
| candidate_builder | 1 | 6.053754 s | 47.1% |
| substitute_variables | 65 | 4.104236 s | 31.9% |
| export | 129 | 0.746441 s | 5.8% |
| bdd_projection_relabel | 2961 | 0.306182 s | 2.4% |
| bdd_relabel_rename | 2376 | 0.302598 s | 2.4% |
| seed_monitor_construction | 3 | 0.198010 s | 1.5% |
| target_monitor_construction | 1 | 0.105471 s | 0.8% |
| seed_solves | 3 | 0.068624 s | 0.5% |
| instantiate_templates | 65 | 0.052960 s | 0.4% |
| support_extraction | 3798 | 0.032665 s | 0.3% |
| projection_metadata | 1365 | 0.020032 s | 0.2% |
| from_aag | 4 | 0.002153 s | 0.0% |
| bdd_existential_quantification | 585 | 0.000733 s | 0.0% |
| variable_cube_construction | 9 | 0.000126 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_buffer-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 2.954 s
- cgroup `memory.peak`: 721.16 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-8` (node cap 33554432): `{"aig_gates_visited":392,"cache_cap":33554432,"final_status":"REGION_VERIFIED","node_cap":33554432,"peak_live_nodes_sample":589825,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":0.195673794,"region_certificate_roots":46,"region_game_roots":44,"region_layer_seconds":0.030533051,"region_layers":30,"region_mode_seconds":0.195640273,"region_modes":10,"requested_roots":90,"setup_seconds":0.089682512,"successor_applications":30,"successor_substitutions":10}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 10: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 2.403757 s | 81.4% |
| export | 55 | 1.334733 s | 45.2% |
| target_check | 1 | 0.326197 s | 11.0% |
| seed_monitor_construction | 3 | 0.194882 s | 6.6% |
| substitute_variables | 19 | 0.134982 s | 4.6% |
| target_monitor_construction | 1 | 0.071591 s | 2.4% |
| seed_solves | 3 | 0.066984 s | 2.3% |
| bdd_projection_relabel | 764 | 0.066231 s | 2.2% |
| bdd_relabel_rename | 593 | 0.065304 s | 2.2% |
| support_extraction | 1037 | 0.012416 s | 0.4% |
| instantiate_templates | 19 | 0.007149 s | 0.2% |
| projection_metadata | 399 | 0.005464 s | 0.2% |
| from_aag | 4 | 0.001191 s | 0.0% |
| bdd_existential_quantification | 171 | 0.000199 s | 0.0% |
| variable_cube_construction | 9 | 0.000081 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_buffer-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 9.123 s
- cgroup `memory.peak`: 1.20 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-9` (node cap 33554432): `{"aig_gates_visited":455,"cache_cap":33554432,"final_status":"REGION_VERIFIED","node_cap":33554432,"peak_live_nodes_sample":2162689,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":0.857411211,"region_certificate_roots":51,"region_game_roots":49,"region_layer_seconds":0.115769994,"region_layers":33,"region_mode_seconds":0.85736272,"region_modes":11,"requested_roots":100,"setup_seconds":0.125003468,"successor_applications":33,"successor_substitutions":11}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 11: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 7.749696 s | 84.9% |
| export | 61 | 4.950278 s | 54.3% |
| target_check | 1 | 1.127733 s | 12.4% |
| substitute_variables | 21 | 0.841955 s | 9.2% |
| bdd_projection_relabel | 866 | 0.321677 s | 3.5% |
| bdd_relabel_rename | 677 | 0.320590 s | 3.5% |
| seed_monitor_construction | 3 | 0.195298 s | 2.1% |
| target_monitor_construction | 1 | 0.070692 s | 0.8% |
| seed_solves | 3 | 0.066736 s | 0.7% |
| support_extraction | 1168 | 0.034672 s | 0.4% |
| instantiate_templates | 21 | 0.008760 s | 0.1% |
| projection_metadata | 441 | 0.005716 s | 0.1% |
| from_aag | 4 | 0.001456 s | 0.0% |
| bdd_existential_quantification | 189 | 0.000237 s | 0.0% |
| variable_cube_construction | 9 | 0.000082 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_on_inpchange-n6

- Verdict/exit: UNKNOWN / 2
- Total wall: 99.436 s
- cgroup `memory.peak`: 1.62 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 33554432): `{"aig_gates_visited":11855,"cache_cap":33554432,"final_status":"UNKNOWN","node_cap":33554432,"peak_live_nodes_sample":33554945,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":95.512825638,"region_certificate_roots":150,"region_game_roots":154,"region_layer_seconds":0.0,"region_layers":0,"region_mode_seconds":95.511495693,"region_modes":1,"requested_roots":304,"setup_seconds":0.084699467,"successor_applications":3,"successor_substitutions":1}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 95.656048 s | 96.2% |
| candidate_builder | 1 | 3.560607 s | 3.6% |
| bdd_projection_relabel | 2772 | 1.496896 s | 1.5% |
| bdd_relabel_rename | 2205 | 1.487510 s | 1.5% |
| seed_solves | 3 | 0.617468 s | 0.6% |
| export | 1368 | 0.485381 s | 0.5% |
| instantiate_templates | 63 | 0.415222 s | 0.4% |
| seed_monitor_construction | 3 | 0.237842 s | 0.2% |
| support_extraction | 3417 | 0.104957 s | 0.1% |
| target_monitor_construction | 1 | 0.087248 s | 0.1% |
| projection_metadata | 1449 | 0.048051 s | 0.0% |
| from_aag | 4 | 0.022604 s | 0.0% |
| bdd_existential_quantification | 567 | 0.006229 s | 0.0% |
| variable_cube_construction | 9 | 0.000384 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_on_inpchange-n7

- Verdict/exit: UNKNOWN / 2
- Total wall: 96.450 s
- cgroup `memory.peak`: 1.64 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 33554432): `{"aig_gates_visited":15926,"cache_cap":33554432,"final_status":"UNKNOWN","node_cap":33554432,"peak_live_nodes_sample":33554945,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":91.978986654,"region_certificate_roots":174,"region_game_roots":179,"region_layer_seconds":0.0,"region_layers":0,"region_mode_seconds":91.97717747,"region_modes":1,"requested_roots":353,"setup_seconds":0.086674124,"successor_applications":3,"successor_substitutions":1}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 30: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 92.126671 s | 95.5% |
| candidate_builder | 1 | 4.107277 s | 4.3% |
| bdd_projection_relabel | 3650 | 1.765572 s | 1.8% |
| bdd_relabel_rename | 2993 | 1.755375 s | 1.8% |
| export | 1790 | 0.732563 s | 0.8% |
| instantiate_templates | 73 | 0.590017 s | 0.6% |
| seed_solves | 3 | 0.584955 s | 0.6% |
| seed_monitor_construction | 3 | 0.222641 s | 0.2% |
| support_extraction | 4395 | 0.131855 s | 0.1% |
| target_monitor_construction | 1 | 0.090799 s | 0.1% |
| projection_metadata | 1679 | 0.052509 s | 0.1% |
| from_aag | 4 | 0.021940 s | 0.0% |
| bdd_existential_quantification | 657 | 0.006316 s | 0.0% |
| variable_cube_construction | 9 | 0.000396 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### round_robin_arbiter_unreal2-n5

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 2.038 s
- cgroup `memory.peak`: 1.35 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-5` (node cap 67108864): `{"aig_gates_visited":141862,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":196609,"policy_counter_constants":240,"policy_cross_mode_root_reuses":270,"policy_dependent_gates_per_mode":132039,"policy_full_cache_modes":16,"policy_independent_gates":326,"policy_independent_roots":270,"policy_mode_builds":16,"policy_specialized_gates":0,"policy_unspecialized_gates":132039,"proof_seconds":0.482444249,"requested_roots":2754,"setup_seconds":0.176244116,"successor_applications":4736,"successor_substitutions":16}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 60, 2: 40
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 16: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 0.914873 s | 44.9% |
| target_solve | 1 | 0.825542 s | 40.5% |
| target_monitor_construction | 1 | 0.080042 s | 3.9% |
| bdd_existential_quantification | 0 | 0.000000 s | 0.0% |
| bdd_projection_relabel | 0 | 0.000000 s | 0.0% |
| bdd_relabel_rename | 0 | 0.000000 s | 0.0% |
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
- Total wall: 7.233 s
- cgroup `memory.peak`: 1.55 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 67108864): `{"aig_gates_visited":734342,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":1310721,"policy_counter_constants":342,"policy_cross_mode_root_reuses":1043,"policy_dependent_gates_per_mode":714747,"policy_full_cache_modes":19,"policy_independent_gates":1112,"policy_independent_roots":1043,"policy_mode_builds":19,"policy_specialized_gates":0,"policy_unspecialized_gates":714747,"proof_seconds":2.529560813,"requested_roots":5059,"setup_seconds":0.191027724,"successor_applications":7744,"successor_substitutions":19}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 72, 2: 60
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 19: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 3.484396 s | 48.2% |
| target_check | 1 | 3.442079 s | 47.6% |
| target_monitor_construction | 1 | 0.086844 s | 1.2% |
| bdd_existential_quantification | 0 | 0.000000 s | 0.0% |
| bdd_projection_relabel | 0 | 0.000000 s | 0.0% |
| bdd_relabel_rename | 0 | 0.000000 s | 0.0% |
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
- Total wall: 31.309 s
- cgroup `memory.peak`: 2.49 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 67108864): `{"aig_gates_visited":3646651,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":6553601,"policy_counter_constants":462,"policy_cross_mode_root_reuses":4120,"policy_dependent_gates_per_mode":3609146,"policy_full_cache_modes":22,"policy_independent_gates":4202,"policy_independent_roots":4120,"policy_mode_builds":22,"policy_specialized_gates":0,"policy_unspecialized_gates":3609146,"proof_seconds":13.944568799,"requested_roots":10210,"setup_seconds":0.232685383,"successor_applications":11832,"successor_substitutions":22}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 84, 2: 84
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 22: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 16.038557 s | 51.2% |
| target_solve | 1 | 14.954398 s | 47.8% |
| target_monitor_construction | 1 | 0.095568 s | 0.3% |
| bdd_existential_quantification | 0 | 0.000000 s | 0.0% |
| bdd_projection_relabel | 0 | 0.000000 s | 0.0% |
| bdd_relabel_rename | 0 | 0.000000 s | 0.0% |
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
- Total wall: 119.972 s
- cgroup `memory.peak`: 2.13 GiB
- Censored stages: none
- Checker `--stats`: no payload captured
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: none

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 119.761440 s | 99.8% |
| bdd_existential_quantification | 0 | 0.000000 s | 0.0% |
| bdd_projection_relabel | 0 | 0.000000 s | 0.0% |
| bdd_relabel_rename | 0 | 0.000000 s | 0.0% |
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
- Total wall: 1.320 s
- cgroup `memory.peak`: 692.46 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 33554432): `{"aig_gates_visited":1577,"cache_cap":33554432,"final_status":"REGION_VERIFIED","node_cap":33554432,"peak_live_nodes_sample":327681,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":0.189948923,"region_certificate_roots":150,"region_game_roots":94,"region_layer_seconds":0.019415327,"region_layers":90,"region_mode_seconds":0.189716063,"region_modes":26,"requested_roots":244,"setup_seconds":0.084356616,"successor_applications":90,"successor_substitutions":26}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 0.820738 s | 62.2% |
| target_check | 1 | 0.279809 s | 21.2% |
| seed_monitor_construction | 2 | 0.136423 s | 10.3% |
| seed_solves | 2 | 0.096501 s | 7.3% |
| export | 1368 | 0.086779 s | 6.6% |
| instantiate_templates | 63 | 0.076699 s | 5.8% |
| target_monitor_construction | 1 | 0.074043 s | 5.6% |
| support_extraction | 3223 | 0.050620 s | 3.8% |
| bdd_projection_relabel | 2646 | 0.045682 s | 3.5% |
| bdd_relabel_rename | 2079 | 0.041728 s | 3.2% |
| projection_metadata | 1260 | 0.027660 s | 2.1% |
| from_aag | 3 | 0.002815 s | 0.2% |
| bdd_existential_quantification | 567 | 0.001312 s | 0.1% |
| variable_cube_construction | 9 | 0.000157 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### prioritized_arbiter-n7

- Verdict/exit: REALIZABLE / 0
- Total wall: 0.819 s
- cgroup `memory.peak`: 681.26 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 33554432): `{"aig_gates_visited":281,"cache_cap":33554432,"final_status":"REGION_VERIFIED","node_cap":33554432,"peak_live_nodes_sample":1,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":0.00087875,"region_certificate_roots":95,"region_game_roots":54,"region_layer_seconds":0.000102284,"region_layers":58,"region_mode_seconds":0.000837302,"region_modes":17,"requested_roots":149,"setup_seconds":0.083810331,"successor_applications":116,"successor_substitutions":17}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 17: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 0.515249 s | 62.9% |
| seed_monitor_construction | 2 | 0.128632 s | 15.7% |
| target_check | 1 | 0.089998 s | 11.0% |
| target_monitor_construction | 1 | 0.069377 s | 8.5% |
| seed_solves | 2 | 0.047292 s | 5.8% |
| instantiate_templates | 40 | 0.014431 s | 1.8% |
| support_extraction | 1746 | 0.013918 s | 1.7% |
| bdd_projection_relabel | 1291 | 0.013725 s | 1.7% |
| bdd_relabel_rename | 1011 | 0.012209 s | 1.5% |
| projection_metadata | 640 | 0.008874 s | 1.1% |
| substitute_variables | 40 | 0.003143 s | 0.4% |
| export | 111 | 0.001061 s | 0.1% |
| from_aag | 3 | 0.000765 s | 0.1% |
| bdd_existential_quantification | 280 | 0.000358 s | 0.0% |
| variable_cube_construction | 7 | 0.000075 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |
