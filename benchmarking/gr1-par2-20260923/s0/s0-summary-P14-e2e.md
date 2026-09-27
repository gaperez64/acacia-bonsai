# S0 diagnostic summary

Source: `/home/gperez/GIT-repos/acacia-gr1-par2-timing/benchmarking/gr1-par2-20260923/s0/raw-p14`

## Notes

- End-to-end with P1.4 tools (tlsf-tools dbe8c2b build-P14-dbe8c2b), Acacia generalizer 3ac18759 (L0 generalizer), timing worktree, serial, 120 s, 8 GiB

## Targets

| Target | Verdict | Exit | Total wall | cgroup memory.peak | Censored stage |
|---|---:|---:|---:|---:|---|
| arbiter_with_cancel-n8 | REALIZABLE | 0 | 6.664 s | 1.19 GiB | none |
| arbiter_with_cancel-n9 | REALIZABLE | 0 | 11.101 s | 1.48 GiB | none |
| arbiter_with_cancel-n10 | REALIZABLE | 0 | 26.482 s | 1.94 GiB | none |
| load_balancer_unreal2-n6 | UNREALIZABLE | 1 | 2.142 s | 1.32 GiB | none |
| load_balancer_unreal2-n7 | UNREALIZABLE | 1 | 11.242 s | 1.58 GiB | none |
| load_balancer-n8 | REALIZABLE | 0 | 18.210 s | 1.01 GiB | none |
| load_balancer-n9 | REALIZABLE | 0 | 56.511 s | 1.05 GiB | none |
| amba_decomposed_lock-n15 | REALIZABLE | 0 | 41.660 s | 2.25 GiB | none |
| arbiter_with_buffer-n8 | REALIZABLE | 0 | 15.360 s | 1.05 GiB | none |
| arbiter_with_buffer-n9 | UNKNOWN | 2 | 120.598 s | 1.77 GiB | instantiate ≥ 0.000 s |
| arbiter_on_inpchange-n6 | REALIZABLE | 0 | 24.186 s | 1.69 GiB | none |
| arbiter_on_inpchange-n7 | UNKNOWN | 2 | 120.174 s | 2.07 GiB | target_check ≥ 106.879 s |
| round_robin_arbiter_unreal2-n5 | UNREALIZABLE | 1 | 1.969 s | 1.34 GiB | none |
| round_robin_arbiter_unreal2-n6 | UNREALIZABLE | 1 | 7.162 s | 1.54 GiB | none |
| round_robin_arbiter_unreal2-n7 | UNREALIZABLE | 1 | 31.202 s | 2.48 GiB | none |
| collector_v1-n11 | UNKNOWN | 2 | 119.921 s | 2.10 GiB | canonicalize ≥ 119.620 s; target_monitor_construction ≥ 119.619 s |
| arbiter-n6 | REALIZABLE | 0 | 2.146 s | 951.44 MiB | none |
| prioritized_arbiter-n7 | REALIZABLE | 0 | 1.024 s | 930.61 MiB | none |

## Cross-target phase dominance

| Target | Dominant phase | Second | Third |
|---|---|---|---|
| arbiter_with_cancel-n8 | target_check — 2.395 s (35.9%) | bdd_projection_relabel — 0.894 s (13.4%) | instantiate_templates — 0.759 s (11.4%) |
| arbiter_with_cancel-n9 | target_check — 6.082 s (54.8%) | bdd_projection_relabel — 1.193 s (10.8%) | instantiate_templates — 1.120 s (10.1%) |
| arbiter_with_cancel-n10 | target_check — 20.777 s (78.5%) | instantiate_templates — 1.474 s (5.6%) | bdd_projection_relabel — 1.401 s (5.3%) |
| load_balancer_unreal2-n6 | target_solve — 1.452 s (67.8%) | target_check — 0.417 s (19.5%) | target_monitor_construction — 0.084 s (3.9%) |
| load_balancer_unreal2-n7 | target_solve — 9.633 s (85.7%) | target_check — 1.326 s (11.8%) | target_monitor_construction — 0.090 s (0.8%) |
| load_balancer-n8 | substitute_variables — 8.765 s (48.1%) | bdd_projection_relabel — 5.811 s (31.9%) | policy_construction_skolemization — 3.221 s (17.7%) |
| load_balancer-n9 | substitute_variables — 34.803 s (61.6%) | bdd_projection_relabel — 14.825 s (26.2%) | policy_construction_skolemization — 9.130 s (16.2%) |
| amba_decomposed_lock-n15 | bdd_projection_relabel — 23.176 s (55.6%) | policy_construction_skolemization — 12.577 s (30.2%) | substitute_variables — 8.109 s (19.5%) |
| arbiter_with_buffer-n8 | policy_construction_skolemization — 9.595 s (62.5%) | bdd_projection_relabel — 5.398 s (35.1%) | export — 1.406 s (9.2%) |
| arbiter_with_buffer-n9 | generalize_gr1 — 120.577 s (100.0%) | instantiate — ≥ 0.000 s (≥ 0.0%) | n/a |
| arbiter_on_inpchange-n6 | target_check — 12.041 s (49.8%) | from_aag — 2.945 s (12.2%) | bdd_projection_relabel — 1.994 s (8.2%) |
| arbiter_on_inpchange-n7 | target_check — ≥ 106.879 s (≥ 88.9%) | from_aag — 3.121 s (2.6%) | bdd_projection_relabel — 2.419 s (2.0%) |
| round_robin_arbiter_unreal2-n5 | target_check — 0.904 s (45.9%) | target_solve — 0.809 s (41.1%) | target_monitor_construction — 0.079 s (4.0%) |
| round_robin_arbiter_unreal2-n6 | target_solve — 3.468 s (48.4%) | target_check — 3.429 s (47.9%) | target_monitor_construction — 0.086 s (1.2%) |
| round_robin_arbiter_unreal2-n7 | target_check — 15.828 s (50.7%) | target_solve — 15.099 s (48.4%) | target_monitor_construction — 0.094 s (0.3%) |
| collector_v1-n11 | target_monitor_construction — ≥ 239.239 s (≥ 199.5%) | canonicalize — ≥ 119.620 s (≥ 99.7%) | seed_monitor_construction — 0.074 s (0.1%) |
| arbiter-n6 | bdd_projection_relabel — 0.342 s (15.9%) | seed_monitor_construction — 0.272 s (12.7%) | export — 0.268 s (12.5%) |
| prioritized_arbiter-n7 | seed_monitor_construction — 0.259 s (25.3%) | target_monitor_construction — 0.136 s (13.3%) | seed_solves — 0.090 s (8.8%) |

## Per-target detail

### arbiter_with_cancel-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 6.664 s
- cgroup `memory.peak`: 1.19 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":21484,"final_status":"VERIFIED","peak_live_nodes_sample":524289,"policy_counter_constants":992,"policy_cross_mode_root_reuses":2240,"policy_dependent_gates_per_mode":436,"policy_full_cache_modes":0,"policy_independent_gates":6148,"policy_independent_roots":70,"policy_mode_builds":32,"policy_specialized_gates":13952,"policy_unspecialized_gates":0,"proof_seconds":0.136098075,"requested_roots":1512,"setup_seconds":0.039411323,"successor_applications":180,"successor_substitutions":32}`
  - `target-8` (node cap 33554432): `{"aig_gates_visited":70321,"final_status":"VERIFIED","peak_live_nodes_sample":7667713,"policy_counter_constants":2450,"policy_cross_mode_root_reuses":8000,"policy_dependent_gates_per_mode":985,"policy_full_cache_modes":0,"policy_independent_gates":17821,"policy_independent_roots":160,"policy_mode_builds":50,"policy_specialized_gates":49250,"policy_unspecialized_gates":0,"proof_seconds":2.291514708,"requested_roots":3468,"setup_seconds":0.080284979,"successor_applications":282,"successor_substitutions":50}`
- Support width min/median/max: 25 / 25 / 25
- Owner-tuple arity histogram: 0: 16, 1: 589
- Subset reuse: 48 distinct; 48 reused; uses min/median/max 73 / 115 / 188; operations instantiation: 3950, projection: 1880
- Mask word-length histogram: 1: 1316, 2: 564
- Actual checker mode counts: 32: 1, 50: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 2.395491 s | 35.9% |
| bdd_projection_relabel | 9402 | 0.894284 s | 13.4% |
| instantiate_templates | 188 | 0.758686 s | 11.4% |
| export | 8874 | 0.587081 s | 8.8% |
| seed_solves | 6 | 0.579167 s | 8.7% |
| from_aag | 564 | 0.510853 s | 7.7% |
| seed_monitor_construction | 6 | 0.430156 s | 6.5% |
| policy_construction_skolemization | 2 | 0.321724 s | 4.8% |
| target_monitor_construction | 2 | 0.166342 s | 2.5% |
| projection_metadata | 4324 | 0.126877 s | 1.9% |
| support_extraction | 5830 | 0.107954 s | 1.6% |
| variable_cube_construction | 1692 | 0.035420 s | 0.5% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_cancel-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 11.101 s
- cgroup `memory.peak`: 1.48 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":21484,"final_status":"VERIFIED","peak_live_nodes_sample":524289,"policy_counter_constants":992,"policy_cross_mode_root_reuses":2240,"policy_dependent_gates_per_mode":436,"policy_full_cache_modes":0,"policy_independent_gates":6148,"policy_independent_roots":70,"policy_mode_builds":32,"policy_specialized_gates":13952,"policy_unspecialized_gates":0,"proof_seconds":0.137377472,"requested_roots":1512,"setup_seconds":0.039959864,"successor_applications":180,"successor_substitutions":32}`
  - `target-9` (node cap 33554432): `{"aig_gates_visited":95432,"final_status":"VERIFIED","peak_live_nodes_sample":17825793,"policy_counter_constants":3080,"policy_cross_mode_root_reuses":11088,"policy_dependent_gates_per_mode":1216,"policy_full_cache_modes":0,"policy_independent_gates":23310,"policy_independent_roots":198,"policy_mode_builds":56,"policy_specialized_gates":68096,"policy_unspecialized_gates":0,"proof_seconds":5.958049419,"requested_roots":4296,"setup_seconds":0.084463941,"successor_applications":316,"successor_substitutions":56}`
- Support width min/median/max: 25 / 25 / 25
- Owner-tuple arity histogram: 0: 16, 1: 608
- Subset reuse: 56 distinct; 56 reused; uses min/median/max 73 / 129 / 202; operations instantiation: 5374, projection: 2020
- Mask word-length histogram: 1: 1414, 2: 606
- Actual checker mode counts: 32: 1, 56: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 6.082316 s | 54.8% |
| bdd_projection_relabel | 11232 | 1.193366 s | 10.8% |
| instantiate_templates | 202 | 1.120214 s | 10.1% |
| export | 10366 | 0.799248 s | 7.2% |
| seed_solves | 6 | 0.581230 s | 5.2% |
| from_aag | 606 | 0.565782 s | 5.1% |
| policy_construction_skolemization | 2 | 0.439176 s | 4.0% |
| seed_monitor_construction | 6 | 0.427846 s | 3.9% |
| target_monitor_construction | 2 | 0.168946 s | 1.5% |
| support_extraction | 7394 | 0.147466 s | 1.3% |
| projection_metadata | 4646 | 0.140472 s | 1.3% |
| variable_cube_construction | 1818 | 0.043557 s | 0.4% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_cancel-n10

- Verdict/exit: REALIZABLE / 0
- Total wall: 26.482 s
- cgroup `memory.peak`: 1.94 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":21484,"final_status":"VERIFIED","peak_live_nodes_sample":524289,"policy_counter_constants":992,"policy_cross_mode_root_reuses":2240,"policy_dependent_gates_per_mode":436,"policy_full_cache_modes":0,"policy_independent_gates":6148,"policy_independent_roots":70,"policy_mode_builds":32,"policy_specialized_gates":13952,"policy_unspecialized_gates":0,"proof_seconds":0.135877262,"requested_roots":1512,"setup_seconds":0.040269325,"successor_applications":180,"successor_substitutions":32}`
  - `target-10` (node cap 33554432): `{"aig_gates_visited":125759,"final_status":"VERIFIED","peak_live_nodes_sample":31195137,"policy_counter_constants":3782,"policy_cross_mode_root_reuses":14880,"policy_dependent_gates_per_mode":1471,"policy_full_cache_modes":0,"policy_independent_gates":29678,"policy_independent_roots":240,"policy_mode_builds":62,"policy_specialized_gates":91202,"policy_unspecialized_gates":0,"proof_seconds":20.644263438,"requested_roots":5212,"setup_seconds":0.080626884,"successor_applications":350,"successor_substitutions":62}`
- Support width min/median/max: 25 / 25 / 25
- Owner-tuple arity histogram: 0: 16, 1: 627
- Subset reuse: 65 distinct; 65 reused; uses min/median/max 73 / 143 / 216; operations instantiation: 7165, projection: 2160
- Mask word-length histogram: 1: 1512, 2: 648
- Actual checker mode counts: 32: 1, 62: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 20.777388 s | 78.5% |
| instantiate_templates | 216 | 1.473824 s | 5.6% |
| bdd_projection_relabel | 13429 | 1.401352 s | 5.3% |
| export | 12018 | 1.054074 s | 4.0% |
| seed_solves | 6 | 0.587443 s | 2.2% |
| from_aag | 648 | 0.583156 s | 2.2% |
| policy_construction_skolemization | 2 | 0.570279 s | 2.2% |
| seed_monitor_construction | 6 | 0.425980 s | 1.6% |
| support_extraction | 9325 | 0.175415 s | 0.7% |
| target_monitor_construction | 2 | 0.172540 s | 0.7% |
| projection_metadata | 4968 | 0.150322 s | 0.6% |
| variable_cube_construction | 1944 | 0.044770 s | 0.2% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### load_balancer_unreal2-n6

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 2.142 s
- cgroup `memory.peak`: 1.32 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 67108864): `{"aig_gates_visited":7831,"final_status":"VERIFIED","peak_live_nodes_sample":786433,"policy_counter_constants":12,"policy_cross_mode_root_reuses":8,"policy_dependent_gates_per_mode":41,"policy_full_cache_modes":0,"policy_independent_gates":20,"policy_independent_roots":2,"policy_mode_builds":4,"policy_specialized_gates":164,"policy_unspecialized_gates":0,"proof_seconds":0.189545665,"requested_roots":1189,"setup_seconds":0.165627633,"successor_applications":2176,"successor_substitutions":4}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 12, 1: 46, 2: 70
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 4: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 1.452322 s | 67.8% |
| target_check | 1 | 0.416779 s | 19.5% |
| target_monitor_construction | 1 | 0.084288 s | 3.9% |
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
- Total wall: 11.242 s
- cgroup `memory.peak`: 1.58 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 67108864): `{"aig_gates_visited":15334,"final_status":"VERIFIED","peak_live_nodes_sample":3670017,"policy_counter_constants":12,"policy_cross_mode_root_reuses":8,"policy_dependent_gates_per_mode":41,"policy_full_cache_modes":0,"policy_independent_gates":20,"policy_independent_roots":2,"policy_mode_builds":4,"policy_specialized_gates":164,"policy_unspecialized_gates":0,"proof_seconds":0.996134895,"requested_roots":1494,"setup_seconds":0.187052437,"successor_applications":2752,"successor_substitutions":4}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 12, 1: 53, 2: 96
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 4: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 9.632733 s | 85.7% |
| target_check | 1 | 1.325994 s | 11.8% |
| target_monitor_construction | 1 | 0.090479 s | 0.8% |
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
- Total wall: 18.210 s
- cgroup `memory.peak`: 1.01 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":12342,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":272,"policy_cross_mode_root_reuses":1292,"policy_dependent_gates_per_mode":492,"policy_full_cache_modes":0,"policy_independent_gates":2600,"policy_independent_roots":76,"policy_mode_builds":17,"policy_specialized_gates":8364,"policy_unspecialized_gates":0,"proof_seconds":0.014565154,"requested_roots":666,"setup_seconds":0.039530259,"successor_applications":224,"successor_substitutions":17}`
  - `target-8` (node cap 33554432): `{"aig_gates_visited":78581,"final_status":"VERIFIED","peak_live_nodes_sample":131073,"policy_counter_constants":650,"policy_cross_mode_root_reuses":11102,"policy_dependent_gates_per_mode":1887,"policy_full_cache_modes":0,"policy_independent_gates":27238,"policy_independent_roots":427,"policy_mode_builds":26,"policy_specialized_gates":49062,"policy_unspecialized_gates":0,"proof_seconds":0.055940971,"requested_roots":1641,"setup_seconds":0.086696193,"successor_applications":344,"successor_substitutions":26}`
- Support width min/median/max: 0 / 20 / 20
- Owner-tuple arity histogram: 0: 96, 1: 249, 2: 46
- Subset reuse: 48 distinct; 48 reused; uses min/median/max 112 / 175 / 287; operations instantiation: 6020, projection: 2870
- Mask word-length histogram: 0: 984, 1: 1886
- Actual checker mode counts: 17: 1, 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| substitute_variables | 287 | 8.765358 s | 48.1% |
| bdd_projection_relabel | 15252 | 5.810890 s | 31.9% |
| policy_construction_skolemization | 2 | 3.221268 s | 17.7% |
| export | 518 | 0.856929 s | 4.7% |
| seed_monitor_construction | 6 | 0.400622 s | 2.2% |
| instantiate_templates | 287 | 0.345784 s | 1.9% |
| from_aag | 861 | 0.317371 s | 1.7% |
| target_check | 1 | 0.172602 s | 0.9% |
| seed_solves | 6 | 0.167635 s | 0.9% |
| projection_metadata | 6601 | 0.141785 s | 0.8% |
| target_monitor_construction | 2 | 0.141309 s | 0.8% |
| support_extraction | 9799 | 0.087700 s | 0.5% |
| variable_cube_construction | 2585 | 0.032529 s | 0.2% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### load_balancer-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 56.511 s
- cgroup `memory.peak`: 1.05 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":12342,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":272,"policy_cross_mode_root_reuses":1292,"policy_dependent_gates_per_mode":492,"policy_full_cache_modes":0,"policy_independent_gates":2600,"policy_independent_roots":76,"policy_mode_builds":17,"policy_specialized_gates":8364,"policy_unspecialized_gates":0,"proof_seconds":0.014554433,"requested_roots":666,"setup_seconds":0.040959954,"successor_applications":224,"successor_substitutions":17}`
  - `target-9` (node cap 33554432): `{"aig_gates_visited":154106,"final_status":"VERIFIED","peak_live_nodes_sample":196609,"policy_counter_constants":812,"policy_cross_mode_root_reuses":23664,"policy_dependent_gates_per_mode":3198,"policy_full_cache_modes":0,"policy_independent_gates":58760,"policy_independent_roots":816,"policy_mode_builds":29,"policy_specialized_gates":92742,"policy_unspecialized_gates":0,"proof_seconds":0.092106269,"requested_roots":2286,"setup_seconds":0.097467662,"successor_applications":384,"successor_substitutions":29}`
- Support width min/median/max: 0 / 20 / 20
- Owner-tuple arity histogram: 0: 96, 1: 256, 2: 48
- Subset reuse: 56 distinct; 56 reused; uses min/median/max 112 / 196 / 308; operations instantiation: 8176, projection: 3080
- Mask word-length histogram: 0: 1056, 1: 2024
- Actual checker mode counts: 17: 1, 29: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| substitute_variables | 308 | 34.803361 s | 61.6% |
| bdd_projection_relabel | 18082 | 14.824549 s | 26.2% |
| policy_construction_skolemization | 2 | 9.129669 s | 16.2% |
| export | 556 | 1.990368 s | 3.5% |
| instantiate_templates | 308 | 0.466259 s | 0.8% |
| seed_monitor_construction | 6 | 0.400885 s | 0.7% |
| from_aag | 924 | 0.358111 s | 0.6% |
| target_check | 1 | 0.257769 s | 0.5% |
| seed_solves | 6 | 0.168747 s | 0.3% |
| projection_metadata | 7084 | 0.154713 s | 0.3% |
| target_monitor_construction | 2 | 0.144083 s | 0.3% |
| support_extraction | 12230 | 0.124496 s | 0.2% |
| variable_cube_construction | 2774 | 0.036912 s | 0.1% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### amba_decomposed_lock-n15

- Verdict/exit: REALIZABLE / 0
- Total wall: 41.660 s
- cgroup `memory.peak`: 2.25 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":1188,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":42,"policy_cross_mode_root_reuses":133,"policy_dependent_gates_per_mode":61,"policy_full_cache_modes":0,"policy_independent_gates":461,"policy_independent_roots":19,"policy_mode_builds":7,"policy_specialized_gates":427,"policy_unspecialized_gates":0,"proof_seconds":0.002750472,"requested_roots":143,"setup_seconds":0.038566001,"successor_applications":63,"successor_substitutions":7}`
  - `target-15` (node cap 67108864): `{"aig_gates_visited":397545,"final_status":"VERIFIED","peak_live_nodes_sample":17170433,"policy_counter_constants":272,"policy_cross_mode_root_reuses":833,"policy_dependent_gates_per_mode":171,"policy_full_cache_modes":0,"policy_independent_gates":393593,"policy_independent_roots":49,"policy_mode_builds":17,"policy_specialized_gates":2907,"policy_unspecialized_gates":0,"proof_seconds":6.792178658,"requested_roots":523,"setup_seconds":0.163178919,"successor_applications":153,"successor_substitutions":17}`
- Support width min/median/max: 7 / 7 / 7
- Owner-tuple arity histogram: 0: 80, 1: 190
- Subset reuse: 29 distinct; 29 reused; uses min/median/max 25 / 65 / 90; operations instantiation: 1100, projection: 810
- Mask word-length histogram: 1: 810
- Actual checker mode counts: 7: 1, 17: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| bdd_projection_relabel | 3896 | 23.176366 s | 55.6% |
| policy_construction_skolemization | 2 | 12.577459 s | 30.2% |
| substitute_variables | 90 | 8.109319 s | 19.5% |
| target_check | 1 | 7.027028 s | 16.9% |
| export | 202 | 1.606193 s | 3.9% |
| seed_monitor_construction | 6 | 0.398400 s | 1.0% |
| target_monitor_construction | 2 | 0.174411 s | 0.4% |
| seed_solves | 6 | 0.131819 s | 0.3% |
| instantiate_templates | 90 | 0.059018 s | 0.1% |
| support_extraction | 2276 | 0.036475 s | 0.1% |
| projection_metadata | 1890 | 0.022295 s | 0.1% |
| from_aag | 270 | 0.018621 s | 0.0% |
| variable_cube_construction | 812 | 0.007488 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_buffer-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 15.360 s
- cgroup `memory.peak`: 1.05 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":1897,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":42,"policy_cross_mode_root_reuses":294,"policy_dependent_gates_per_mode":163,"policy_full_cache_modes":0,"policy_independent_gates":535,"policy_independent_roots":42,"policy_mode_builds":7,"policy_specialized_gates":1141,"policy_unspecialized_gates":0,"proof_seconds":0.001313534,"requested_roots":214,"setup_seconds":0.038806435,"successor_applications":35,"successor_substitutions":7}`
  - `target-8` (node cap 33554432): `{"aig_gates_visited":13346,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":90,"policy_cross_mode_root_reuses":2720,"policy_dependent_gates_per_mode":883,"policy_full_cache_modes":0,"policy_independent_gates":4124,"policy_independent_roots":272,"policy_mode_builds":10,"policy_specialized_gates":8830,"policy_unspecialized_gates":0,"proof_seconds":0.005698563,"requested_roots":612,"setup_seconds":0.090200693,"successor_applications":50,"successor_substitutions":10}`
- Support width min/median/max: 4 / 4 / 4
- Owner-tuple arity histogram: 0: 16, 1: 248
- Subset reuse: 22 distinct; 22 reused; uses min/median/max 13 / 19 / 32; operations instantiation: 217, projection: 288
- Mask word-length histogram: 1: 288
- Actual checker mode counts: 7: 1, 10: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| policy_construction_skolemization | 2 | 9.594856 s | 62.5% |
| bdd_projection_relabel | 1276 | 5.398489 s | 35.1% |
| export | 133 | 1.405746 s | 9.2% |
| seed_monitor_construction | 6 | 0.393479 s | 2.6% |
| substitute_variables | 32 | 0.228961 s | 1.5% |
| target_monitor_construction | 2 | 0.138636 s | 0.9% |
| target_check | 1 | 0.135805 s | 0.9% |
| seed_solves | 6 | 0.129559 s | 0.8% |
| support_extraction | 700 | 0.021617 s | 0.1% |
| instantiate_templates | 32 | 0.007281 s | 0.0% |
| projection_metadata | 672 | 0.006935 s | 0.0% |
| from_aag | 96 | 0.006219 s | 0.0% |
| variable_cube_construction | 290 | 0.003902 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_buffer-n9

- Verdict/exit: UNKNOWN / 2
- Total wall: 120.598 s
- cgroup `memory.peak`: 1.77 GiB
- Censored stages: instantiate ≥ 0.000 s (absolute_deadline_exhausted)
- Checker `--stats`: no payload captured
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Legacy recorded mode counts (known inaccurate): none

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| generalize_gr1 | 1 | 120.577464 s | 100.0% |

### arbiter_on_inpchange-n6

- Verdict/exit: REALIZABLE / 0
- Total wall: 24.186 s
- cgroup `memory.peak`: 1.69 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":40375,"final_status":"VERIFIED","peak_live_nodes_sample":6881281,"policy_counter_constants":462,"policy_cross_mode_root_reuses":1540,"policy_dependent_gates_per_mode":296,"policy_full_cache_modes":0,"policy_independent_gates":25495,"policy_independent_roots":70,"policy_mode_builds":22,"policy_specialized_gates":6512,"policy_unspecialized_gates":0,"proof_seconds":3.004323377,"requested_roots":897,"setup_seconds":0.043374341,"successor_applications":130,"successor_substitutions":22}`
  - `target-6` (node cap 33554432): `{"aig_gates_visited":59768,"final_status":"VERIFIED","peak_live_nodes_sample":22675457,"policy_counter_constants":650,"policy_cross_mode_root_reuses":2496,"policy_dependent_gates_per_mode":403,"policy_full_cache_modes":0,"policy_independent_gates":37435,"policy_independent_roots":96,"policy_mode_builds":26,"policy_specialized_gates":10478,"policy_unspecialized_gates":0,"proof_seconds":11.908651161,"requested_roots":1206,"setup_seconds":0.084886353,"successor_applications":154,"successor_substitutions":26}`
- Support width min/median/max: 39 / 39 / 39
- Owner-tuple arity histogram: 0: 16, 1: 667
- Subset reuse: 35 distinct; 35 reused; uses min/median/max 53 / 63 / 116; operations instantiation: 1475, projection: 1160
- Mask word-length histogram: 1: 348, 2: 812
- Actual checker mode counts: 22: 1, 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 12.040713 s | 49.8% |
| from_aag | 348 | 2.945277 s | 12.2% |
| bdd_projection_relabel | 4839 | 1.993732 s | 8.2% |
| export | 4510 | 1.436321 s | 5.9% |
| seed_solves | 6 | 1.163858 s | 4.8% |
| instantiate_templates | 116 | 0.932786 s | 3.9% |
| policy_construction_skolemization | 2 | 0.749764 s | 3.1% |
| seed_monitor_construction | 6 | 0.444624 s | 1.8% |
| target_monitor_construction | 2 | 0.169516 s | 0.7% |
| projection_metadata | 2668 | 0.126362 s | 0.5% |
| support_extraction | 2635 | 0.076451 s | 0.3% |
| variable_cube_construction | 1044 | 0.031447 s | 0.1% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_on_inpchange-n7

- Verdict/exit: UNKNOWN / 2
- Total wall: 120.174 s
- cgroup `memory.peak`: 2.07 GiB
- Censored stages: target_check ≥ 106.879 s (absolute_deadline_exhausted, sigterm)
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":40375,"final_status":"VERIFIED","peak_live_nodes_sample":6881281,"policy_counter_constants":462,"policy_cross_mode_root_reuses":1540,"policy_dependent_gates_per_mode":296,"policy_full_cache_modes":0,"policy_independent_gates":25495,"policy_independent_roots":70,"policy_mode_builds":22,"policy_specialized_gates":6512,"policy_unspecialized_gates":0,"proof_seconds":2.94879603,"requested_roots":897,"setup_seconds":0.042834812,"successor_applications":130,"successor_substitutions":22}`
- Support width min/median/max: 39 / 39 / 39
- Owner-tuple arity histogram: 0: 16, 1: 690
- Subset reuse: 41 distinct; 41 reused; uses min/median/max 53 / 73 / 126; operations instantiation: 2063, projection: 1260
- Mask word-length histogram: 1: 378, 2: 882
- Actual checker mode counts: 22: 1, 30: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| from_aag | 378 | 3.121210 s | 2.6% |
| bdd_projection_relabel | 5717 | 2.418837 s | 2.0% |
| export | 5334 | 1.931088 s | 1.6% |
| instantiate_templates | 126 | 1.297049 s | 1.1% |
| seed_solves | 6 | 1.167267 s | 1.0% |
| policy_construction_skolemization | 2 | 1.003369 s | 0.8% |
| seed_monitor_construction | 6 | 0.444079 s | 0.4% |
| target_monitor_construction | 2 | 0.169493 s | 0.1% |
| projection_metadata | 2898 | 0.115364 s | 0.1% |
| support_extraction | 3323 | 0.095624 s | 0.1% |
| variable_cube_construction | 1134 | 0.035545 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_check | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### round_robin_arbiter_unreal2-n5

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 1.969 s
- cgroup `memory.peak`: 1.34 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-5` (node cap 67108864): `{"aig_gates_visited":141862,"final_status":"VERIFIED","peak_live_nodes_sample":196609,"policy_counter_constants":240,"policy_cross_mode_root_reuses":270,"policy_dependent_gates_per_mode":132039,"policy_full_cache_modes":16,"policy_independent_gates":326,"policy_independent_roots":270,"policy_mode_builds":16,"policy_specialized_gates":0,"policy_unspecialized_gates":132039,"proof_seconds":0.473029058,"requested_roots":2754,"setup_seconds":0.169795099,"successor_applications":4736,"successor_substitutions":16}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 60, 2: 40
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 16: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 0.903720 s | 45.9% |
| target_solve | 1 | 0.808936 s | 41.1% |
| target_monitor_construction | 1 | 0.079291 s | 4.0% |
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
- Total wall: 7.162 s
- cgroup `memory.peak`: 1.54 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 67108864): `{"aig_gates_visited":734342,"final_status":"VERIFIED","peak_live_nodes_sample":1310721,"policy_counter_constants":342,"policy_cross_mode_root_reuses":1043,"policy_dependent_gates_per_mode":714747,"policy_full_cache_modes":19,"policy_independent_gates":1112,"policy_independent_roots":1043,"policy_mode_builds":19,"policy_specialized_gates":0,"policy_unspecialized_gates":714747,"proof_seconds":2.506628863,"requested_roots":5059,"setup_seconds":0.194798145,"successor_applications":7744,"successor_substitutions":19}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 72, 2: 60
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 19: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 3.468387 s | 48.4% |
| target_check | 1 | 3.428654 s | 47.9% |
| target_monitor_construction | 1 | 0.085688 s | 1.2% |
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
- Total wall: 31.202 s
- cgroup `memory.peak`: 2.48 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 67108864): `{"aig_gates_visited":3646651,"final_status":"VERIFIED","peak_live_nodes_sample":6553601,"policy_counter_constants":462,"policy_cross_mode_root_reuses":4120,"policy_dependent_gates_per_mode":3609146,"policy_full_cache_modes":22,"policy_independent_gates":4202,"policy_independent_roots":4120,"policy_mode_builds":22,"policy_specialized_gates":0,"policy_unspecialized_gates":3609146,"proof_seconds":13.670556087,"requested_roots":10210,"setup_seconds":0.250204344,"successor_applications":11832,"successor_substitutions":22}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 84, 2: 84
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 22: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 15.827581 s | 50.7% |
| target_solve | 1 | 15.099406 s | 48.4% |
| target_monitor_construction | 1 | 0.093662 s | 0.3% |
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
- Total wall: 119.921 s
- cgroup `memory.peak`: 2.10 GiB
- Censored stages: canonicalize ≥ 119.620 s (subprocess_timeout); target_monitor_construction ≥ 119.619 s (subprocess_timeout)
- Checker `--stats`: no payload captured
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 42, 1: 12
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: none

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_monitor_construction | 1 | 119.619420 s | 99.7% |
| seed_monitor_construction | 1 | 0.073688 s | 0.1% |
| seed_solves | 1 | 0.044348 s | 0.0% |
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
- Total wall: 2.146 s
- cgroup `memory.peak`: 951.44 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":12710,"final_status":"VERIFIED","peak_live_nodes_sample":65537,"policy_counter_constants":462,"policy_cross_mode_root_reuses":1430,"policy_dependent_gates_per_mode":296,"policy_full_cache_modes":0,"policy_independent_gates":5072,"policy_independent_roots":65,"policy_mode_builds":22,"policy_specialized_gates":6512,"policy_unspecialized_gates":0,"proof_seconds":0.029606566,"requested_roots":842,"setup_seconds":0.039414599,"successor_applications":130,"successor_substitutions":22}`
  - `target-6` (node cap 33554432): `{"aig_gates_visited":19687,"final_status":"VERIFIED","peak_live_nodes_sample":131073,"policy_counter_constants":650,"policy_cross_mode_root_reuses":2340,"policy_dependent_gates_per_mode":403,"policy_full_cache_modes":0,"policy_independent_gates":7632,"policy_independent_roots":90,"policy_mode_builds":26,"policy_specialized_gates":10478,"policy_unspecialized_gates":0,"proof_seconds":0.052756541,"requested_roots":1140,"setup_seconds":0.080694334,"successor_applications":154,"successor_substitutions":26}`
- Support width min/median/max: 19 / 19 / 19
- Owner-tuple arity histogram: 0: 12, 1: 325
- Subset reuse: 34 distinct; 34 reused; uses min/median/max 53 / 63 / 116; operations instantiation: 1475, projection: 1044
- Mask word-length histogram: 1: 1044
- Actual checker mode counts: 22: 1, 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| bdd_projection_relabel | 4607 | 0.341817 s | 15.9% |
| seed_monitor_construction | 4 | 0.272028 s | 12.7% |
| export | 4510 | 0.267824 s | 12.5% |
| instantiate_templates | 116 | 0.226930 s | 10.6% |
| from_aag | 232 | 0.198266 s | 9.2% |
| seed_solves | 4 | 0.182188 s | 8.5% |
| policy_construction_skolemization | 2 | 0.150170 s | 7.0% |
| target_monitor_construction | 2 | 0.144846 s | 6.7% |
| target_check | 1 | 0.138739 s | 6.5% |
| projection_metadata | 2320 | 0.053860 s | 2.5% |
| support_extraction | 2519 | 0.042147 s | 2.0% |
| variable_cube_construction | 1044 | 0.016787 s | 0.8% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### prioritized_arbiter-n7

- Verdict/exit: REALIZABLE / 0
- Total wall: 1.024 s
- cgroup `memory.peak`: 930.61 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":5754,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":156,"policy_cross_mode_root_reuses":611,"policy_dependent_gates_per_mode":378,"policy_full_cache_modes":0,"policy_independent_gates":642,"policy_independent_roots":47,"policy_mode_builds":13,"policy_specialized_gates":4914,"policy_unspecialized_gates":0,"proof_seconds":0.002360553,"requested_roots":394,"setup_seconds":0.038767757,"successor_applications":88,"successor_substitutions":13}`
  - `target-7` (node cap 33554432): `{"aig_gates_visited":19751,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":272,"policy_cross_mode_root_reuses":2499,"policy_dependent_gates_per_mode":982,"policy_full_cache_modes":0,"policy_independent_gates":2776,"policy_independent_roots":147,"policy_mode_builds":17,"policy_specialized_gates":16694,"policy_unspecialized_gates":0,"proof_seconds":0.005990536,"requested_roots":704,"setup_seconds":0.078739644,"successor_applications":116,"successor_substitutions":17}`
- Support width min/median/max: 7 / 9 / 9
- Owner-tuple arity histogram: 0: 60, 1: 156
- Subset reuse: 19 distinct; 19 reused; uses min/median/max 30 / 40 / 70; operations instantiation: 430, projection: 490
- Mask word-length histogram: 1: 490
- Actual checker mode counts: 13: 1, 17: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| seed_monitor_construction | 4 | 0.258623 s | 25.3% |
| target_monitor_construction | 2 | 0.136492 s | 13.3% |
| seed_solves | 4 | 0.089652 s | 8.8% |
| target_check | 1 | 0.088631 s | 8.7% |
| bdd_projection_relabel | 2256 | 0.045495 s | 4.4% |
| instantiate_templates | 70 | 0.020710 s | 2.0% |
| policy_construction_skolemization | 2 | 0.020215 s | 2.0% |
| projection_metadata | 1120 | 0.013740 s | 1.3% |
| from_aag | 140 | 0.012539 s | 1.2% |
| support_extraction | 1276 | 0.010344 s | 1.0% |
| export | 236 | 0.010166 s | 1.0% |
| variable_cube_construction | 492 | 0.004854 s | 0.5% |
| substitute_variables | 70 | 0.004012 s | 0.4% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |
