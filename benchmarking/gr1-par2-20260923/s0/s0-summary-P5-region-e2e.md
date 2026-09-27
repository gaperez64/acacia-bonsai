# S0 diagnostic summary

Source: `/home/gperez/GIT-repos/acacia-gr1-par2-timing/benchmarking/gr1-par2-20260923/s0/raw-p5-region`

## Notes

- End-to-end: tlsf-tools build-P5-9212e2b + generalizer ba98fb39 + adapter 0043e97b, --real-check region, timing worktree, serial, 120 s, 8 GiB

## Targets

| Target | Verdict | Exit | Total wall | cgroup memory.peak | Censored stage |
|---|---:|---:|---:|---:|---|
| arbiter_with_cancel-n8 | REALIZABLE | 0 | 21.228 s | 1.17 GiB | none |
| arbiter_with_cancel-n9 | UNKNOWN | 2 | 120.074 s | 1.83 GiB | target_check ≥ 0.000 s |
| arbiter_with_cancel-n10 | UNKNOWN | 2 | 120.166 s | 1.79 GiB | target_check ≥ 116.740 s |
| load_balancer_unreal2-n6 | UNREALIZABLE | 1 | 2.189 s | 1.32 GiB | none |
| load_balancer_unreal2-n7 | UNREALIZABLE | 1 | 11.105 s | 1.59 GiB | none |
| load_balancer-n8 | REALIZABLE | 0 | 9.509 s | 929.59 MiB | none |
| load_balancer-n9 | REALIZABLE | 0 | 23.830 s | 1.32 GiB | none |
| amba_decomposed_lock-n15 | REALIZABLE | 0 | 23.617 s | 1.55 GiB | none |
| arbiter_with_buffer-n8 | REALIZABLE | 0 | 5.366 s | 720.16 MiB | none |
| arbiter_with_buffer-n9 | REALIZABLE | 0 | 18.585 s | 1.23 GiB | none |
| arbiter_on_inpchange-n6 | UNKNOWN | 2 | 100.141 s | 1.62 GiB | none |
| arbiter_on_inpchange-n7 | UNKNOWN | 2 | 99.020 s | 1.64 GiB | none |
| round_robin_arbiter_unreal2-n5 | UNREALIZABLE | 1 | 2.014 s | 1.35 GiB | none |
| round_robin_arbiter_unreal2-n6 | UNREALIZABLE | 1 | 7.216 s | 1.55 GiB | none |
| round_robin_arbiter_unreal2-n7 | UNREALIZABLE | 1 | 31.134 s | 2.49 GiB | none |
| collector_v1-n11 | UNKNOWN | 2 | 120.032 s | 2.13 GiB | candidate_builder ≥ 119.818 s; generalize_gr1 ≥ 0.000 s |
| arbiter-n6 | REALIZABLE | 0 | 1.381 s | 692.79 MiB | none |
| prioritized_arbiter-n7 | REALIZABLE | 0 | 0.789 s | 681.03 MiB | none |

## Cross-target phase dominance

| Target | Dominant phase | Second | Third |
|---|---|---|---|
| arbiter_with_cancel-n8 | target_check — 18.948 s (89.3%) | candidate_builder — 2.064 s (9.7%) | bdd_projection_relabel — 0.851 s (4.0%) |
| arbiter_with_cancel-n9 | n/a | n/a | n/a |
| arbiter_with_cancel-n10 | target_check — ≥ 116.740 s (≥ 97.1%) | candidate_builder — 3.219 s (2.7%) | bdd_projection_relabel — 1.598 s (1.3%) |
| load_balancer_unreal2-n6 | target_solve — 1.466 s (67.0%) | target_check — 0.420 s (19.2%) | target_monitor_construction — 0.084 s (3.8%) |
| load_balancer_unreal2-n7 | target_solve — 9.452 s (85.1%) | target_check — 1.328 s (12.0%) | target_monitor_construction — 0.094 s (0.8%) |
| load_balancer-n8 | candidate_builder — 5.635 s (59.3%) | target_check — 3.648 s (38.4%) | bdd_projection_relabel — 2.941 s (30.9%) |
| load_balancer-n9 | candidate_builder — 13.514 s (56.7%) | target_check — 10.074 s (42.3%) | bdd_projection_relabel — 6.990 s (29.3%) |
| amba_decomposed_lock-n15 | candidate_builder — 16.887 s (71.5%) | bdd_projection_relabel — 10.838 s (45.9%) | target_check — 6.507 s (27.6%) |
| arbiter_with_buffer-n8 | candidate_builder — 4.817 s (89.8%) | bdd_projection_relabel — 2.504 s (46.7%) | export — 1.337 s (24.9%) |
| arbiter_with_buffer-n9 | candidate_builder — 17.118 s (92.1%) | bdd_projection_relabel — 9.769 s (52.6%) | export — 4.807 s (25.9%) |
| arbiter_on_inpchange-n6 | target_check — 95.144 s (95.0%) | candidate_builder — 4.781 s (4.8%) | bdd_projection_relabel — 2.955 s (3.0%) |
| arbiter_on_inpchange-n7 | target_check — 92.624 s (93.5%) | candidate_builder — 6.179 s (6.2%) | bdd_projection_relabel — 3.979 s (4.0%) |
| round_robin_arbiter_unreal2-n5 | target_check — 0.913 s (45.3%) | target_solve — 0.809 s (40.2%) | target_monitor_construction — 0.079 s (3.9%) |
| round_robin_arbiter_unreal2-n6 | target_check — 3.487 s (48.3%) | target_solve — 3.425 s (47.5%) | target_monitor_construction — 0.085 s (1.2%) |
| round_robin_arbiter_unreal2-n7 | target_check — 15.756 s (50.6%) | target_solve — 15.062 s (48.4%) | target_monitor_construction — 0.095 s (0.3%) |
| collector_v1-n11 | candidate_builder — ≥ 239.639 s (≥ 199.6%) | n/a | n/a |
| arbiter-n6 | candidate_builder — 0.891 s (64.5%) | target_check — 0.275 s (19.9%) | bdd_projection_relabel — 0.228 s (16.5%) |
| prioritized_arbiter-n7 | candidate_builder — 0.491 s (62.2%) | seed_monitor_construction — 0.131 s (16.5%) | target_check — 0.082 s (10.4%) |

## Per-target detail

### arbiter_with_cancel-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 21.228 s
- cgroup `memory.peak`: 1.17 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-8` (node cap 33554432): `{"aig_gates_visited":3250,"cache_cap":33554432,"final_status":"REGION_VERIFIED","node_cap":33554432,"peak_live_nodes_sample":16711681,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":18.831280694,"region_certificate_roots":278,"region_game_roots":180,"region_layer_seconds":1.350809474,"region_layers":166,"region_mode_seconds":18.830767076,"region_modes":50,"requested_roots":458,"setup_seconds":0.080720281,"successor_applications":166,"successor_substitutions":50}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 50: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 18.947729 s | 89.3% |
| candidate_builder | 1 | 2.063650 s | 9.7% |
| bdd_projection_relabel | 6555 | 0.850882 s | 4.0% |
| instantiate_templates | 115 | 0.579398 s | 2.7% |
| seed_solves | 3 | 0.288900 s | 1.4% |
| export | 3212 | 0.234297 s | 1.1% |
| seed_monitor_construction | 3 | 0.208525 s | 1.0% |
| target_monitor_construction | 1 | 0.087244 s | 0.4% |
| projection_metadata | 2645 | 0.048264 s | 0.2% |
| support_extraction | 2313 | 0.043418 s | 0.2% |
| from_aag | 4 | 0.004108 s | 0.0% |
| variable_cube_construction | 9 | 0.000217 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_cancel-n9

- Verdict/exit: UNKNOWN / 2
- Total wall: 120.074 s
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
- Total wall: 120.166 s
- cgroup `memory.peak`: 1.79 GiB
- Censored stages: target_check ≥ 116.740 s (absolute_deadline_exhausted, sigterm)
- Checker `--stats`: no payload captured
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 62: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 3.218594 s | 2.7% |
| bdd_projection_relabel | 10582 | 1.598228 s | 1.3% |
| instantiate_templates | 143 | 1.277774 s | 1.1% |
| export | 4812 | 0.492973 s | 0.4% |
| seed_solves | 3 | 0.292220 s | 0.2% |
| seed_monitor_construction | 3 | 0.217590 s | 0.2% |
| target_monitor_construction | 1 | 0.097033 s | 0.1% |
| projection_metadata | 3289 | 0.066399 s | 0.1% |
| support_extraction | 2873 | 0.064058 s | 0.1% |
| from_aag | 4 | 0.004795 s | 0.0% |
| variable_cube_construction | 9 | 0.000227 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_check | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### load_balancer_unreal2-n6

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 2.189 s
- cgroup `memory.peak`: 1.32 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 67108864): `{"aig_gates_visited":7831,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":786433,"policy_counter_constants":12,"policy_cross_mode_root_reuses":8,"policy_dependent_gates_per_mode":41,"policy_full_cache_modes":0,"policy_independent_gates":20,"policy_independent_roots":2,"policy_mode_builds":4,"policy_specialized_gates":164,"policy_unspecialized_gates":0,"proof_seconds":0.192255045,"requested_roots":1189,"setup_seconds":0.166920468,"successor_applications":2176,"successor_substitutions":4}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 12, 1: 46, 2: 70
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 4: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 1.465972 s | 67.0% |
| target_check | 1 | 0.420002 s | 19.2% |
| target_monitor_construction | 1 | 0.084100 s | 3.8% |
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
- Total wall: 11.105 s
- cgroup `memory.peak`: 1.59 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 67108864): `{"aig_gates_visited":15334,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":3670017,"policy_counter_constants":12,"policy_cross_mode_root_reuses":8,"policy_dependent_gates_per_mode":41,"policy_full_cache_modes":0,"policy_independent_gates":20,"policy_independent_roots":2,"policy_mode_builds":4,"policy_specialized_gates":164,"policy_unspecialized_gates":0,"proof_seconds":0.997584142,"requested_roots":1494,"setup_seconds":0.185488583,"successor_applications":2752,"successor_substitutions":4}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 12, 1: 53, 2: 96
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 4: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 9.452376 s | 85.1% |
| target_check | 1 | 1.328100 s | 12.0% |
| target_monitor_construction | 1 | 0.094331 s | 0.8% |
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
- Total wall: 9.509 s
- cgroup `memory.peak`: 929.59 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-8` (node cap 33554432): `{"aig_gates_visited":2281,"cache_cap":33554432,"final_status":"REGION_VERIFIED","node_cap":33554432,"peak_live_nodes_sample":7471105,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":3.518901345,"region_certificate_roots":258,"region_game_roots":98,"region_layer_seconds":0.236458426,"region_layers":206,"region_mode_seconds":3.518624458,"region_modes":26,"requested_roots":356,"setup_seconds":0.087337036,"successor_applications":344,"successor_substitutions":26}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 5.634515 s | 59.3% |
| target_check | 1 | 3.648119 s | 38.4% |
| bdd_projection_relabel | 10477 | 2.940732 s | 30.9% |
| substitute_variables | 175 | 0.956495 s | 10.1% |
| export | 283 | 0.727856 s | 7.7% |
| seed_monitor_construction | 3 | 0.199938 s | 2.1% |
| instantiate_templates | 175 | 0.175532 s | 1.8% |
| seed_solves | 3 | 0.083260 s | 0.9% |
| target_monitor_construction | 1 | 0.072864 s | 0.8% |
| projection_metadata | 4025 | 0.068475 s | 0.7% |
| support_extraction | 4074 | 0.053585 s | 0.6% |
| from_aag | 4 | 0.005424 s | 0.1% |
| variable_cube_construction | 13 | 0.000166 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### load_balancer-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 23.830 s
- cgroup `memory.peak`: 1.32 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-9` (node cap 33554432): `{"aig_gates_visited":2604,"cache_cap":33554432,"final_status":"REGION_VERIFIED","node_cap":33554432,"peak_live_nodes_sample":18612225,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":9.880236496,"region_certificate_roots":289,"region_game_roots":108,"region_layer_seconds":0.608448036,"region_layers":230,"region_mode_seconds":9.879920555,"region_modes":29,"requested_roots":397,"setup_seconds":0.098555058,"successor_applications":384,"successor_substitutions":29}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 29: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 13.513623 s | 56.7% |
| target_check | 1 | 10.073703 s | 42.3% |
| bdd_projection_relabel | 13301 | 6.989780 s | 29.3% |
| substitute_variables | 196 | 3.421434 s | 14.4% |
| export | 317 | 1.707471 s | 7.2% |
| instantiate_templates | 196 | 0.223510 s | 0.9% |
| seed_monitor_construction | 3 | 0.199520 s | 0.8% |
| seed_solves | 3 | 0.082917 s | 0.3% |
| target_monitor_construction | 1 | 0.075277 s | 0.3% |
| projection_metadata | 4508 | 0.074543 s | 0.3% |
| support_extraction | 4553 | 0.069081 s | 0.3% |
| from_aag | 4 | 0.005392 s | 0.0% |
| variable_cube_construction | 13 | 0.000164 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### amba_decomposed_lock-n15

- Verdict/exit: REALIZABLE / 0
- Total wall: 23.617 s
- cgroup `memory.peak`: 1.55 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-15` (node cap 67108864): `{"aig_gates_visited":1045,"cache_cap":67108864,"final_status":"REGION_VERIFIED","node_cap":67108864,"peak_live_nodes_sample":8192001,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":6.300877815,"region_certificate_roots":113,"region_game_roots":72,"region_layer_seconds":0.827074239,"region_layers":85,"region_mode_seconds":6.300791467,"region_modes":17,"requested_roots":185,"setup_seconds":0.164487868,"successor_applications":153,"successor_substitutions":17}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 17: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 16.887122 s | 71.5% |
| bdd_projection_relabel | 2961 | 10.837959 s | 45.9% |
| target_check | 1 | 6.507116 s | 27.6% |
| substitute_variables | 65 | 4.494885 s | 19.0% |
| export | 129 | 0.706805 s | 3.0% |
| seed_monitor_construction | 3 | 0.196784 s | 0.8% |
| target_monitor_construction | 1 | 0.104728 s | 0.4% |
| seed_solves | 3 | 0.067489 s | 0.3% |
| instantiate_templates | 65 | 0.032013 s | 0.1% |
| projection_metadata | 1365 | 0.015838 s | 0.1% |
| support_extraction | 1422 | 0.014317 s | 0.1% |
| from_aag | 4 | 0.002109 s | 0.0% |
| variable_cube_construction | 9 | 0.000059 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_buffer-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 5.366 s
- cgroup `memory.peak`: 720.16 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-8` (node cap 33554432): `{"aig_gates_visited":392,"cache_cap":33554432,"final_status":"REGION_VERIFIED","node_cap":33554432,"peak_live_nodes_sample":589825,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":0.195703041,"region_certificate_roots":46,"region_game_roots":44,"region_layer_seconds":0.030491654,"region_layers":30,"region_mode_seconds":0.195669455,"region_modes":10,"requested_roots":90,"setup_seconds":0.089932407,"successor_applications":30,"successor_substitutions":10}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 10: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 4.817158 s | 89.8% |
| bdd_projection_relabel | 764 | 2.503898 s | 46.7% |
| export | 55 | 1.337114 s | 24.9% |
| target_check | 1 | 0.326607 s | 6.1% |
| seed_monitor_construction | 3 | 0.194247 s | 3.6% |
| substitute_variables | 19 | 0.143105 s | 2.7% |
| target_monitor_construction | 1 | 0.071277 s | 1.3% |
| seed_solves | 3 | 0.066976 s | 1.2% |
| support_extraction | 444 | 0.009027 s | 0.2% |
| projection_metadata | 399 | 0.003681 s | 0.1% |
| instantiate_templates | 19 | 0.003205 s | 0.1% |
| from_aag | 4 | 0.001145 s | 0.0% |
| variable_cube_construction | 9 | 0.000074 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_buffer-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 18.585 s
- cgroup `memory.peak`: 1.23 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-9` (node cap 33554432): `{"aig_gates_visited":455,"cache_cap":33554432,"final_status":"REGION_VERIFIED","node_cap":33554432,"peak_live_nodes_sample":2162689,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":0.954798571,"region_certificate_roots":51,"region_game_roots":49,"region_layer_seconds":0.116787511,"region_layers":33,"region_mode_seconds":0.95475151,"region_modes":11,"requested_roots":100,"setup_seconds":0.124802557,"successor_applications":33,"successor_substitutions":11}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 11: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 17.117989 s | 92.1% |
| bdd_projection_relabel | 866 | 9.768901 s | 52.6% |
| export | 61 | 4.806691 s | 25.9% |
| target_check | 1 | 1.221470 s | 6.6% |
| substitute_variables | 21 | 0.936372 s | 5.0% |
| seed_monitor_construction | 3 | 0.193000 s | 1.0% |
| target_monitor_construction | 1 | 0.072149 s | 0.4% |
| seed_solves | 3 | 0.065418 s | 0.4% |
| support_extraction | 491 | 0.031529 s | 0.2% |
| instantiate_templates | 21 | 0.004257 s | 0.0% |
| projection_metadata | 441 | 0.004051 s | 0.0% |
| from_aag | 4 | 0.001381 s | 0.0% |
| variable_cube_construction | 9 | 0.000143 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_on_inpchange-n6

- Verdict/exit: UNKNOWN / 2
- Total wall: 100.141 s
- cgroup `memory.peak`: 1.62 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 33554432): `{"aig_gates_visited":11855,"cache_cap":33554432,"final_status":"UNKNOWN","node_cap":33554432,"peak_live_nodes_sample":33554945,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":95.010051028,"region_certificate_roots":150,"region_game_roots":154,"region_layer_seconds":0.0,"region_layers":0,"region_mode_seconds":95.008723776,"region_modes":1,"requested_roots":304,"setup_seconds":0.083550457,"successor_applications":3,"successor_substitutions":1}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 95.144249 s | 95.0% |
| candidate_builder | 1 | 4.780907 s | 4.8% |
| bdd_projection_relabel | 2772 | 2.955414 s | 3.0% |
| instantiate_templates | 63 | 1.601033 s | 1.6% |
| seed_solves | 3 | 0.575036 s | 0.6% |
| export | 1368 | 0.478690 s | 0.5% |
| seed_monitor_construction | 3 | 0.221444 s | 0.2% |
| target_monitor_construction | 1 | 0.084832 s | 0.1% |
| projection_metadata | 1449 | 0.050327 s | 0.1% |
| support_extraction | 1275 | 0.039897 s | 0.0% |
| from_aag | 4 | 0.022113 s | 0.0% |
| variable_cube_construction | 9 | 0.000388 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_on_inpchange-n7

- Verdict/exit: UNKNOWN / 2
- Total wall: 99.020 s
- cgroup `memory.peak`: 1.64 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 33554432): `{"aig_gates_visited":15926,"cache_cap":33554432,"final_status":"UNKNOWN","node_cap":33554432,"peak_live_nodes_sample":33554945,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":92.484085909,"region_certificate_roots":174,"region_game_roots":179,"region_layer_seconds":0.0,"region_layers":0,"region_mode_seconds":92.482282701,"region_modes":1,"requested_roots":353,"setup_seconds":0.086688217,"successor_applications":3,"successor_substitutions":1}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 30: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 92.624356 s | 93.5% |
| candidate_builder | 1 | 6.178934 s | 6.2% |
| bdd_projection_relabel | 3650 | 3.979124 s | 4.0% |
| instantiate_templates | 73 | 2.475532 s | 2.5% |
| export | 1790 | 0.733741 s | 0.7% |
| seed_solves | 3 | 0.644674 s | 0.7% |
| seed_monitor_construction | 3 | 0.223034 s | 0.2% |
| target_monitor_construction | 1 | 0.088776 s | 0.1% |
| projection_metadata | 1679 | 0.047107 s | 0.0% |
| support_extraction | 1475 | 0.044010 s | 0.0% |
| from_aag | 4 | 0.022366 s | 0.0% |
| variable_cube_construction | 9 | 0.000395 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### round_robin_arbiter_unreal2-n5

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 2.014 s
- cgroup `memory.peak`: 1.35 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-5` (node cap 67108864): `{"aig_gates_visited":141862,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":196609,"policy_counter_constants":240,"policy_cross_mode_root_reuses":270,"policy_dependent_gates_per_mode":132039,"policy_full_cache_modes":16,"policy_independent_gates":326,"policy_independent_roots":270,"policy_mode_builds":16,"policy_specialized_gates":0,"policy_unspecialized_gates":132039,"proof_seconds":0.473424473,"requested_roots":2754,"setup_seconds":0.171352488,"successor_applications":4736,"successor_substitutions":16}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 60, 2: 40
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 16: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 0.913047 s | 45.3% |
| target_solve | 1 | 0.809144 s | 40.2% |
| target_monitor_construction | 1 | 0.078826 s | 3.9% |
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
- Total wall: 7.216 s
- cgroup `memory.peak`: 1.55 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 67108864): `{"aig_gates_visited":734342,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":1310721,"policy_counter_constants":342,"policy_cross_mode_root_reuses":1043,"policy_dependent_gates_per_mode":714747,"policy_full_cache_modes":19,"policy_independent_gates":1112,"policy_independent_roots":1043,"policy_mode_builds":19,"policy_specialized_gates":0,"policy_unspecialized_gates":714747,"proof_seconds":2.507500189,"requested_roots":5059,"setup_seconds":0.191534361,"successor_applications":7744,"successor_substitutions":19}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 72, 2: 60
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 19: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 3.486980 s | 48.3% |
| target_solve | 1 | 3.424961 s | 47.5% |
| target_monitor_construction | 1 | 0.084777 s | 1.2% |
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
- Total wall: 31.134 s
- cgroup `memory.peak`: 2.49 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 67108864): `{"aig_gates_visited":3646651,"cache_cap":67108864,"final_status":"VERIFIED","node_cap":67108864,"peak_live_nodes_sample":6553601,"policy_counter_constants":462,"policy_cross_mode_root_reuses":4120,"policy_dependent_gates_per_mode":3609146,"policy_full_cache_modes":22,"policy_independent_gates":4202,"policy_independent_roots":4120,"policy_mode_builds":22,"policy_specialized_gates":0,"policy_unspecialized_gates":3609146,"proof_seconds":13.701788512,"requested_roots":10210,"setup_seconds":0.231095867,"successor_applications":11832,"successor_substitutions":22}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 84, 2: 84
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 22: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 15.756212 s | 50.6% |
| target_solve | 1 | 15.062327 s | 48.4% |
| target_monitor_construction | 1 | 0.094545 s | 0.3% |
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
- Total wall: 120.032 s
- cgroup `memory.peak`: 2.13 GiB
- Censored stages: candidate_builder ≥ 119.818 s (sigterm); generalize_gr1 ≥ 0.000 s (absolute_deadline_exhausted)
- Checker `--stats`: no payload captured
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: none

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 119.820828 s | 99.8% |
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
- Total wall: 1.381 s
- cgroup `memory.peak`: 692.79 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 33554432): `{"aig_gates_visited":1577,"cache_cap":33554432,"final_status":"REGION_VERIFIED","node_cap":33554432,"peak_live_nodes_sample":327681,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":0.190426443,"region_certificate_roots":150,"region_game_roots":94,"region_layer_seconds":0.019494944,"region_layers":90,"region_mode_seconds":0.190193963,"region_modes":26,"requested_roots":244,"setup_seconds":0.079148184,"successor_applications":90,"successor_substitutions":26}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 0.891301 s | 64.5% |
| target_check | 1 | 0.275062 s | 19.9% |
| bdd_projection_relabel | 2646 | 0.228427 s | 16.5% |
| seed_monitor_construction | 2 | 0.136591 s | 9.9% |
| instantiate_templates | 63 | 0.126417 s | 9.2% |
| seed_solves | 2 | 0.091093 s | 6.6% |
| export | 1368 | 0.083175 s | 6.0% |
| target_monitor_construction | 1 | 0.073170 s | 5.3% |
| projection_metadata | 1260 | 0.023146 s | 1.7% |
| support_extraction | 1144 | 0.017286 s | 1.3% |
| from_aag | 3 | 0.002799 s | 0.2% |
| variable_cube_construction | 9 | 0.000163 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### prioritized_arbiter-n7

- Verdict/exit: REALIZABLE / 0
- Total wall: 0.789 s
- cgroup `memory.peak`: 681.03 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 33554432): `{"aig_gates_visited":281,"cache_cap":33554432,"final_status":"REGION_VERIFIED","node_cap":33554432,"peak_live_nodes_sample":1,"policy_counter_constants":0,"policy_cross_mode_root_reuses":0,"policy_dependent_gates_per_mode":0,"policy_full_cache_modes":0,"policy_independent_gates":0,"policy_independent_roots":0,"policy_mode_builds":0,"policy_specialized_gates":0,"policy_unspecialized_gates":0,"proof_seconds":0.000889785,"region_certificate_roots":95,"region_game_roots":54,"region_layer_seconds":9.8953e-05,"region_layers":58,"region_mode_seconds":0.000848899,"region_modes":17,"requested_roots":149,"setup_seconds":0.077497524,"successor_applications":116,"successor_substitutions":17}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: none
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 17: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| candidate_builder | 1 | 0.490933 s | 62.2% |
| seed_monitor_construction | 2 | 0.130558 s | 16.5% |
| target_check | 1 | 0.081932 s | 10.4% |
| target_monitor_construction | 1 | 0.069926 s | 8.9% |
| seed_solves | 2 | 0.045659 s | 5.8% |
| bdd_projection_relabel | 1291 | 0.026111 s | 3.3% |
| instantiate_templates | 40 | 0.009099 s | 1.2% |
| projection_metadata | 640 | 0.006945 s | 0.9% |
| support_extraction | 735 | 0.006050 s | 0.8% |
| substitute_variables | 40 | 0.003351 s | 0.4% |
| export | 111 | 0.001246 s | 0.2% |
| from_aag | 3 | 0.000771 s | 0.1% |
| variable_cube_construction | 7 | 0.000070 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| policy_construction_skolemization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |
