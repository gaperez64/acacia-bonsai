# S0 diagnostic summary

Source: `/home/gperez/GIT-repos/acacia-gr1-par2-timing/benchmarking/gr1-par2-20260923/s0/raw-p2c`

## Notes

- End-to-end: P1.4 tools (build-P14-dbe8c2b) + P2c generalizer (b123e090) + adapter 0043e97b, timing worktree, serial, 120 s, 8 GiB

## Targets

| Target | Verdict | Exit | Total wall | cgroup memory.peak | Censored stage |
|---|---:|---:|---:|---:|---|
| arbiter_with_cancel-n8 | REALIZABLE | 0 | 5.103 s | 945.80 MiB | none |
| arbiter_with_cancel-n9 | REALIZABLE | 0 | 9.351 s | 1.21 GiB | none |
| arbiter_with_cancel-n10 | REALIZABLE | 0 | 24.526 s | 1.67 GiB | none |
| load_balancer_unreal2-n6 | UNREALIZABLE | 1 | 2.190 s | 1.32 GiB | none |
| load_balancer_unreal2-n7 | UNREALIZABLE | 1 | 11.310 s | 1.59 GiB | none |
| load_balancer-n8 | REALIZABLE | 0 | 9.369 s | 703.84 MiB | none |
| load_balancer-n9 | REALIZABLE | 0 | 22.946 s | 735.71 MiB | none |
| amba_decomposed_lock-n15 | REALIZABLE | 0 | 35.639 s | 1.84 GiB | none |
| arbiter_with_buffer-n8 | REALIZABLE | 0 | 14.783 s | 715.12 MiB | none |
| arbiter_with_buffer-n9 | REALIZABLE | 0 | 117.345 s | 1.81 GiB | none |
| arbiter_on_inpchange-n6 | REALIZABLE | 0 | 17.272 s | 1.38 GiB | none |
| arbiter_on_inpchange-n7 | UNKNOWN | 2 | 120.095 s | 1.76 GiB | target_check ≥ 112.957 s |
| round_robin_arbiter_unreal2-n5 | UNREALIZABLE | 1 | 2.009 s | 1.35 GiB | none |
| round_robin_arbiter_unreal2-n6 | UNREALIZABLE | 1 | 7.221 s | 1.55 GiB | none |
| round_robin_arbiter_unreal2-n7 | UNREALIZABLE | 1 | 31.251 s | 2.49 GiB | none |
| collector_v1-n11 | UNKNOWN | 2 | 120.057 s | 2.12 GiB | canonicalize ≥ 119.647 s |
| arbiter-n6 | REALIZABLE | 0 | 1.358 s | 687.90 MiB | none |
| prioritized_arbiter-n7 | REALIZABLE | 0 | 0.806 s | 681.34 MiB | none |

## Cross-target phase dominance

| Target | Dominant phase | Second | Third |
|---|---|---|---|
| arbiter_with_cancel-n8 | target_check — 2.520 s (49.4%) | candidate_builder — 2.359 s (46.2%) | bdd_projection_relabel — 0.863 s (16.9%) |
| arbiter_with_cancel-n9 | target_check — 6.195 s (66.2%) | candidate_builder — 2.940 s (31.4%) | bdd_projection_relabel — 1.173 s (12.5%) |
| arbiter_with_cancel-n10 | target_check — 20.651 s (84.2%) | candidate_builder — 3.661 s (14.9%) | bdd_projection_relabel — 1.537 s (6.3%) |
| load_balancer_unreal2-n6 | target_solve — 1.474 s (67.3%) | target_check — 0.421 s (19.2%) | target_monitor_construction — 0.084 s (3.8%) |
| load_balancer_unreal2-n7 | target_solve — 9.553 s (84.5%) | target_check — 1.439 s (12.7%) | target_monitor_construction — 0.091 s (0.8%) |
| load_balancer-n8 | candidate_builder — 8.971 s (95.8%) | bdd_projection_relabel — 5.673 s (60.5%) | policy_construction_skolemization — 3.239 s (34.6%) |
| load_balancer-n9 | candidate_builder — 22.450 s (97.8%) | bdd_projection_relabel — 13.921 s (60.7%) | policy_construction_skolemization — 8.538 s (37.2%) |
| amba_decomposed_lock-n15 | candidate_builder — 28.370 s (79.6%) | bdd_projection_relabel — 21.269 s (59.7%) | policy_construction_skolemization — 11.444 s (32.1%) |
| arbiter_with_buffer-n8 | candidate_builder — 14.424 s (97.6%) | policy_construction_skolemization — 9.416 s (63.7%) | bdd_projection_relabel — 5.223 s (35.3%) |
| arbiter_with_buffer-n9 | candidate_builder — 116.831 s (99.6%) | policy_construction_skolemization — 99.163 s (84.5%) | bdd_projection_relabel — 20.632 s (17.6%) |
| arbiter_on_inpchange-n6 | target_check — 12.184 s (70.5%) | candidate_builder — 4.875 s (28.2%) | bdd_projection_relabel — 2.618 s (15.2%) |
| arbiter_on_inpchange-n7 | target_check — ≥ 112.957 s (≥ 94.1%) | candidate_builder — 6.939 s (5.8%) | bdd_projection_relabel — 3.906 s (3.3%) |
| round_robin_arbiter_unreal2-n5 | target_check — 0.909 s (45.2%) | target_solve — 0.810 s (40.3%) | target_monitor_construction — 0.079 s (3.9%) |
| round_robin_arbiter_unreal2-n6 | target_solve — 3.482 s (48.2%) | target_check — 3.442 s (47.7%) | target_monitor_construction — 0.085 s (1.2%) |
| round_robin_arbiter_unreal2-n7 | target_check — 15.843 s (50.7%) | target_solve — 15.097 s (48.3%) | target_monitor_construction — 0.094 s (0.3%) |
| collector_v1-n11 | candidate_builder — 119.857 s (99.8%) | canonicalize — ≥ 119.647 s (≥ 99.7%) | n/a |
| arbiter-n6 | candidate_builder — 1.004 s (74.0%) | bdd_projection_relabel — 0.233 s (17.2%) | export — 0.167 s (12.3%) |
| prioritized_arbiter-n7 | candidate_builder — 0.509 s (63.2%) | seed_monitor_construction — 0.130 s (16.2%) | target_check — 0.087 s (10.8%) |

## Per-target detail

### arbiter_with_cancel-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 5.103 s
- cgroup `memory.peak`: 945.80 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-8` (node cap 33554432): `{"aig_gates_visited":70321,"final_status":"VERIFIED","peak_live_nodes_sample":7667713,"policy_counter_constants":2450,"policy_cross_mode_root_reuses":8000,"policy_dependent_gates_per_mode":985,"policy_full_cache_modes":0,"policy_independent_gates":17821,"policy_independent_roots":160,"policy_mode_builds":50,"policy_specialized_gates":49250,"policy_unspecialized_gates":0,"proof_seconds":2.415630195,"requested_roots":3468,"setup_seconds":0.080878157,"successor_applications":282,"successor_substitutions":50}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 50: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 2.520168 s | 49.4% |
| candidate_builder | 1 | 2.359218 s | 46.2% |
| bdd_projection_relabel | 6555 | 0.862640 s | 16.9% |
| instantiate_templates | 115 | 0.590586 s | 11.6% |
| export | 6195 | 0.464386 s | 9.1% |
| seed_solves | 3 | 0.299021 s | 5.9% |
| policy_construction_skolemization | 1 | 0.250914 s | 4.9% |
| seed_monitor_construction | 3 | 0.216969 s | 4.3% |
| target_monitor_construction | 1 | 0.088472 s | 1.7% |
| projection_metadata | 2645 | 0.050464 s | 1.0% |
| support_extraction | 2313 | 0.044446 s | 0.9% |
| from_aag | 4 | 0.003888 s | 0.1% |
| variable_cube_construction | 9 | 0.000234 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_cancel-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 9.351 s
- cgroup `memory.peak`: 1.21 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-9` (node cap 33554432): `{"aig_gates_visited":95432,"final_status":"VERIFIED","peak_live_nodes_sample":17825793,"policy_counter_constants":3080,"policy_cross_mode_root_reuses":11088,"policy_dependent_gates_per_mode":1216,"policy_full_cache_modes":0,"policy_independent_gates":23310,"policy_independent_roots":198,"policy_mode_builds":56,"policy_specialized_gates":68096,"policy_unspecialized_gates":0,"proof_seconds":6.071766364,"requested_roots":4296,"setup_seconds":0.080207176,"successor_applications":316,"successor_substitutions":56}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 56: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 6.195111 s | 66.2% |
| candidate_builder | 1 | 2.940459 s | 31.4% |
| bdd_projection_relabel | 8385 | 1.172805 s | 12.5% |
| instantiate_templates | 129 | 0.882732 s | 9.4% |
| export | 7687 | 0.678483 s | 7.3% |
| policy_construction_skolemization | 1 | 0.361619 s | 3.9% |
| seed_solves | 3 | 0.299078 s | 3.2% |
| seed_monitor_construction | 3 | 0.215988 s | 2.3% |
| target_monitor_construction | 1 | 0.093476 s | 1.0% |
| projection_metadata | 2967 | 0.055583 s | 0.6% |
| support_extraction | 2593 | 0.051092 s | 0.5% |
| from_aag | 4 | 0.004365 s | 0.0% |
| variable_cube_construction | 9 | 0.000226 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_cancel-n10

- Verdict/exit: REALIZABLE / 0
- Total wall: 24.526 s
- cgroup `memory.peak`: 1.67 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-10` (node cap 33554432): `{"aig_gates_visited":125759,"final_status":"VERIFIED","peak_live_nodes_sample":31195137,"policy_counter_constants":3782,"policy_cross_mode_root_reuses":14880,"policy_dependent_gates_per_mode":1471,"policy_full_cache_modes":0,"policy_independent_gates":29678,"policy_independent_roots":240,"policy_mode_builds":62,"policy_specialized_gates":91202,"policy_unspecialized_gates":0,"proof_seconds":20.496503687,"requested_roots":5212,"setup_seconds":0.093867274,"successor_applications":350,"successor_substitutions":62}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 62: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 20.650768 s | 84.2% |
| candidate_builder | 1 | 3.661408 s | 14.9% |
| bdd_projection_relabel | 10582 | 1.537033 s | 6.3% |
| instantiate_templates | 143 | 1.232549 s | 5.0% |
| export | 9339 | 0.976585 s | 4.0% |
| policy_construction_skolemization | 1 | 0.517749 s | 2.1% |
| seed_solves | 3 | 0.292724 s | 1.2% |
| seed_monitor_construction | 3 | 0.213143 s | 0.9% |
| target_monitor_construction | 1 | 0.094156 s | 0.4% |
| projection_metadata | 3289 | 0.063689 s | 0.3% |
| support_extraction | 2873 | 0.059735 s | 0.2% |
| from_aag | 4 | 0.004571 s | 0.0% |
| variable_cube_construction | 9 | 0.000215 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### load_balancer_unreal2-n6

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 2.190 s
- cgroup `memory.peak`: 1.32 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 67108864): `{"aig_gates_visited":7831,"final_status":"VERIFIED","peak_live_nodes_sample":786433,"policy_counter_constants":12,"policy_cross_mode_root_reuses":8,"policy_dependent_gates_per_mode":41,"policy_full_cache_modes":0,"policy_independent_gates":20,"policy_independent_roots":2,"policy_mode_builds":4,"policy_specialized_gates":164,"policy_unspecialized_gates":0,"proof_seconds":0.19136093,"requested_roots":1189,"setup_seconds":0.166665767,"successor_applications":2176,"successor_substitutions":4}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 12, 1: 46, 2: 70
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 4: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 1.473966 s | 67.3% |
| target_check | 1 | 0.420824 s | 19.2% |
| target_monitor_construction | 1 | 0.084313 s | 3.8% |
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
- Total wall: 11.310 s
- cgroup `memory.peak`: 1.59 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 67108864): `{"aig_gates_visited":15334,"final_status":"VERIFIED","peak_live_nodes_sample":3670017,"policy_counter_constants":12,"policy_cross_mode_root_reuses":8,"policy_dependent_gates_per_mode":41,"policy_full_cache_modes":0,"policy_independent_gates":20,"policy_independent_roots":2,"policy_mode_builds":4,"policy_specialized_gates":164,"policy_unspecialized_gates":0,"proof_seconds":1.108011642,"requested_roots":1494,"setup_seconds":0.186863864,"successor_applications":2752,"successor_substitutions":4}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 12, 1: 53, 2: 96
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 4: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 9.553013 s | 84.5% |
| target_check | 1 | 1.438833 s | 12.7% |
| target_monitor_construction | 1 | 0.091066 s | 0.8% |
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
- Total wall: 9.369 s
- cgroup `memory.peak`: 703.84 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-8` (node cap 33554432): `{"aig_gates_visited":78581,"final_status":"VERIFIED","peak_live_nodes_sample":131073,"policy_counter_constants":650,"policy_cross_mode_root_reuses":11102,"policy_dependent_gates_per_mode":1887,"policy_full_cache_modes":0,"policy_independent_gates":27238,"policy_independent_roots":427,"policy_mode_builds":26,"policy_specialized_gates":49062,"policy_unspecialized_gates":0,"proof_seconds":0.055101832,"requested_roots":1641,"setup_seconds":0.086484538,"successor_applications":344,"successor_substitutions":26}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 8.970926 s | 95.8% |
| bdd_projection_relabel | 10527 | 5.672571 s | 60.5% |
| policy_construction_skolemization | 1 | 3.238628 s | 34.6% |
| substitute_variables | 175 | 0.963691 s | 10.3% |
| export | 316 | 0.779029 s | 8.3% |
| seed_monitor_construction | 3 | 0.198400 s | 2.1% |
| instantiate_templates | 175 | 0.177735 s | 1.9% |
| target_check | 1 | 0.172074 s | 1.8% |
| seed_solves | 3 | 0.083150 s | 0.9% |
| target_monitor_construction | 1 | 0.072884 s | 0.8% |
| projection_metadata | 4025 | 0.068173 s | 0.7% |
| support_extraction | 4124 | 0.061823 s | 0.7% |
| from_aag | 4 | 0.005300 s | 0.1% |
| variable_cube_construction | 14 | 0.000184 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### load_balancer-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 22.946 s
- cgroup `memory.peak`: 735.71 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-9` (node cap 33554432): `{"aig_gates_visited":154106,"final_status":"VERIFIED","peak_live_nodes_sample":196609,"policy_counter_constants":812,"policy_cross_mode_root_reuses":23664,"policy_dependent_gates_per_mode":3198,"policy_full_cache_modes":0,"policy_independent_gates":58760,"policy_independent_roots":816,"policy_mode_builds":29,"policy_specialized_gates":92742,"policy_unspecialized_gates":0,"proof_seconds":0.0934937,"requested_roots":2286,"setup_seconds":0.09889459,"successor_applications":384,"successor_substitutions":29}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 29: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 22.450111 s | 97.8% |
| bdd_projection_relabel | 13357 | 13.920902 s | 60.7% |
| policy_construction_skolemization | 1 | 8.537744 s | 37.2% |
| substitute_variables | 196 | 3.518879 s | 15.3% |
| export | 354 | 1.896918 s | 8.3% |
| target_check | 1 | 0.261119 s | 1.1% |
| instantiate_templates | 196 | 0.237292 s | 1.0% |
| seed_monitor_construction | 3 | 0.202120 s | 0.9% |
| support_extraction | 4609 | 0.090360 s | 0.4% |
| seed_solves | 3 | 0.083265 s | 0.4% |
| projection_metadata | 4508 | 0.078584 s | 0.3% |
| target_monitor_construction | 1 | 0.075907 s | 0.3% |
| from_aag | 4 | 0.005549 s | 0.0% |
| variable_cube_construction | 14 | 0.000192 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### amba_decomposed_lock-n15

- Verdict/exit: REALIZABLE / 0
- Total wall: 35.639 s
- cgroup `memory.peak`: 1.84 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-15` (node cap 67108864): `{"aig_gates_visited":397545,"final_status":"VERIFIED","peak_live_nodes_sample":17170433,"policy_counter_constants":272,"policy_cross_mode_root_reuses":833,"policy_dependent_gates_per_mode":171,"policy_full_cache_modes":0,"policy_independent_gates":393593,"policy_independent_roots":49,"policy_mode_builds":17,"policy_specialized_gates":2907,"policy_unspecialized_gates":0,"proof_seconds":6.799779807,"requested_roots":523,"setup_seconds":0.165490325,"successor_applications":153,"successor_substitutions":17}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 17: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 28.370435 s | 79.6% |
| bdd_projection_relabel | 2993 | 21.269060 s | 59.7% |
| policy_construction_skolemization | 1 | 11.444185 s | 32.1% |
| target_check | 1 | 7.046211 s | 19.8% |
| substitute_variables | 65 | 4.529000 s | 12.7% |
| export | 146 | 1.503812 s | 4.2% |
| seed_monitor_construction | 3 | 0.195630 s | 0.5% |
| target_monitor_construction | 1 | 0.105555 s | 0.3% |
| seed_solves | 3 | 0.066492 s | 0.2% |
| instantiate_templates | 65 | 0.032900 s | 0.1% |
| support_extraction | 1454 | 0.017668 s | 0.0% |
| projection_metadata | 1365 | 0.016169 s | 0.0% |
| from_aag | 4 | 0.002165 s | 0.0% |
| variable_cube_construction | 10 | 0.000065 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_buffer-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 14.783 s
- cgroup `memory.peak`: 715.12 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-8` (node cap 33554432): `{"aig_gates_visited":13346,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":90,"policy_cross_mode_root_reuses":2720,"policy_dependent_gates_per_mode":883,"policy_full_cache_modes":0,"policy_independent_gates":4124,"policy_independent_roots":272,"policy_mode_builds":10,"policy_specialized_gates":8830,"policy_unspecialized_gates":0,"proof_seconds":0.005634015,"requested_roots":612,"setup_seconds":0.091558595,"successor_applications":50,"successor_substitutions":10}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 10: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 14.423639 s | 97.6% |
| policy_construction_skolemization | 1 | 9.415504 s | 63.7% |
| bdd_projection_relabel | 782 | 5.222790 s | 35.3% |
| export | 80 | 1.396001 s | 9.4% |
| seed_monitor_construction | 3 | 0.194755 s | 1.3% |
| substitute_variables | 19 | 0.155274 s | 1.1% |
| target_check | 1 | 0.136459 s | 0.9% |
| target_monitor_construction | 1 | 0.074250 s | 0.5% |
| seed_solves | 3 | 0.070853 s | 0.5% |
| support_extraction | 462 | 0.018172 s | 0.1% |
| projection_metadata | 399 | 0.003918 s | 0.0% |
| instantiate_templates | 19 | 0.003316 s | 0.0% |
| from_aag | 4 | 0.001242 s | 0.0% |
| variable_cube_construction | 10 | 0.000119 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_buffer-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 117.345 s
- cgroup `memory.peak`: 1.81 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-9` (node cap 33554432): `{"aig_gates_visited":26959,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":110,"policy_cross_mode_root_reuses":5830,"policy_dependent_gates_per_mode":1669,"policy_full_cache_modes":0,"policy_independent_gates":8145,"policy_independent_roots":530,"policy_mode_builds":11,"policy_specialized_gates":18359,"policy_unspecialized_gates":0,"proof_seconds":0.009590685,"requested_roots":938,"setup_seconds":0.122818104,"successor_applications":55,"successor_substitutions":11}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 11: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 116.831326 s | 99.6% |
| policy_construction_skolemization | 1 | 99.162864 s | 84.5% |
| bdd_projection_relabel | 886 | 20.632079 s | 17.6% |
| export | 89 | 4.923451 s | 4.2% |
| substitute_variables | 21 | 0.931741 s | 0.8% |
| target_check | 1 | 0.273362 s | 0.2% |
| seed_monitor_construction | 3 | 0.195273 s | 0.2% |
| target_monitor_construction | 1 | 0.073612 s | 0.1% |
| support_extraction | 511 | 0.065435 s | 0.1% |
| seed_solves | 3 | 0.065174 s | 0.1% |
| instantiate_templates | 21 | 0.004017 s | 0.0% |
| projection_metadata | 441 | 0.003985 s | 0.0% |
| from_aag | 4 | 0.001418 s | 0.0% |
| variable_cube_construction | 10 | 0.000131 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_on_inpchange-n6

- Verdict/exit: REALIZABLE / 0
- Total wall: 17.272 s
- cgroup `memory.peak`: 1.38 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 33554432): `{"aig_gates_visited":59768,"final_status":"VERIFIED","peak_live_nodes_sample":22675457,"policy_counter_constants":650,"policy_cross_mode_root_reuses":2496,"policy_dependent_gates_per_mode":403,"policy_full_cache_modes":0,"policy_independent_gates":37435,"policy_independent_roots":96,"policy_mode_builds":26,"policy_specialized_gates":10478,"policy_unspecialized_gates":0,"proof_seconds":12.044571789,"requested_roots":1206,"setup_seconds":0.085498603,"successor_applications":154,"successor_substitutions":26}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 12.183603 s | 70.5% |
| candidate_builder | 1 | 4.874894 s | 28.2% |
| bdd_projection_relabel | 2772 | 2.618081 s | 15.2% |
| instantiate_templates | 63 | 1.415088 s | 8.2% |
| export | 2611 | 0.878434 s | 5.1% |
| seed_solves | 3 | 0.581787 s | 3.4% |
| policy_construction_skolemization | 1 | 0.452369 s | 2.6% |
| seed_monitor_construction | 3 | 0.223193 s | 1.3% |
| target_monitor_construction | 1 | 0.086046 s | 0.5% |
| projection_metadata | 1449 | 0.038853 s | 0.2% |
| support_extraction | 1275 | 0.037021 s | 0.2% |
| from_aag | 4 | 0.022052 s | 0.1% |
| variable_cube_construction | 9 | 0.000368 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_on_inpchange-n7

- Verdict/exit: UNKNOWN / 2
- Total wall: 120.095 s
- cgroup `memory.peak`: 1.76 GiB
- Censored stages: target_check ≥ 112.957 s (absolute_deadline_exhausted, sigterm)
- Checker `--stats`: no payload captured
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 30: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 6.938917 s | 5.8% |
| bdd_projection_relabel | 3650 | 3.905913 s | 3.3% |
| instantiate_templates | 73 | 2.432832 s | 2.0% |
| export | 3435 | 1.406242 s | 1.2% |
| seed_solves | 3 | 0.769937 s | 0.6% |
| policy_construction_skolemization | 1 | 0.720126 s | 0.6% |
| seed_monitor_construction | 3 | 0.220549 s | 0.2% |
| target_monitor_construction | 1 | 0.089974 s | 0.1% |
| projection_metadata | 1679 | 0.046371 s | 0.0% |
| support_extraction | 1475 | 0.043652 s | 0.0% |
| from_aag | 4 | 0.022128 s | 0.0% |
| variable_cube_construction | 9 | 0.000403 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_check | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### round_robin_arbiter_unreal2-n5

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 2.009 s
- cgroup `memory.peak`: 1.35 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-5` (node cap 67108864): `{"aig_gates_visited":141862,"final_status":"VERIFIED","peak_live_nodes_sample":196609,"policy_counter_constants":240,"policy_cross_mode_root_reuses":270,"policy_dependent_gates_per_mode":132039,"policy_full_cache_modes":16,"policy_independent_gates":326,"policy_independent_roots":270,"policy_mode_builds":16,"policy_specialized_gates":0,"policy_unspecialized_gates":132039,"proof_seconds":0.475181255,"requested_roots":2754,"setup_seconds":0.172166411,"successor_applications":4736,"successor_substitutions":16}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 60, 2: 40
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 16: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 0.908926 s | 45.2% |
| target_solve | 1 | 0.810479 s | 40.3% |
| target_monitor_construction | 1 | 0.078818 s | 3.9% |
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
- Total wall: 7.221 s
- cgroup `memory.peak`: 1.55 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 67108864): `{"aig_gates_visited":734342,"final_status":"VERIFIED","peak_live_nodes_sample":1310721,"policy_counter_constants":342,"policy_cross_mode_root_reuses":1043,"policy_dependent_gates_per_mode":714747,"policy_full_cache_modes":19,"policy_independent_gates":1112,"policy_independent_roots":1043,"policy_mode_builds":19,"policy_specialized_gates":0,"policy_unspecialized_gates":714747,"proof_seconds":2.528472433,"requested_roots":5059,"setup_seconds":0.194531402,"successor_applications":7744,"successor_substitutions":19}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 72, 2: 60
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 19: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 3.481947 s | 48.2% |
| target_check | 1 | 3.442033 s | 47.7% |
| target_monitor_construction | 1 | 0.085348 s | 1.2% |
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
- Total wall: 31.251 s
- cgroup `memory.peak`: 2.49 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 67108864): `{"aig_gates_visited":3646651,"final_status":"VERIFIED","peak_live_nodes_sample":6553601,"policy_counter_constants":462,"policy_cross_mode_root_reuses":4120,"policy_dependent_gates_per_mode":3609146,"policy_full_cache_modes":22,"policy_independent_gates":4202,"policy_independent_roots":4120,"policy_mode_builds":22,"policy_specialized_gates":0,"policy_unspecialized_gates":3609146,"proof_seconds":13.746652848,"requested_roots":10210,"setup_seconds":0.240765443,"successor_applications":11832,"successor_substitutions":22}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 84, 2: 84
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 22: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 15.842860 s | 50.7% |
| target_solve | 1 | 15.097480 s | 48.3% |
| target_monitor_construction | 1 | 0.094309 s | 0.3% |
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
- Total wall: 120.057 s
- cgroup `memory.peak`: 2.12 GiB
- Censored stages: canonicalize ≥ 119.647 s (absolute_deadline_exhausted)
- Checker `--stats`: no payload captured
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: none

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 119.856997 s | 99.8% |
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
- Total wall: 1.358 s
- cgroup `memory.peak`: 687.90 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 33554432): `{"aig_gates_visited":19687,"final_status":"VERIFIED","peak_live_nodes_sample":131073,"policy_counter_constants":650,"policy_cross_mode_root_reuses":2340,"policy_dependent_gates_per_mode":403,"policy_full_cache_modes":0,"policy_independent_gates":7632,"policy_independent_roots":90,"policy_mode_builds":26,"policy_specialized_gates":10478,"policy_unspecialized_gates":0,"proof_seconds":0.053557947,"requested_roots":1140,"setup_seconds":0.080781841,"successor_applications":154,"successor_substitutions":26}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 1.004436 s | 74.0% |
| bdd_projection_relabel | 2646 | 0.233068 s | 17.2% |
| export | 2611 | 0.167346 s | 12.3% |
| seed_monitor_construction | 2 | 0.141846 s | 10.4% |
| target_check | 1 | 0.139700 s | 10.3% |
| instantiate_templates | 63 | 0.128295 s | 9.4% |
| seed_solves | 2 | 0.091270 s | 6.7% |
| policy_construction_skolemization | 1 | 0.090618 s | 6.7% |
| target_monitor_construction | 1 | 0.073135 s | 5.4% |
| projection_metadata | 1260 | 0.023323 s | 1.7% |
| support_extraction | 1144 | 0.018091 s | 1.3% |
| from_aag | 3 | 0.002795 s | 0.2% |
| variable_cube_construction | 9 | 0.000154 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### prioritized_arbiter-n7

- Verdict/exit: REALIZABLE / 0
- Total wall: 0.806 s
- cgroup `memory.peak`: 681.34 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 33554432): `{"aig_gates_visited":19751,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":272,"policy_cross_mode_root_reuses":2499,"policy_dependent_gates_per_mode":982,"policy_full_cache_modes":0,"policy_independent_gates":2776,"policy_independent_roots":147,"policy_mode_builds":17,"policy_specialized_gates":16694,"policy_unspecialized_gates":0,"proof_seconds":0.005968567,"requested_roots":704,"setup_seconds":0.077483393,"successor_applications":116,"successor_substitutions":17}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 17: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 0.509458 s | 63.2% |
| seed_monitor_construction | 2 | 0.130192 s | 16.2% |
| target_check | 1 | 0.087343 s | 10.8% |
| target_monitor_construction | 1 | 0.068659 s | 8.5% |
| seed_solves | 2 | 0.044508 s | 5.5% |
| bdd_projection_relabel | 1323 | 0.028498 s | 3.5% |
| policy_construction_skolemization | 1 | 0.017679 s | 2.2% |
| instantiate_templates | 40 | 0.010403 s | 1.3% |
| export | 135 | 0.007278 s | 0.9% |
| support_extraction | 767 | 0.006644 s | 0.8% |
| projection_metadata | 640 | 0.006535 s | 0.8% |
| substitute_variables | 40 | 0.003353 s | 0.4% |
| from_aag | 3 | 0.000789 s | 0.1% |
| variable_cube_construction | 8 | 0.000099 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |
