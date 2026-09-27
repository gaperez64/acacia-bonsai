# S0 diagnostic summary

Source: `/home/gperez/GIT-repos/acacia-gr1-par2-timing/benchmarking/gr1-par2-20260923/s0/raw-p2d-policy`

## Notes

- End-to-end: tlsf-tools build-P5-9212e2b + generalizer 17b2b209 + adapter d5ccf8f9, --real-check policy, timing worktree, serial, 120 s, 8 GiB

## Targets

| Target | Verdict | Exit | Total wall | cgroup memory.peak | Censored stage |
|---|---:|---:|---:|---:|---|
| arbiter_with_cancel-n8 | REALIZABLE | 0 | 4.783 s | 935.81 MiB | none |
| arbiter_with_cancel-n9 | REALIZABLE | 0 | 9.061 s | 1.21 GiB | none |
| arbiter_with_cancel-n10 | REALIZABLE | 0 | 24.629 s | 1.67 GiB | none |
| load_balancer_unreal2-n6 | UNREALIZABLE | 1 | 2.261 s | 1.32 GiB | none |
| load_balancer_unreal2-n7 | UNREALIZABLE | 1 | 11.238 s | 1.59 GiB | none |
| load_balancer-n8 | REALIZABLE | 0 | 4.581 s | 704.20 MiB | none |
| load_balancer-n9 | REALIZABLE | 0 | 10.195 s | 736.53 MiB | none |
| amba_decomposed_lock-n15 | REALIZABLE | 0 | 14.475 s | 1.84 GiB | none |
| arbiter_with_buffer-n8 | REALIZABLE | 0 | 9.797 s | 715.43 MiB | none |
| arbiter_with_buffer-n9 | REALIZABLE | 0 | 97.102 s | 1.73 GiB | none |
| arbiter_on_inpchange-n6 | REALIZABLE | 0 | 16.447 s | 1.38 GiB | none |
| arbiter_on_inpchange-n7 | UNKNOWN | 2 | 120.157 s | 1.76 GiB | target_check ≥ 115.031 s |
| round_robin_arbiter_unreal2-n5 | UNREALIZABLE | 1 | 2.036 s | 1.35 GiB | none |
| round_robin_arbiter_unreal2-n6 | UNREALIZABLE | 1 | 7.177 s | 1.55 GiB | none |
| round_robin_arbiter_unreal2-n7 | UNREALIZABLE | 1 | 30.932 s | 2.49 GiB | none |
| collector_v1-n11 | UNKNOWN | 2 | 120.035 s | 2.13 GiB | canonicalize ≥ 119.613 s |
| arbiter-n6 | REALIZABLE | 0 | 1.258 s | 688.76 MiB | none |
| prioritized_arbiter-n7 | REALIZABLE | 0 | 0.844 s | 681.84 MiB | none |

## Cross-target phase dominance

| Target | Dominant phase | Second | Third |
|---|---|---|---|
| arbiter_with_cancel-n8 | target_check — 2.460 s (51.4%) | candidate_builder — 2.081 s (43.5%) | export — 0.484 s (10.1%) |
| arbiter_with_cancel-n9 | target_check — 6.299 s (69.5%) | candidate_builder — 2.541 s (28.0%) | export — 0.715 s (7.9%) |
| arbiter_with_cancel-n10 | target_check — 21.362 s (86.7%) | candidate_builder — 3.043 s (12.4%) | export — 0.951 s (3.9%) |
| load_balancer_unreal2-n6 | target_solve — 1.458 s (64.5%) | target_check — 0.499 s (22.1%) | target_monitor_construction — 0.087 s (3.9%) |
| load_balancer_unreal2-n7 | target_solve — 9.566 s (85.1%) | target_check — 1.339 s (11.9%) | target_monitor_construction — 0.092 s (0.8%) |
| load_balancer-n8 | candidate_builder — 4.182 s (91.3%) | substitute_variables — 0.963 s (21.0%) | export — 0.777 s (17.0%) |
| load_balancer-n9 | candidate_builder — 9.698 s (95.1%) | substitute_variables — 3.488 s (34.2%) | policy_construction_skolemization — 1.895 s (18.6%) |
| amba_decomposed_lock-n15 | candidate_builder — 7.227 s (49.9%) | target_check — 7.026 s (48.5%) | substitute_variables — 4.130 s (28.5%) |
| arbiter_with_buffer-n8 | candidate_builder — 9.435 s (96.3%) | policy_construction_skolemization — 7.047 s (71.9%) | export — 1.323 s (13.5%) |
| arbiter_with_buffer-n9 | candidate_builder — 96.577 s (99.5%) | policy_construction_skolemization — 88.799 s (91.4%) | export — 4.933 s (5.1%) |
| arbiter_on_inpchange-n6 | target_check — 12.216 s (74.3%) | candidate_builder — 4.007 s (24.4%) | bdd_projection_relabel — 1.455 s (8.8%) |
| arbiter_on_inpchange-n7 | target_check — ≥ 115.031 s (≥ 95.7%) | candidate_builder — 4.924 s (4.1%) | bdd_projection_relabel — 1.813 s (1.5%) |
| round_robin_arbiter_unreal2-n5 | target_check — 0.912 s (44.8%) | target_solve — 0.831 s (40.8%) | target_monitor_construction — 0.079 s (3.9%) |
| round_robin_arbiter_unreal2-n6 | target_check — 3.436 s (47.9%) | target_solve — 3.433 s (47.8%) | target_monitor_construction — 0.089 s (1.2%) |
| round_robin_arbiter_unreal2-n7 | target_check — 15.774 s (51.0%) | target_solve — 14.846 s (48.0%) | target_monitor_construction — 0.093 s (0.3%) |
| collector_v1-n11 | candidate_builder — 119.825 s (99.8%) | canonicalize — ≥ 119.613 s (≥ 99.6%) | n/a |
| arbiter-n6 | candidate_builder — 0.901 s (71.6%) | export — 0.169 s (13.5%) | target_check — 0.141 s (11.2%) |
| prioritized_arbiter-n7 | candidate_builder — 0.540 s (63.9%) | seed_monitor_construction — 0.130 s (15.4%) | target_check — 0.090 s (10.7%) |

## Per-target detail

### arbiter_with_cancel-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 4.783 s
- cgroup `memory.peak`: 935.81 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-8` (node cap 33554432): `{"aig_gates_visited":70321,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":7667713,"policy_counter_constants":2450,"policy_cross_mode_root_reuses":8000,"policy_dependent_gates_per_mode":985,"policy_full_cache_modes":0,"policy_independent_gates":17821,"policy_independent_roots":160,"policy_mode_builds":50,"policy_specialized_gates":49250,"policy_unspecialized_gates":0,"proof_seconds":2.35442602,"requested_roots":3468,"setup_seconds":0.079840495,"successor_applications":282,"successor_substitutions":50}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 50: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 2.459677 s | 51.4% |
| candidate_builder | 1 | 2.080758 s | 43.5% |
| export | 6195 | 0.483765 s | 10.1% |
| instantiate_templates | 115 | 0.376642 s | 7.9% |
| seed_solves | 3 | 0.294824 s | 6.2% |
| policy_construction_skolemization | 1 | 0.259061 s | 5.4% |
| bdd_projection_relabel | 6555 | 0.249506 s | 5.2% |
| bdd_relabel_rename | 5520 | 0.240312 s | 5.0% |
| seed_monitor_construction | 3 | 0.220687 s | 4.6% |
| support_extraction | 7718 | 0.160283 s | 3.4% |
| target_monitor_construction | 1 | 0.088553 s | 1.9% |
| projection_metadata | 2645 | 0.058510 s | 1.2% |
| from_aag | 4 | 0.004083 s | 0.1% |
| bdd_existential_quantification | 1035 | 0.002705 s | 0.1% |
| variable_cube_construction | 9 | 0.000216 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_cancel-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 9.061 s
- cgroup `memory.peak`: 1.21 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-9` (node cap 33554432): `{"aig_gates_visited":95432,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":17825793,"policy_counter_constants":3080,"policy_cross_mode_root_reuses":11088,"policy_dependent_gates_per_mode":1216,"policy_full_cache_modes":0,"policy_independent_gates":23310,"policy_independent_roots":198,"policy_mode_builds":56,"policy_specialized_gates":68096,"policy_unspecialized_gates":0,"proof_seconds":6.171767732,"requested_roots":4296,"setup_seconds":0.082759476,"successor_applications":316,"successor_substitutions":56}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 56: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 6.298856 s | 69.5% |
| candidate_builder | 1 | 2.541145 s | 28.0% |
| export | 7687 | 0.714733 s | 7.9% |
| instantiate_templates | 129 | 0.556707 s | 6.1% |
| policy_construction_skolemization | 1 | 0.385371 s | 4.3% |
| bdd_projection_relabel | 8385 | 0.341897 s | 3.8% |
| bdd_relabel_rename | 7224 | 0.330080 s | 3.6% |
| seed_solves | 3 | 0.291168 s | 3.2% |
| seed_monitor_construction | 3 | 0.215696 s | 2.4% |
| support_extraction | 9688 | 0.191666 s | 2.1% |
| target_monitor_construction | 1 | 0.091816 s | 1.0% |
| projection_metadata | 2967 | 0.065329 s | 0.7% |
| from_aag | 4 | 0.004127 s | 0.0% |
| bdd_existential_quantification | 1161 | 0.003104 s | 0.0% |
| variable_cube_construction | 9 | 0.000232 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_cancel-n10

- Verdict/exit: REALIZABLE / 0
- Total wall: 24.629 s
- cgroup `memory.peak`: 1.67 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-10` (node cap 33554432): `{"aig_gates_visited":125759,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":31195137,"policy_counter_constants":3782,"policy_cross_mode_root_reuses":14880,"policy_dependent_gates_per_mode":1471,"policy_full_cache_modes":0,"policy_independent_gates":29678,"policy_independent_roots":240,"policy_mode_builds":62,"policy_specialized_gates":91202,"policy_unspecialized_gates":0,"proof_seconds":21.222199762,"requested_roots":5212,"setup_seconds":0.083459281,"successor_applications":350,"successor_substitutions":62}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 62: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 21.362117 s | 86.7% |
| candidate_builder | 1 | 3.042767 s | 12.4% |
| export | 9339 | 0.950525 s | 3.9% |
| instantiate_templates | 143 | 0.759361 s | 3.1% |
| policy_construction_skolemization | 1 | 0.517265 s | 2.1% |
| bdd_projection_relabel | 10582 | 0.436626 s | 1.8% |
| bdd_relabel_rename | 9295 | 0.422396 s | 1.7% |
| seed_solves | 3 | 0.294853 s | 1.2% |
| support_extraction | 12025 | 0.234610 s | 1.0% |
| seed_monitor_construction | 3 | 0.213447 s | 0.9% |
| target_monitor_construction | 1 | 0.097061 s | 0.4% |
| projection_metadata | 3289 | 0.070370 s | 0.3% |
| from_aag | 4 | 0.004380 s | 0.0% |
| bdd_existential_quantification | 1287 | 0.003542 s | 0.0% |
| variable_cube_construction | 9 | 0.000222 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### load_balancer_unreal2-n6

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 2.261 s
- cgroup `memory.peak`: 1.32 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 67108864): `{"aig_gates_visited":7831,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":786433,"policy_counter_constants":12,"policy_cross_mode_root_reuses":8,"policy_dependent_gates_per_mode":41,"policy_full_cache_modes":0,"policy_independent_gates":20,"policy_independent_roots":2,"policy_mode_builds":4,"policy_specialized_gates":164,"policy_unspecialized_gates":0,"proof_seconds":0.193631993,"requested_roots":1189,"setup_seconds":0.243769483,"successor_applications":2176,"successor_substitutions":4}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 12, 1: 46, 2: 70
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 4: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 1.457797 s | 64.5% |
| target_check | 1 | 0.498786 s | 22.1% |
| target_monitor_construction | 1 | 0.087231 s | 3.9% |
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
- Total wall: 11.238 s
- cgroup `memory.peak`: 1.59 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 67108864): `{"aig_gates_visited":15334,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":3670017,"policy_counter_constants":12,"policy_cross_mode_root_reuses":8,"policy_dependent_gates_per_mode":41,"policy_full_cache_modes":0,"policy_independent_gates":20,"policy_independent_roots":2,"policy_mode_builds":4,"policy_specialized_gates":164,"policy_unspecialized_gates":0,"proof_seconds":0.999671688,"requested_roots":1494,"setup_seconds":0.18987065,"successor_applications":2752,"successor_substitutions":4}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 12, 1: 53, 2: 96
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 4: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 9.566052 s | 85.1% |
| target_check | 1 | 1.338897 s | 11.9% |
| target_monitor_construction | 1 | 0.092434 s | 0.8% |
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
- Total wall: 4.581 s
- cgroup `memory.peak`: 704.20 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-8` (node cap 33554432): `{"aig_gates_visited":78581,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":131073,"policy_counter_constants":650,"policy_cross_mode_root_reuses":11102,"policy_dependent_gates_per_mode":1887,"policy_full_cache_modes":0,"policy_independent_gates":27238,"policy_independent_roots":427,"policy_mode_builds":26,"policy_specialized_gates":49062,"policy_unspecialized_gates":0,"proof_seconds":0.056512066,"requested_roots":1641,"setup_seconds":0.087090123,"successor_applications":344,"successor_substitutions":26}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 4.181925 s | 91.3% |
| substitute_variables | 175 | 0.962774 s | 21.0% |
| export | 316 | 0.777288 s | 17.0% |
| policy_construction_skolemization | 1 | 0.693776 s | 15.1% |
| instantiate_templates | 175 | 0.361035 s | 7.9% |
| bdd_projection_relabel | 10527 | 0.238822 s | 5.2% |
| bdd_relabel_rename | 8952 | 0.225321 s | 4.9% |
| seed_monitor_construction | 3 | 0.202141 s | 4.4% |
| target_check | 1 | 0.172519 s | 3.8% |
| support_extraction | 12901 | 0.127676 s | 2.8% |
| seed_solves | 3 | 0.085382 s | 1.9% |
| projection_metadata | 4025 | 0.083636 s | 1.8% |
| target_monitor_construction | 1 | 0.074006 s | 1.6% |
| from_aag | 4 | 0.005287 s | 0.1% |
| bdd_existential_quantification | 1575 | 0.003401 s | 0.1% |
| variable_cube_construction | 14 | 0.000190 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### load_balancer-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 10.195 s
- cgroup `memory.peak`: 736.53 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-9` (node cap 33554432): `{"aig_gates_visited":154106,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":196609,"policy_counter_constants":812,"policy_cross_mode_root_reuses":23664,"policy_dependent_gates_per_mode":3198,"policy_full_cache_modes":0,"policy_independent_gates":58760,"policy_independent_roots":816,"policy_mode_builds":29,"policy_specialized_gates":92742,"policy_unspecialized_gates":0,"proof_seconds":0.094623312,"requested_roots":2286,"setup_seconds":0.097261665,"successor_applications":384,"successor_substitutions":29}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 29: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 9.697935 s | 95.1% |
| substitute_variables | 196 | 3.487898 s | 34.2% |
| policy_construction_skolemization | 1 | 1.895286 s | 18.6% |
| export | 354 | 1.866430 s | 18.3% |
| instantiate_templates | 196 | 0.508361 s | 5.0% |
| bdd_projection_relabel | 13357 | 0.474928 s | 4.7% |
| bdd_relabel_rename | 11593 | 0.458407 s | 4.5% |
| target_check | 1 | 0.257573 s | 2.5% |
| seed_monitor_construction | 3 | 0.202147 s | 2.0% |
| support_extraction | 16006 | 0.164274 s | 1.6% |
| projection_metadata | 4508 | 0.092102 s | 0.9% |
| seed_solves | 3 | 0.084151 s | 0.8% |
| target_monitor_construction | 1 | 0.074999 s | 0.7% |
| from_aag | 4 | 0.005598 s | 0.1% |
| bdd_existential_quantification | 1764 | 0.003783 s | 0.0% |
| variable_cube_construction | 14 | 0.000925 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### amba_decomposed_lock-n15

- Verdict/exit: REALIZABLE / 0
- Total wall: 14.475 s
- cgroup `memory.peak`: 1.84 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-15` (node cap 67108864): `{"aig_gates_visited":397545,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":17170433,"policy_counter_constants":272,"policy_cross_mode_root_reuses":833,"policy_dependent_gates_per_mode":171,"policy_full_cache_modes":0,"policy_independent_gates":393593,"policy_independent_roots":49,"policy_mode_builds":17,"policy_specialized_gates":2907,"policy_unspecialized_gates":0,"proof_seconds":6.777814299,"requested_roots":523,"setup_seconds":0.168294675,"successor_applications":153,"successor_substitutions":17}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 17: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 7.227099 s | 49.9% |
| target_check | 1 | 7.026162 s | 48.5% |
| substitute_variables | 65 | 4.130238 s | 28.5% |
| export | 146 | 1.492768 s | 10.3% |
| policy_construction_skolemization | 1 | 1.072087 s | 7.4% |
| bdd_projection_relabel | 2993 | 0.429064 s | 3.0% |
| bdd_relabel_rename | 2408 | 0.425352 s | 2.9% |
| seed_monitor_construction | 3 | 0.197721 s | 1.4% |
| target_monitor_construction | 1 | 0.104383 s | 0.7% |
| seed_solves | 3 | 0.066152 s | 0.5% |
| instantiate_templates | 65 | 0.054251 s | 0.4% |
| support_extraction | 3862 | 0.036022 s | 0.2% |
| projection_metadata | 1365 | 0.020183 s | 0.1% |
| from_aag | 4 | 0.002169 s | 0.0% |
| bdd_existential_quantification | 585 | 0.000767 s | 0.0% |
| variable_cube_construction | 10 | 0.000067 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_buffer-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 9.797 s
- cgroup `memory.peak`: 715.43 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-8` (node cap 33554432): `{"aig_gates_visited":13346,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":1,"policy_counter_constants":90,"policy_cross_mode_root_reuses":2720,"policy_dependent_gates_per_mode":883,"policy_full_cache_modes":0,"policy_independent_gates":4124,"policy_independent_roots":272,"policy_mode_builds":10,"policy_specialized_gates":8830,"policy_unspecialized_gates":0,"proof_seconds":0.005740211,"requested_roots":612,"setup_seconds":0.090089274,"successor_applications":50,"successor_substitutions":10}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 10: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 9.435412 s | 96.3% |
| policy_construction_skolemization | 1 | 7.046762 s | 71.9% |
| export | 80 | 1.322736 s | 13.5% |
| seed_monitor_construction | 3 | 0.199007 s | 2.0% |
| target_check | 1 | 0.136674 s | 1.4% |
| substitute_variables | 19 | 0.134920 s | 1.4% |
| bdd_projection_relabel | 782 | 0.112662 s | 1.1% |
| bdd_relabel_rename | 611 | 0.111762 s | 1.1% |
| target_monitor_construction | 1 | 0.071121 s | 0.7% |
| seed_solves | 3 | 0.066303 s | 0.7% |
| support_extraction | 1073 | 0.018862 s | 0.2% |
| instantiate_templates | 19 | 0.006922 s | 0.1% |
| projection_metadata | 399 | 0.005249 s | 0.1% |
| from_aag | 4 | 0.001177 s | 0.0% |
| bdd_existential_quantification | 171 | 0.000193 s | 0.0% |
| variable_cube_construction | 10 | 0.000121 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_buffer-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 97.102 s
- cgroup `memory.peak`: 1.73 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-9` (node cap 33554432): `{"aig_gates_visited":26959,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":1,"policy_counter_constants":110,"policy_cross_mode_root_reuses":5830,"policy_dependent_gates_per_mode":1669,"policy_full_cache_modes":0,"policy_independent_gates":8145,"policy_independent_roots":530,"policy_mode_builds":11,"policy_specialized_gates":18359,"policy_unspecialized_gates":0,"proof_seconds":0.009755301,"requested_roots":938,"setup_seconds":0.125593134,"successor_applications":55,"successor_substitutions":11}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 11: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 96.577475 s | 99.5% |
| policy_construction_skolemization | 1 | 88.799142 s | 91.4% |
| export | 89 | 4.932859 s | 5.1% |
| substitute_variables | 21 | 0.862765 s | 0.9% |
| bdd_projection_relabel | 886 | 0.644804 s | 0.7% |
| bdd_relabel_rename | 697 | 0.643697 s | 0.7% |
| target_check | 1 | 0.277585 s | 0.3% |
| seed_monitor_construction | 3 | 0.195107 s | 0.2% |
| target_monitor_construction | 1 | 0.073588 s | 0.1% |
| seed_solves | 3 | 0.066269 s | 0.1% |
| support_extraction | 1208 | 0.064137 s | 0.1% |
| instantiate_templates | 21 | 0.008559 s | 0.0% |
| projection_metadata | 441 | 0.005650 s | 0.0% |
| from_aag | 4 | 0.001436 s | 0.0% |
| bdd_existential_quantification | 189 | 0.000219 s | 0.0% |
| variable_cube_construction | 10 | 0.000136 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_on_inpchange-n6

- Verdict/exit: REALIZABLE / 0
- Total wall: 16.447 s
- cgroup `memory.peak`: 1.38 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 33554432): `{"aig_gates_visited":59768,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":22675457,"policy_counter_constants":650,"policy_cross_mode_root_reuses":2496,"policy_dependent_gates_per_mode":403,"policy_full_cache_modes":0,"policy_independent_gates":37435,"policy_independent_roots":96,"policy_mode_builds":26,"policy_specialized_gates":10478,"policy_unspecialized_gates":0,"proof_seconds":12.077115268,"requested_roots":1206,"setup_seconds":0.085074139,"successor_applications":154,"successor_substitutions":26}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 12.215738 s | 74.3% |
| candidate_builder | 1 | 4.007194 s | 24.4% |
| bdd_projection_relabel | 2772 | 1.454519 s | 8.8% |
| bdd_relabel_rename | 2205 | 1.445928 s | 8.8% |
| export | 2611 | 0.982655 s | 6.0% |
| seed_solves | 3 | 0.592460 s | 3.6% |
| policy_construction_skolemization | 1 | 0.513738 s | 3.1% |
| instantiate_templates | 63 | 0.391822 s | 2.4% |
| seed_monitor_construction | 3 | 0.223394 s | 1.4% |
| support_extraction | 3417 | 0.108850 s | 0.7% |
| target_monitor_construction | 1 | 0.089922 s | 0.5% |
| projection_metadata | 1449 | 0.049396 s | 0.3% |
| from_aag | 4 | 0.022788 s | 0.1% |
| bdd_existential_quantification | 567 | 0.005520 s | 0.0% |
| variable_cube_construction | 9 | 0.000403 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_on_inpchange-n7

- Verdict/exit: UNKNOWN / 2
- Total wall: 120.157 s
- cgroup `memory.peak`: 1.76 GiB
- Censored stages: target_check ≥ 115.031 s (absolute_deadline_exhausted, sigterm)
- Checker `--stats`: no payload captured
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 30: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 4.924125 s | 4.1% |
| bdd_projection_relabel | 3650 | 1.812512 s | 1.5% |
| bdd_relabel_rename | 2993 | 1.801818 s | 1.5% |
| export | 3435 | 1.459459 s | 1.2% |
| policy_construction_skolemization | 1 | 0.755783 s | 0.6% |
| instantiate_templates | 73 | 0.613457 s | 0.5% |
| seed_solves | 3 | 0.586149 s | 0.5% |
| seed_monitor_construction | 3 | 0.222110 s | 0.2% |
| support_extraction | 4395 | 0.129469 s | 0.1% |
| target_monitor_construction | 1 | 0.092042 s | 0.1% |
| projection_metadata | 1679 | 0.053785 s | 0.0% |
| from_aag | 4 | 0.021973 s | 0.0% |
| bdd_existential_quantification | 657 | 0.006672 s | 0.0% |
| variable_cube_construction | 9 | 0.000411 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_check | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### round_robin_arbiter_unreal2-n5

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 2.036 s
- cgroup `memory.peak`: 1.35 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-5` (node cap 67108864): `{"aig_gates_visited":141862,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":196609,"policy_counter_constants":240,"policy_cross_mode_root_reuses":270,"policy_dependent_gates_per_mode":132039,"policy_full_cache_modes":16,"policy_independent_gates":326,"policy_independent_roots":270,"policy_mode_builds":16,"policy_specialized_gates":0,"policy_unspecialized_gates":132039,"proof_seconds":0.473704703,"requested_roots":2754,"setup_seconds":0.182469151,"successor_applications":4736,"successor_substitutions":16}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 60, 2: 40
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 16: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 0.911534 s | 44.8% |
| target_solve | 1 | 0.831269 s | 40.8% |
| target_monitor_construction | 1 | 0.079119 s | 3.9% |
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
- Total wall: 7.177 s
- cgroup `memory.peak`: 1.55 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 67108864): `{"aig_gates_visited":734342,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":1310721,"policy_counter_constants":342,"policy_cross_mode_root_reuses":1043,"policy_dependent_gates_per_mode":714747,"policy_full_cache_modes":19,"policy_independent_gates":1112,"policy_independent_roots":1043,"policy_mode_builds":19,"policy_specialized_gates":0,"policy_unspecialized_gates":714747,"proof_seconds":2.513031794,"requested_roots":5059,"setup_seconds":0.19497546,"successor_applications":7744,"successor_substitutions":19}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 72, 2: 60
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 19: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 3.435606 s | 47.9% |
| target_solve | 1 | 3.432630 s | 47.8% |
| target_monitor_construction | 1 | 0.088561 s | 1.2% |
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
- Total wall: 30.932 s
- cgroup `memory.peak`: 2.49 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 67108864): `{"aig_gates_visited":3646651,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":6553601,"policy_counter_constants":462,"policy_cross_mode_root_reuses":4120,"policy_dependent_gates_per_mode":3609146,"policy_full_cache_modes":22,"policy_independent_gates":4202,"policy_independent_roots":4120,"policy_mode_builds":22,"policy_specialized_gates":0,"policy_unspecialized_gates":3609146,"proof_seconds":13.698223898,"requested_roots":10210,"setup_seconds":0.240657697,"successor_applications":11832,"successor_substitutions":22}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 84, 2: 84
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 22: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 15.774127 s | 51.0% |
| target_solve | 1 | 14.846428 s | 48.0% |
| target_monitor_construction | 1 | 0.093300 s | 0.3% |
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
- Total wall: 120.035 s
- cgroup `memory.peak`: 2.13 GiB
- Censored stages: canonicalize ≥ 119.613 s (absolute_deadline_exhausted)
- Checker `--stats`: no payload captured
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: none

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 119.825114 s | 99.8% |
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
- Total wall: 1.258 s
- cgroup `memory.peak`: 688.76 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 33554432): `{"aig_gates_visited":19687,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":131073,"policy_counter_constants":650,"policy_cross_mode_root_reuses":2340,"policy_dependent_gates_per_mode":403,"policy_full_cache_modes":0,"policy_independent_gates":7632,"policy_independent_roots":90,"policy_mode_builds":26,"policy_specialized_gates":10478,"policy_unspecialized_gates":0,"proof_seconds":0.054221911,"requested_roots":1140,"setup_seconds":0.081352236,"successor_applications":154,"successor_substitutions":26}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 0.901040 s | 71.6% |
| export | 2611 | 0.169389 s | 13.5% |
| target_check | 1 | 0.141177 s | 11.2% |
| seed_monitor_construction | 2 | 0.135984 s | 10.8% |
| policy_construction_skolemization | 1 | 0.092358 s | 7.3% |
| seed_solves | 2 | 0.091912 s | 7.3% |
| instantiate_templates | 63 | 0.074674 s | 5.9% |
| target_monitor_construction | 1 | 0.071748 s | 5.7% |
| support_extraction | 3223 | 0.050394 s | 4.0% |
| bdd_projection_relabel | 2646 | 0.044264 s | 3.5% |
| bdd_relabel_rename | 2079 | 0.040403 s | 3.2% |
| projection_metadata | 1260 | 0.027352 s | 2.2% |
| from_aag | 3 | 0.002753 s | 0.2% |
| bdd_existential_quantification | 567 | 0.001322 s | 0.1% |
| variable_cube_construction | 9 | 0.000164 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### prioritized_arbiter-n7

- Verdict/exit: REALIZABLE / 0
- Total wall: 0.844 s
- cgroup `memory.peak`: 681.84 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 33554432): `{"aig_gates_visited":19751,"cache_cap":33554432,"final_status":"VERIFIED","node_cap":33554432,"peak_live_nodes_sample":1,"policy_counter_constants":272,"policy_cross_mode_root_reuses":2499,"policy_dependent_gates_per_mode":982,"policy_full_cache_modes":0,"policy_independent_gates":2776,"policy_independent_roots":147,"policy_mode_builds":17,"policy_specialized_gates":16694,"policy_unspecialized_gates":0,"proof_seconds":0.005949558,"requested_roots":704,"setup_seconds":0.07920656,"successor_applications":116,"successor_substitutions":17}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 17: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 0.539899 s | 63.9% |
| seed_monitor_construction | 2 | 0.129716 s | 15.4% |
| target_check | 1 | 0.089996 s | 10.7% |
| target_monitor_construction | 1 | 0.069887 s | 8.3% |
| seed_solves | 2 | 0.044491 s | 5.3% |
| policy_construction_skolemization | 1 | 0.019188 s | 2.3% |
| support_extraction | 1810 | 0.015731 s | 1.9% |
| instantiate_templates | 40 | 0.014546 s | 1.7% |
| bdd_projection_relabel | 1323 | 0.014447 s | 1.7% |
| bdd_relabel_rename | 1043 | 0.012917 s | 1.5% |
| projection_metadata | 640 | 0.009003 s | 1.1% |
| export | 135 | 0.008065 s | 1.0% |
| substitute_variables | 40 | 0.003292 s | 0.4% |
| from_aag | 3 | 0.000863 s | 0.1% |
| bdd_existential_quantification | 280 | 0.000362 s | 0.0% |
| variable_cube_construction | 8 | 0.000104 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |
