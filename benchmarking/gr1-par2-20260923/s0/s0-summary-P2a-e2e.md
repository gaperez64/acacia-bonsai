# S0 diagnostic summary

Source: `/home/gperez/GIT-repos/acacia-gr1-par2-timing/benchmarking/gr1-par2-20260923/s0/raw-p2a`

## Notes

- End-to-end: P1.4 tools (build-P14-dbe8c2b) + P2a generalizer (112eb7fe) + frozen BuDDy adapter 980aa651, timing worktree, serial, 120 s, 8 GiB

## Targets

| Target | Verdict | Exit | Total wall | cgroup memory.peak | Censored stage |
|---|---:|---:|---:|---:|---|
| arbiter_with_cancel-n8 | REALIZABLE | 0 | 6.263 s | 1.18 GiB | none |
| arbiter_with_cancel-n9 | REALIZABLE | 0 | 10.424 s | 1.48 GiB | none |
| arbiter_with_cancel-n10 | REALIZABLE | 0 | 26.197 s | 1.94 GiB | none |
| load_balancer_unreal2-n6 | UNREALIZABLE | 1 | 2.135 s | 1.32 GiB | none |
| load_balancer_unreal2-n7 | UNREALIZABLE | 1 | 11.126 s | 1.58 GiB | none |
| load_balancer-n8 | REALIZABLE | 0 | 10.417 s | 1.02 GiB | none |
| load_balancer-n9 | REALIZABLE | 0 | 23.488 s | 1.05 GiB | none |
| amba_decomposed_lock-n15 | REALIZABLE | 0 | 35.903 s | 2.22 GiB | none |
| arbiter_with_buffer-n8 | REALIZABLE | 0 | 15.268 s | 1.05 GiB | none |
| arbiter_with_buffer-n9 | REALIZABLE | 0 | 117.732 s | 1.87 GiB | none |
| arbiter_on_inpchange-n6 | REALIZABLE | 0 | 23.751 s | 1.69 GiB | none |
| arbiter_on_inpchange-n7 | UNKNOWN | 2 | 120.154 s | 2.07 GiB | target_check ≥ 106.247 s |
| round_robin_arbiter_unreal2-n5 | UNREALIZABLE | 1 | 1.999 s | 1.34 GiB | none |
| round_robin_arbiter_unreal2-n6 | UNREALIZABLE | 1 | 7.116 s | 1.54 GiB | none |
| round_robin_arbiter_unreal2-n7 | UNREALIZABLE | 1 | 31.053 s | 2.48 GiB | none |
| collector_v1-n11 | UNKNOWN | 2 | 119.948 s | 2.10 GiB | canonicalize ≥ 119.637 s; target_monitor_construction ≥ 119.637 s |
| arbiter-n6 | REALIZABLE | 0 | 1.994 s | 950.45 MiB | none |
| prioritized_arbiter-n7 | REALIZABLE | 0 | 1.171 s | 932.50 MiB | none |

## Cross-target phase dominance

| Target | Dominant phase | Second | Third |
|---|---|---|---|
| arbiter_with_cancel-n8 | target_check — 2.444 s (39.0%) | bdd_projection_relabel — 1.230 s (19.6%) | instantiate_templates — 0.750 s (12.0%) |
| arbiter_with_cancel-n9 | target_check — 6.093 s (58.5%) | bdd_projection_relabel — 1.514 s (14.5%) | instantiate_templates — 1.010 s (9.7%) |
| arbiter_with_cancel-n10 | target_check — 21.205 s (80.9%) | bdd_projection_relabel — 1.849 s (7.1%) | instantiate_templates — 1.333 s (5.1%) |
| load_balancer_unreal2-n6 | target_solve — 1.438 s (67.4%) | target_check — 0.415 s (19.4%) | target_monitor_construction — 0.087 s (4.1%) |
| load_balancer_unreal2-n7 | target_solve — 9.499 s (85.4%) | target_check — 1.332 s (12.0%) | target_monitor_construction — 0.091 s (0.8%) |
| load_balancer-n8 | bdd_projection_relabel — 6.283 s (60.3%) | policy_construction_skolemization — 3.472 s (33.3%) | substitute_variables — 1.018 s (9.8%) |
| load_balancer-n9 | bdd_projection_relabel — 14.040 s (59.8%) | policy_construction_skolemization — 8.583 s (36.5%) | substitute_variables — 3.606 s (15.4%) |
| amba_decomposed_lock-n15 | bdd_projection_relabel — 21.272 s (59.2%) | policy_construction_skolemization — 11.423 s (31.8%) | target_check — 7.115 s (19.8%) |
| arbiter_with_buffer-n8 | policy_construction_skolemization — 9.687 s (63.4%) | bdd_projection_relabel — 5.235 s (34.3%) | export — 1.389 s (9.1%) |
| arbiter_with_buffer-n9 | policy_construction_skolemization — 99.706 s (84.7%) | bdd_projection_relabel — 20.156 s (17.1%) | export — 4.833 s (4.1%) |
| arbiter_on_inpchange-n6 | target_check — 12.091 s (50.9%) | bdd_projection_relabel — 4.642 s (19.5%) | instantiate_templates — 2.316 s (9.7%) |
| arbiter_on_inpchange-n7 | target_check — ≥ 106.247 s (≥ 88.4%) | bdd_projection_relabel — 6.068 s (5.0%) | instantiate_templates — 3.406 s (2.8%) |
| round_robin_arbiter_unreal2-n5 | target_check — 0.910 s (45.5%) | target_solve — 0.819 s (41.0%) | target_monitor_construction — 0.079 s (3.9%) |
| round_robin_arbiter_unreal2-n6 | target_check — 3.423 s (48.1%) | target_solve — 3.418 s (48.0%) | target_monitor_construction — 0.085 s (1.2%) |
| round_robin_arbiter_unreal2-n7 | target_check — 15.973 s (51.4%) | target_solve — 14.792 s (47.6%) | target_monitor_construction — 0.095 s (0.3%) |
| collector_v1-n11 | target_monitor_construction — ≥ 239.273 s (≥ 199.5%) | canonicalize — ≥ 119.637 s (≥ 99.7%) | seed_monitor_construction — 0.075 s (0.1%) |
| arbiter-n6 | bdd_projection_relabel — 0.402 s (20.2%) | seed_monitor_construction — 0.291 s (14.6%) | export — 0.275 s (13.8%) |
| prioritized_arbiter-n7 | seed_monitor_construction — 0.320 s (27.4%) | target_monitor_construction — 0.163 s (13.9%) | seed_solves — 0.096 s (8.2%) |

## Per-target detail

### arbiter_with_cancel-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 6.263 s
- cgroup `memory.peak`: 1.18 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":21484,"final_status":"VERIFIED","peak_live_nodes_sample":524289,"policy_counter_constants":992,"policy_cross_mode_root_reuses":2240,"policy_dependent_gates_per_mode":436,"policy_full_cache_modes":0,"policy_independent_gates":6148,"policy_independent_roots":70,"policy_mode_builds":32,"policy_specialized_gates":13952,"policy_unspecialized_gates":0,"proof_seconds":0.138378731,"requested_roots":1512,"setup_seconds":0.039960973,"successor_applications":180,"successor_substitutions":32}`
  - `target-8` (node cap 33554432): `{"aig_gates_visited":70321,"final_status":"VERIFIED","peak_live_nodes_sample":7667713,"policy_counter_constants":2450,"policy_cross_mode_root_reuses":8000,"policy_dependent_gates_per_mode":985,"policy_full_cache_modes":0,"policy_independent_gates":17821,"policy_independent_roots":160,"policy_mode_builds":50,"policy_specialized_gates":49250,"policy_unspecialized_gates":0,"proof_seconds":2.340046337,"requested_roots":3468,"setup_seconds":0.081202213,"successor_applications":282,"successor_substitutions":50}`
- Support width min/median/max: 25 / 25 / 25
- Owner-tuple arity histogram: 0: 16, 1: 589
- Subset reuse: 48 distinct; 48 reused; uses min/median/max 73 / 115 / 188; operations instantiation: 3950, projection: 1880
- Mask word-length histogram: 1: 1316, 2: 564
- Actual checker mode counts: 32: 1, 50: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 2.443721 s | 39.0% |
| bdd_projection_relabel | 9402 | 1.229990 s | 19.6% |
| instantiate_templates | 188 | 0.750237 s | 12.0% |
| export | 8874 | 0.615732 s | 9.8% |
| seed_solves | 6 | 0.587525 s | 9.4% |
| seed_monitor_construction | 6 | 0.426063 s | 6.8% |
| policy_construction_skolemization | 2 | 0.339067 s | 5.4% |
| target_monitor_construction | 2 | 0.167097 s | 2.7% |
| support_extraction | 3760 | 0.071775 s | 1.1% |
| projection_metadata | 4324 | 0.066324 s | 1.1% |
| variable_cube_construction | 98 | 0.009730 s | 0.2% |
| from_aag | 8 | 0.005068 s | 0.1% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_cancel-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 10.424 s
- cgroup `memory.peak`: 1.48 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":21484,"final_status":"VERIFIED","peak_live_nodes_sample":524289,"policy_counter_constants":992,"policy_cross_mode_root_reuses":2240,"policy_dependent_gates_per_mode":436,"policy_full_cache_modes":0,"policy_independent_gates":6148,"policy_independent_roots":70,"policy_mode_builds":32,"policy_specialized_gates":13952,"policy_unspecialized_gates":0,"proof_seconds":0.137219168,"requested_roots":1512,"setup_seconds":0.039649524,"successor_applications":180,"successor_substitutions":32}`
  - `target-9` (node cap 33554432): `{"aig_gates_visited":95432,"final_status":"VERIFIED","peak_live_nodes_sample":17825793,"policy_counter_constants":3080,"policy_cross_mode_root_reuses":11088,"policy_dependent_gates_per_mode":1216,"policy_full_cache_modes":0,"policy_independent_gates":23310,"policy_independent_roots":198,"policy_mode_builds":56,"policy_specialized_gates":68096,"policy_unspecialized_gates":0,"proof_seconds":5.979009071,"requested_roots":4296,"setup_seconds":0.080525521,"successor_applications":316,"successor_substitutions":56}`
- Support width min/median/max: 25 / 25 / 25
- Owner-tuple arity histogram: 0: 16, 1: 608
- Subset reuse: 56 distinct; 56 reused; uses min/median/max 73 / 129 / 202; operations instantiation: 5374, projection: 2020
- Mask word-length histogram: 1: 1414, 2: 606
- Actual checker mode counts: 32: 1, 56: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 6.093068 s | 58.5% |
| bdd_projection_relabel | 11232 | 1.514235 s | 14.5% |
| instantiate_templates | 202 | 1.010063 s | 9.7% |
| export | 10366 | 0.790920 s | 7.6% |
| seed_solves | 6 | 0.584570 s | 5.6% |
| seed_monitor_construction | 6 | 0.424952 s | 4.1% |
| policy_construction_skolemization | 2 | 0.420726 s | 4.0% |
| target_monitor_construction | 2 | 0.169045 s | 1.6% |
| support_extraction | 4040 | 0.083846 s | 0.8% |
| projection_metadata | 4646 | 0.070001 s | 0.7% |
| variable_cube_construction | 114 | 0.014787 s | 0.1% |
| from_aag | 8 | 0.005038 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_cancel-n10

- Verdict/exit: REALIZABLE / 0
- Total wall: 26.197 s
- cgroup `memory.peak`: 1.94 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":21484,"final_status":"VERIFIED","peak_live_nodes_sample":524289,"policy_counter_constants":992,"policy_cross_mode_root_reuses":2240,"policy_dependent_gates_per_mode":436,"policy_full_cache_modes":0,"policy_independent_gates":6148,"policy_independent_roots":70,"policy_mode_builds":32,"policy_specialized_gates":13952,"policy_unspecialized_gates":0,"proof_seconds":0.13857784,"requested_roots":1512,"setup_seconds":0.040072161,"successor_applications":180,"successor_substitutions":32}`
  - `target-10` (node cap 33554432): `{"aig_gates_visited":125759,"final_status":"VERIFIED","peak_live_nodes_sample":31195137,"policy_counter_constants":3782,"policy_cross_mode_root_reuses":14880,"policy_dependent_gates_per_mode":1471,"policy_full_cache_modes":0,"policy_independent_gates":29678,"policy_independent_roots":240,"policy_mode_builds":62,"policy_specialized_gates":91202,"policy_unspecialized_gates":0,"proof_seconds":21.0783385,"requested_roots":5212,"setup_seconds":0.081980012,"successor_applications":350,"successor_substitutions":62}`
- Support width min/median/max: 25 / 25 / 25
- Owner-tuple arity histogram: 0: 16, 1: 627
- Subset reuse: 65 distinct; 65 reused; uses min/median/max 73 / 143 / 216; operations instantiation: 7165, projection: 2160
- Mask word-length histogram: 1: 1512, 2: 648
- Actual checker mode counts: 32: 1, 62: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 21.204772 s | 80.9% |
| bdd_projection_relabel | 13429 | 1.849039 s | 7.1% |
| instantiate_templates | 216 | 1.333022 s | 5.1% |
| export | 12018 | 1.049567 s | 4.0% |
| seed_solves | 6 | 0.588442 s | 2.2% |
| policy_construction_skolemization | 2 | 0.557496 s | 2.1% |
| seed_monitor_construction | 6 | 0.432939 s | 1.7% |
| target_monitor_construction | 2 | 0.177189 s | 0.7% |
| support_extraction | 4320 | 0.081872 s | 0.3% |
| projection_metadata | 4968 | 0.070209 s | 0.3% |
| variable_cube_construction | 132 | 0.022141 s | 0.1% |
| from_aag | 8 | 0.005442 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### load_balancer_unreal2-n6

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 2.135 s
- cgroup `memory.peak`: 1.32 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 67108864): `{"aig_gates_visited":7831,"final_status":"VERIFIED","peak_live_nodes_sample":786433,"policy_counter_constants":12,"policy_cross_mode_root_reuses":8,"policy_dependent_gates_per_mode":41,"policy_full_cache_modes":0,"policy_independent_gates":20,"policy_independent_roots":2,"policy_mode_builds":4,"policy_specialized_gates":164,"policy_unspecialized_gates":0,"proof_seconds":0.187975161,"requested_roots":1189,"setup_seconds":0.165371074,"successor_applications":2176,"successor_substitutions":4}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 12, 1: 46, 2: 70
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 4: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 1.437980 s | 67.4% |
| target_check | 1 | 0.414740 s | 19.4% |
| target_monitor_construction | 1 | 0.087437 s | 4.1% |
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
- Total wall: 11.126 s
- cgroup `memory.peak`: 1.58 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 67108864): `{"aig_gates_visited":15334,"final_status":"VERIFIED","peak_live_nodes_sample":3670017,"policy_counter_constants":12,"policy_cross_mode_root_reuses":8,"policy_dependent_gates_per_mode":41,"policy_full_cache_modes":0,"policy_independent_gates":20,"policy_independent_roots":2,"policy_mode_builds":4,"policy_specialized_gates":164,"policy_unspecialized_gates":0,"proof_seconds":1.000639522,"requested_roots":1494,"setup_seconds":0.188345604,"successor_applications":2752,"successor_substitutions":4}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 12, 1: 53, 2: 96
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 4: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 9.498770 s | 85.4% |
| target_check | 1 | 1.331508 s | 12.0% |
| target_monitor_construction | 1 | 0.090648 s | 0.8% |
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
- Total wall: 10.417 s
- cgroup `memory.peak`: 1.02 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":12342,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":272,"policy_cross_mode_root_reuses":1292,"policy_dependent_gates_per_mode":492,"policy_full_cache_modes":0,"policy_independent_gates":2600,"policy_independent_roots":76,"policy_mode_builds":17,"policy_specialized_gates":8364,"policy_unspecialized_gates":0,"proof_seconds":0.014417203,"requested_roots":666,"setup_seconds":0.039360622,"successor_applications":224,"successor_substitutions":17}`
  - `target-8` (node cap 33554432): `{"aig_gates_visited":78581,"final_status":"VERIFIED","peak_live_nodes_sample":131073,"policy_counter_constants":650,"policy_cross_mode_root_reuses":11102,"policy_dependent_gates_per_mode":1887,"policy_full_cache_modes":0,"policy_independent_gates":27238,"policy_independent_roots":427,"policy_mode_builds":26,"policy_specialized_gates":49062,"policy_unspecialized_gates":0,"proof_seconds":0.054312516,"requested_roots":1641,"setup_seconds":0.085045211,"successor_applications":344,"successor_substitutions":26}`
- Support width min/median/max: 0 / 20 / 20
- Owner-tuple arity histogram: 0: 96, 1: 249, 2: 46
- Subset reuse: 48 distinct; 48 reused; uses min/median/max 112 / 175 / 287; operations instantiation: 6020, projection: 2870
- Mask word-length histogram: 0: 984, 1: 1886
- Actual checker mode counts: 17: 1, 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| bdd_projection_relabel | 15252 | 6.282686 s | 60.3% |
| policy_construction_skolemization | 2 | 3.472315 s | 33.3% |
| substitute_variables | 287 | 1.018360 s | 9.8% |
| export | 518 | 0.824962 s | 7.9% |
| seed_monitor_construction | 6 | 0.397330 s | 3.8% |
| instantiate_templates | 287 | 0.228643 s | 2.2% |
| seed_solves | 6 | 0.169134 s | 1.6% |
| target_check | 1 | 0.168529 s | 1.6% |
| target_monitor_construction | 2 | 0.142255 s | 1.4% |
| support_extraction | 6649 | 0.084636 s | 0.8% |
| projection_metadata | 6601 | 0.065808 s | 0.6% |
| from_aag | 8 | 0.008148 s | 0.1% |
| variable_cube_construction | 100 | 0.004897 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### load_balancer-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 23.488 s
- cgroup `memory.peak`: 1.05 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":12342,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":272,"policy_cross_mode_root_reuses":1292,"policy_dependent_gates_per_mode":492,"policy_full_cache_modes":0,"policy_independent_gates":2600,"policy_independent_roots":76,"policy_mode_builds":17,"policy_specialized_gates":8364,"policy_unspecialized_gates":0,"proof_seconds":0.014513948,"requested_roots":666,"setup_seconds":0.040220216,"successor_applications":224,"successor_substitutions":17}`
  - `target-9` (node cap 33554432): `{"aig_gates_visited":154106,"final_status":"VERIFIED","peak_live_nodes_sample":196609,"policy_counter_constants":812,"policy_cross_mode_root_reuses":23664,"policy_dependent_gates_per_mode":3198,"policy_full_cache_modes":0,"policy_independent_gates":58760,"policy_independent_roots":816,"policy_mode_builds":29,"policy_specialized_gates":92742,"policy_unspecialized_gates":0,"proof_seconds":0.092627983,"requested_roots":2286,"setup_seconds":0.096359228,"successor_applications":384,"successor_substitutions":29}`
- Support width min/median/max: 0 / 20 / 20
- Owner-tuple arity histogram: 0: 96, 1: 256, 2: 48
- Subset reuse: 56 distinct; 56 reused; uses min/median/max 112 / 196 / 308; operations instantiation: 8176, projection: 3080
- Mask word-length histogram: 0: 1056, 1: 2024
- Actual checker mode counts: 17: 1, 29: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| bdd_projection_relabel | 18082 | 14.039985 s | 59.8% |
| policy_construction_skolemization | 2 | 8.582877 s | 36.5% |
| substitute_variables | 308 | 3.606437 s | 15.4% |
| export | 556 | 1.879478 s | 8.0% |
| seed_monitor_construction | 6 | 0.403261 s | 1.7% |
| instantiate_templates | 308 | 0.298499 s | 1.3% |
| target_check | 1 | 0.254684 s | 1.1% |
| seed_solves | 6 | 0.167037 s | 0.7% |
| target_monitor_construction | 2 | 0.143124 s | 0.6% |
| support_extraction | 7134 | 0.106440 s | 0.5% |
| projection_metadata | 7084 | 0.066940 s | 0.3% |
| from_aag | 8 | 0.008034 s | 0.0% |
| variable_cube_construction | 116 | 0.006598 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### amba_decomposed_lock-n15

- Verdict/exit: REALIZABLE / 0
- Total wall: 35.903 s
- cgroup `memory.peak`: 2.22 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":1188,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":42,"policy_cross_mode_root_reuses":133,"policy_dependent_gates_per_mode":61,"policy_full_cache_modes":0,"policy_independent_gates":461,"policy_independent_roots":19,"policy_mode_builds":7,"policy_specialized_gates":427,"policy_unspecialized_gates":0,"proof_seconds":0.002797707,"requested_roots":143,"setup_seconds":0.039388701,"successor_applications":63,"successor_substitutions":7}`
  - `target-15` (node cap 67108864): `{"aig_gates_visited":397545,"final_status":"VERIFIED","peak_live_nodes_sample":17170433,"policy_counter_constants":272,"policy_cross_mode_root_reuses":833,"policy_dependent_gates_per_mode":171,"policy_full_cache_modes":0,"policy_independent_gates":393593,"policy_independent_roots":49,"policy_mode_builds":17,"policy_specialized_gates":2907,"policy_unspecialized_gates":0,"proof_seconds":6.869519079,"requested_roots":523,"setup_seconds":0.175875441,"successor_applications":153,"successor_substitutions":17}`
- Support width min/median/max: 7 / 7 / 7
- Owner-tuple arity histogram: 0: 80, 1: 190
- Subset reuse: 29 distinct; 29 reused; uses min/median/max 25 / 65 / 90; operations instantiation: 1100, projection: 810
- Mask word-length histogram: 1: 810
- Actual checker mode counts: 7: 1, 17: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| bdd_projection_relabel | 3896 | 21.271917 s | 59.2% |
| policy_construction_skolemization | 2 | 11.422606 s | 31.8% |
| target_check | 1 | 7.114568 s | 19.8% |
| substitute_variables | 90 | 4.461870 s | 12.4% |
| export | 202 | 1.509687 s | 4.2% |
| seed_monitor_construction | 6 | 0.392661 s | 1.1% |
| target_monitor_construction | 2 | 0.174657 s | 0.5% |
| seed_solves | 6 | 0.134142 s | 0.4% |
| instantiate_templates | 90 | 0.035997 s | 0.1% |
| support_extraction | 1986 | 0.021271 s | 0.1% |
| projection_metadata | 1890 | 0.014462 s | 0.0% |
| from_aag | 8 | 0.002471 s | 0.0% |
| variable_cube_construction | 40 | 0.002017 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_buffer-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 15.268 s
- cgroup `memory.peak`: 1.05 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":1897,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":42,"policy_cross_mode_root_reuses":294,"policy_dependent_gates_per_mode":163,"policy_full_cache_modes":0,"policy_independent_gates":535,"policy_independent_roots":42,"policy_mode_builds":7,"policy_specialized_gates":1141,"policy_unspecialized_gates":0,"proof_seconds":0.001321925,"requested_roots":214,"setup_seconds":0.039703048,"successor_applications":35,"successor_substitutions":7}`
  - `target-8` (node cap 33554432): `{"aig_gates_visited":13346,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":90,"policy_cross_mode_root_reuses":2720,"policy_dependent_gates_per_mode":883,"policy_full_cache_modes":0,"policy_independent_gates":4124,"policy_independent_roots":272,"policy_mode_builds":10,"policy_specialized_gates":8830,"policy_unspecialized_gates":0,"proof_seconds":0.005608128,"requested_roots":612,"setup_seconds":0.091522274,"successor_applications":50,"successor_substitutions":10}`
- Support width min/median/max: 4 / 4 / 4
- Owner-tuple arity histogram: 0: 16, 1: 248
- Subset reuse: 22 distinct; 22 reused; uses min/median/max 13 / 19 / 32; operations instantiation: 217, projection: 288
- Mask word-length histogram: 1: 288
- Actual checker mode counts: 7: 1, 10: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| policy_construction_skolemization | 2 | 9.686788 s | 63.4% |
| bdd_projection_relabel | 1276 | 5.235229 s | 34.3% |
| export | 133 | 1.388715 s | 9.1% |
| seed_monitor_construction | 6 | 0.389164 s | 2.5% |
| substitute_variables | 32 | 0.146730 s | 1.0% |
| target_monitor_construction | 2 | 0.141201 s | 0.9% |
| target_check | 1 | 0.137861 s | 0.9% |
| seed_solves | 6 | 0.132261 s | 0.9% |
| support_extraction | 771 | 0.019793 s | 0.1% |
| instantiate_templates | 32 | 0.006115 s | 0.0% |
| projection_metadata | 672 | 0.004619 s | 0.0% |
| variable_cube_construction | 33 | 0.001420 s | 0.0% |
| from_aag | 8 | 0.001290 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_buffer-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 117.732 s
- cgroup `memory.peak`: 1.87 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":1897,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":42,"policy_cross_mode_root_reuses":294,"policy_dependent_gates_per_mode":163,"policy_full_cache_modes":0,"policy_independent_gates":535,"policy_independent_roots":42,"policy_mode_builds":7,"policy_specialized_gates":1141,"policy_unspecialized_gates":0,"proof_seconds":0.001328005,"requested_roots":214,"setup_seconds":0.03954593,"successor_applications":35,"successor_substitutions":7}`
  - `target-9` (node cap 33554432): `{"aig_gates_visited":26959,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":110,"policy_cross_mode_root_reuses":5830,"policy_dependent_gates_per_mode":1669,"policy_full_cache_modes":0,"policy_independent_gates":8145,"policy_independent_roots":530,"policy_mode_builds":11,"policy_specialized_gates":18359,"policy_unspecialized_gates":0,"proof_seconds":0.00954927,"requested_roots":938,"setup_seconds":0.123296728,"successor_applications":55,"successor_substitutions":11}`
- Support width min/median/max: 4 / 4 / 4
- Owner-tuple arity histogram: 0: 16, 1: 256
- Subset reuse: 23 distinct; 23 reused; uses min/median/max 13 / 21 / 34; operations instantiation: 254, projection: 306
- Mask word-length histogram: 1: 306
- Actual checker mode counts: 7: 1, 11: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| policy_construction_skolemization | 2 | 99.705828 s | 84.7% |
| bdd_projection_relabel | 1380 | 20.155576 s | 17.1% |
| export | 142 | 4.832797 s | 4.1% |
| substitute_variables | 34 | 0.961167 s | 0.8% |
| seed_monitor_construction | 6 | 0.388985 s | 0.3% |
| target_check | 1 | 0.272735 s | 0.2% |
| target_monitor_construction | 2 | 0.141209 s | 0.1% |
| seed_solves | 6 | 0.131788 s | 0.1% |
| support_extraction | 820 | 0.067538 s | 0.1% |
| instantiate_templates | 34 | 0.005884 s | 0.0% |
| projection_metadata | 714 | 0.004529 s | 0.0% |
| variable_cube_construction | 34 | 0.001827 s | 0.0% |
| from_aag | 8 | 0.001513 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_on_inpchange-n6

- Verdict/exit: REALIZABLE / 0
- Total wall: 23.751 s
- cgroup `memory.peak`: 1.69 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":40375,"final_status":"VERIFIED","peak_live_nodes_sample":6881281,"policy_counter_constants":462,"policy_cross_mode_root_reuses":1540,"policy_dependent_gates_per_mode":296,"policy_full_cache_modes":0,"policy_independent_gates":25495,"policy_independent_roots":70,"policy_mode_builds":22,"policy_specialized_gates":6512,"policy_unspecialized_gates":0,"proof_seconds":2.96307901,"requested_roots":897,"setup_seconds":0.043192775,"successor_applications":130,"successor_substitutions":22}`
  - `target-6` (node cap 33554432): `{"aig_gates_visited":59768,"final_status":"VERIFIED","peak_live_nodes_sample":22675457,"policy_counter_constants":650,"policy_cross_mode_root_reuses":2496,"policy_dependent_gates_per_mode":403,"policy_full_cache_modes":0,"policy_independent_gates":37435,"policy_independent_roots":96,"policy_mode_builds":26,"policy_specialized_gates":10478,"policy_unspecialized_gates":0,"proof_seconds":11.963788349,"requested_roots":1206,"setup_seconds":0.085451434,"successor_applications":154,"successor_substitutions":26}`
- Support width min/median/max: 39 / 39 / 39
- Owner-tuple arity histogram: 0: 16, 1: 667
- Subset reuse: 35 distinct; 35 reused; uses min/median/max 53 / 63 / 116; operations instantiation: 1475, projection: 1160
- Mask word-length histogram: 1: 348, 2: 812
- Actual checker mode counts: 22: 1, 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 12.090727 s | 50.9% |
| bdd_projection_relabel | 4839 | 4.641615 s | 19.5% |
| instantiate_templates | 116 | 2.315605 s | 9.7% |
| export | 4510 | 1.416723 s | 6.0% |
| seed_solves | 6 | 1.167049 s | 4.9% |
| policy_construction_skolemization | 2 | 0.730074 s | 3.1% |
| seed_monitor_construction | 6 | 0.447252 s | 1.9% |
| target_monitor_construction | 2 | 0.169295 s | 0.7% |
| support_extraction | 2320 | 0.067779 s | 0.3% |
| projection_metadata | 2668 | 0.054271 s | 0.2% |
| from_aag | 8 | 0.030529 s | 0.1% |
| variable_cube_construction | 72 | 0.005399 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_on_inpchange-n7

- Verdict/exit: UNKNOWN / 2
- Total wall: 120.154 s
- cgroup `memory.peak`: 2.07 GiB
- Censored stages: target_check ≥ 106.247 s (absolute_deadline_exhausted, sigterm)
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":40375,"final_status":"VERIFIED","peak_live_nodes_sample":6881281,"policy_counter_constants":462,"policy_cross_mode_root_reuses":1540,"policy_dependent_gates_per_mode":296,"policy_full_cache_modes":0,"policy_independent_gates":25495,"policy_independent_roots":70,"policy_mode_builds":22,"policy_specialized_gates":6512,"policy_unspecialized_gates":0,"proof_seconds":3.063842537,"requested_roots":897,"setup_seconds":0.043463288,"successor_applications":130,"successor_substitutions":22}`
- Support width min/median/max: 39 / 39 / 39
- Owner-tuple arity histogram: 0: 16, 1: 690
- Subset reuse: 41 distinct; 41 reused; uses min/median/max 53 / 73 / 126; operations instantiation: 2063, projection: 1260
- Mask word-length histogram: 1: 378, 2: 882
- Actual checker mode counts: 22: 1, 30: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| bdd_projection_relabel | 5717 | 6.067641 s | 5.0% |
| instantiate_templates | 126 | 3.405906 s | 2.8% |
| export | 5334 | 2.013362 s | 1.7% |
| seed_solves | 6 | 1.172144 s | 1.0% |
| policy_construction_skolemization | 2 | 1.035224 s | 0.9% |
| seed_monitor_construction | 6 | 0.443664 s | 0.4% |
| target_monitor_construction | 2 | 0.173195 s | 0.1% |
| support_extraction | 2520 | 0.078040 s | 0.1% |
| projection_metadata | 2898 | 0.064397 s | 0.1% |
| from_aag | 8 | 0.032276 s | 0.0% |
| variable_cube_construction | 84 | 0.008460 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_check | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### round_robin_arbiter_unreal2-n5

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 1.999 s
- cgroup `memory.peak`: 1.34 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-5` (node cap 67108864): `{"aig_gates_visited":141862,"final_status":"VERIFIED","peak_live_nodes_sample":196609,"policy_counter_constants":240,"policy_cross_mode_root_reuses":270,"policy_dependent_gates_per_mode":132039,"policy_full_cache_modes":16,"policy_independent_gates":326,"policy_independent_roots":270,"policy_mode_builds":16,"policy_specialized_gates":0,"policy_unspecialized_gates":132039,"proof_seconds":0.477691675,"requested_roots":2754,"setup_seconds":0.169305296,"successor_applications":4736,"successor_substitutions":16}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 60, 2: 40
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 16: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 0.910062 s | 45.5% |
| target_solve | 1 | 0.819444 s | 41.0% |
| target_monitor_construction | 1 | 0.078750 s | 3.9% |
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
- Total wall: 7.116 s
- cgroup `memory.peak`: 1.54 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 67108864): `{"aig_gates_visited":734342,"final_status":"VERIFIED","peak_live_nodes_sample":1310721,"policy_counter_constants":342,"policy_cross_mode_root_reuses":1043,"policy_dependent_gates_per_mode":714747,"policy_full_cache_modes":19,"policy_independent_gates":1112,"policy_independent_roots":1043,"policy_mode_builds":19,"policy_specialized_gates":0,"policy_unspecialized_gates":714747,"proof_seconds":2.490710444,"requested_roots":5059,"setup_seconds":0.193580951,"successor_applications":7744,"successor_substitutions":19}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 72, 2: 60
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 19: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 3.423088 s | 48.1% |
| target_solve | 1 | 3.417949 s | 48.0% |
| target_monitor_construction | 1 | 0.084988 s | 1.2% |
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
- Total wall: 31.053 s
- cgroup `memory.peak`: 2.48 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 67108864): `{"aig_gates_visited":3646651,"final_status":"VERIFIED","peak_live_nodes_sample":6553601,"policy_counter_constants":462,"policy_cross_mode_root_reuses":4120,"policy_dependent_gates_per_mode":3609146,"policy_full_cache_modes":22,"policy_independent_gates":4202,"policy_independent_roots":4120,"policy_mode_builds":22,"policy_specialized_gates":0,"policy_unspecialized_gates":3609146,"proof_seconds":13.921962217,"requested_roots":10210,"setup_seconds":0.243361291,"successor_applications":11832,"successor_substitutions":22}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 84, 2: 84
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 22: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 15.973074 s | 51.4% |
| target_solve | 1 | 14.791512 s | 47.6% |
| target_monitor_construction | 1 | 0.094587 s | 0.3% |
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
- Total wall: 119.948 s
- cgroup `memory.peak`: 2.10 GiB
- Censored stages: canonicalize ≥ 119.637 s (subprocess_timeout); target_monitor_construction ≥ 119.637 s (subprocess_timeout)
- Checker `--stats`: no payload captured
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 42, 1: 12
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: none

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_monitor_construction | 1 | 119.636560 s | 99.7% |
| seed_monitor_construction | 1 | 0.074770 s | 0.1% |
| seed_solves | 1 | 0.044250 s | 0.0% |
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
- Total wall: 1.994 s
- cgroup `memory.peak`: 950.45 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":12710,"final_status":"VERIFIED","peak_live_nodes_sample":65537,"policy_counter_constants":462,"policy_cross_mode_root_reuses":1430,"policy_dependent_gates_per_mode":296,"policy_full_cache_modes":0,"policy_independent_gates":5072,"policy_independent_roots":65,"policy_mode_builds":22,"policy_specialized_gates":6512,"policy_unspecialized_gates":0,"proof_seconds":0.032742372,"requested_roots":842,"setup_seconds":0.0400499,"successor_applications":130,"successor_substitutions":22}`
  - `target-6` (node cap 33554432): `{"aig_gates_visited":19687,"final_status":"VERIFIED","peak_live_nodes_sample":131073,"policy_counter_constants":650,"policy_cross_mode_root_reuses":2340,"policy_dependent_gates_per_mode":403,"policy_full_cache_modes":0,"policy_independent_gates":7632,"policy_independent_roots":90,"policy_mode_builds":26,"policy_specialized_gates":10478,"policy_unspecialized_gates":0,"proof_seconds":0.058751892,"requested_roots":1140,"setup_seconds":0.082402456,"successor_applications":154,"successor_substitutions":26}`
- Support width min/median/max: 19 / 19 / 19
- Owner-tuple arity histogram: 0: 12, 1: 325
- Subset reuse: 34 distinct; 34 reused; uses min/median/max 53 / 63 / 116; operations instantiation: 1475, projection: 1044
- Mask word-length histogram: 1: 1044
- Actual checker mode counts: 22: 1, 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| bdd_projection_relabel | 4607 | 0.401774 s | 20.2% |
| seed_monitor_construction | 4 | 0.290751 s | 14.6% |
| export | 4510 | 0.275178 s | 13.8% |
| instantiate_templates | 116 | 0.198974 s | 10.0% |
| seed_solves | 4 | 0.192841 s | 9.7% |
| target_monitor_construction | 2 | 0.153022 s | 7.7% |
| policy_construction_skolemization | 2 | 0.149526 s | 7.5% |
| target_check | 1 | 0.147323 s | 7.4% |
| support_extraction | 2088 | 0.032984 s | 1.7% |
| projection_metadata | 2320 | 0.030227 s | 1.5% |
| from_aag | 6 | 0.003920 s | 0.2% |
| variable_cube_construction | 72 | 0.002658 s | 0.1% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### prioritized_arbiter-n7

- Verdict/exit: REALIZABLE / 0
- Total wall: 1.171 s
- cgroup `memory.peak`: 932.50 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":5754,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":156,"policy_cross_mode_root_reuses":611,"policy_dependent_gates_per_mode":378,"policy_full_cache_modes":0,"policy_independent_gates":642,"policy_independent_roots":47,"policy_mode_builds":13,"policy_specialized_gates":4914,"policy_unspecialized_gates":0,"proof_seconds":0.002673992,"requested_roots":394,"setup_seconds":0.039137342,"successor_applications":88,"successor_substitutions":13}`
  - `target-7` (node cap 33554432): `{"aig_gates_visited":19751,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":272,"policy_cross_mode_root_reuses":2499,"policy_dependent_gates_per_mode":982,"policy_full_cache_modes":0,"policy_independent_gates":2776,"policy_independent_roots":147,"policy_mode_builds":17,"policy_specialized_gates":16694,"policy_unspecialized_gates":0,"proof_seconds":0.006880468,"requested_roots":704,"setup_seconds":0.080372258,"successor_applications":116,"successor_substitutions":17}`
- Support width min/median/max: 7 / 9 / 9
- Owner-tuple arity histogram: 0: 60, 1: 156
- Subset reuse: 19 distinct; 19 reused; uses min/median/max 30 / 40 / 70; operations instantiation: 430, projection: 490
- Mask word-length histogram: 1: 490
- Actual checker mode counts: 13: 1, 17: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| seed_monitor_construction | 4 | 0.320459 s | 27.4% |
| target_monitor_construction | 2 | 0.163270 s | 13.9% |
| seed_solves | 4 | 0.096184 s | 8.2% |
| target_check | 1 | 0.091484 s | 7.8% |
| bdd_projection_relabel | 2256 | 0.052512 s | 4.5% |
| policy_construction_skolemization | 2 | 0.021244 s | 1.8% |
| instantiate_templates | 70 | 0.015900 s | 1.4% |
| support_extraction | 1336 | 0.012286 s | 1.0% |
| export | 236 | 0.010999 s | 0.9% |
| projection_metadata | 1120 | 0.009851 s | 0.8% |
| substitute_variables | 70 | 0.004597 s | 0.4% |
| from_aag | 6 | 0.001068 s | 0.1% |
| variable_cube_construction | 28 | 0.000807 s | 0.1% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |
