# S0 diagnostic summary

Source: `/home/gperez/GIT-repos/acacia-gr1-par2-timing/benchmarking/gr1-par2-20260923/s0/raw-p2b`

## Notes

- End-to-end: P1.4 tools (build-P14-dbe8c2b) + P2b generalizer (ef2b6393) + adapter 0043e97b, timing worktree, serial, 120 s, 8 GiB

## Targets

| Target | Verdict | Exit | Total wall | cgroup memory.peak | Censored stage |
|---|---:|---:|---:|---:|---|
| arbiter_with_cancel-n8 | REALIZABLE | 0 | 6.309 s | 1.18 GiB | none |
| arbiter_with_cancel-n9 | REALIZABLE | 0 | 10.370 s | 1.48 GiB | none |
| arbiter_with_cancel-n10 | REALIZABLE | 0 | 25.724 s | 1.95 GiB | none |
| load_balancer_unreal2-n6 | UNREALIZABLE | 1 | 2.217 s | 1.32 GiB | none |
| load_balancer_unreal2-n7 | UNREALIZABLE | 1 | 11.113 s | 1.58 GiB | none |
| load_balancer-n8 | REALIZABLE | 0 | 10.158 s | 1.02 GiB | none |
| load_balancer-n9 | REALIZABLE | 0 | 24.269 s | 1.06 GiB | none |
| amba_decomposed_lock-n15 | REALIZABLE | 0 | 35.936 s | 2.22 GiB | none |
| arbiter_with_buffer-n8 | REALIZABLE | 0 | 15.054 s | 1.05 GiB | none |
| arbiter_with_buffer-n9 | REALIZABLE | 0 | 118.212 s | 1.87 GiB | none |
| arbiter_on_inpchange-n6 | REALIZABLE | 0 | 23.762 s | 1.69 GiB | none |
| arbiter_on_inpchange-n7 | UNKNOWN | 2 | 120.175 s | 2.07 GiB | target_check ≥ 106.610 s |
| round_robin_arbiter_unreal2-n5 | UNREALIZABLE | 1 | 2.121 s | 1.35 GiB | none |
| round_robin_arbiter_unreal2-n6 | UNREALIZABLE | 1 | 7.174 s | 1.55 GiB | none |
| round_robin_arbiter_unreal2-n7 | UNREALIZABLE | 1 | 31.006 s | 2.49 GiB | none |
| collector_v1-n11 | UNKNOWN | 2 | 119.926 s | 2.10 GiB | canonicalize ≥ 119.604 s; target_monitor_construction ≥ 119.604 s |
| arbiter-n6 | REALIZABLE | 0 | 1.976 s | 936.18 MiB | none |
| prioritized_arbiter-n7 | REALIZABLE | 0 | 1.022 s | 925.56 MiB | none |

## Cross-target phase dominance

| Target | Dominant phase | Second | Third |
|---|---|---|---|
| arbiter_with_cancel-n8 | target_check — 2.452 s (38.9%) | bdd_projection_relabel — 1.230 s (19.5%) | instantiate_templates — 0.743 s (11.8%) |
| arbiter_with_cancel-n9 | target_check — 6.062 s (58.5%) | bdd_projection_relabel — 1.491 s (14.4%) | instantiate_templates — 0.991 s (9.6%) |
| arbiter_with_cancel-n10 | target_check — 20.641 s (80.2%) | bdd_projection_relabel — 1.882 s (7.3%) | instantiate_templates — 1.367 s (5.3%) |
| load_balancer_unreal2-n6 | target_solve — 1.454 s (65.6%) | target_check — 0.473 s (21.3%) | target_monitor_construction — 0.086 s (3.9%) |
| load_balancer_unreal2-n7 | target_solve — 9.454 s (85.1%) | target_check — 1.356 s (12.2%) | target_monitor_construction — 0.091 s (0.8%) |
| load_balancer-n8 | bdd_projection_relabel — 5.972 s (58.8%) | policy_construction_skolemization — 3.326 s (32.7%) | substitute_variables — 1.009 s (9.9%) |
| load_balancer-n9 | bdd_projection_relabel — 14.816 s (61.0%) | policy_construction_skolemization — 8.930 s (36.8%) | substitute_variables — 3.499 s (14.4%) |
| amba_decomposed_lock-n15 | bdd_projection_relabel — 21.466 s (59.7%) | policy_construction_skolemization — 11.547 s (32.1%) | target_check — 6.977 s (19.4%) |
| arbiter_with_buffer-n8 | policy_construction_skolemization — 9.493 s (63.1%) | bdd_projection_relabel — 5.336 s (35.4%) | export — 1.381 s (9.2%) |
| arbiter_with_buffer-n9 | policy_construction_skolemization — 99.960 s (84.6%) | bdd_projection_relabel — 20.485 s (17.3%) | export — 4.927 s (4.2%) |
| arbiter_on_inpchange-n6 | target_check — 11.984 s (50.4%) | bdd_projection_relabel — 4.715 s (19.8%) | instantiate_templates — 2.367 s (10.0%) |
| arbiter_on_inpchange-n7 | target_check — ≥ 106.610 s (≥ 88.7%) | bdd_projection_relabel — 5.830 s (4.9%) | instantiate_templates — 3.266 s (2.7%) |
| round_robin_arbiter_unreal2-n5 | target_check — 1.019 s (48.0%) | target_solve — 0.824 s (38.9%) | target_monitor_construction — 0.079 s (3.7%) |
| round_robin_arbiter_unreal2-n6 | target_check — 3.473 s (48.4%) | target_solve — 3.416 s (47.6%) | target_monitor_construction — 0.085 s (1.2%) |
| round_robin_arbiter_unreal2-n7 | target_check — 15.930 s (51.4%) | target_solve — 14.779 s (47.7%) | target_monitor_construction — 0.093 s (0.3%) |
| collector_v1-n11 | target_monitor_construction — ≥ 239.208 s (≥ 199.5%) | canonicalize — ≥ 119.604 s (≥ 99.7%) | seed_monitor_construction — 0.072 s (0.1%) |
| arbiter-n6 | bdd_projection_relabel — 0.422 s (21.4%) | export — 0.277 s (14.0%) | seed_monitor_construction — 0.271 s (13.7%) |
| prioritized_arbiter-n7 | seed_monitor_construction — 0.258 s (25.3%) | target_monitor_construction — 0.134 s (13.1%) | seed_solves — 0.091 s (8.9%) |

## Per-target detail

### arbiter_with_cancel-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 6.309 s
- cgroup `memory.peak`: 1.18 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":21484,"final_status":"VERIFIED","peak_live_nodes_sample":524289,"policy_counter_constants":992,"policy_cross_mode_root_reuses":2240,"policy_dependent_gates_per_mode":436,"policy_full_cache_modes":0,"policy_independent_gates":6148,"policy_independent_roots":70,"policy_mode_builds":32,"policy_specialized_gates":13952,"policy_unspecialized_gates":0,"proof_seconds":0.138893474,"requested_roots":1512,"setup_seconds":0.039932155,"successor_applications":180,"successor_substitutions":32}`
  - `target-8` (node cap 33554432): `{"aig_gates_visited":70321,"final_status":"VERIFIED","peak_live_nodes_sample":7667713,"policy_counter_constants":2450,"policy_cross_mode_root_reuses":8000,"policy_dependent_gates_per_mode":985,"policy_full_cache_modes":0,"policy_independent_gates":17821,"policy_independent_roots":160,"policy_mode_builds":50,"policy_specialized_gates":49250,"policy_unspecialized_gates":0,"proof_seconds":2.352716341,"requested_roots":3468,"setup_seconds":0.08220396,"successor_applications":282,"successor_substitutions":50}`
- Support width min/median/max: 25 / 25 / 25
- Owner-tuple arity histogram: 0: 16, 1: 589
- Subset reuse: 48 distinct; 48 reused; uses min/median/max 73 / 115 / 188; operations instantiation: 3950, projection: 1880
- Mask word-length histogram: 1: 1316, 2: 564
- Actual checker mode counts: 32: 1, 50: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 2.452153 s | 38.9% |
| bdd_projection_relabel | 9402 | 1.229687 s | 19.5% |
| instantiate_templates | 188 | 0.743410 s | 11.8% |
| export | 8874 | 0.598788 s | 9.5% |
| seed_solves | 6 | 0.596339 s | 9.5% |
| seed_monitor_construction | 6 | 0.430429 s | 6.8% |
| policy_construction_skolemization | 2 | 0.322222 s | 5.1% |
| target_monitor_construction | 2 | 0.165083 s | 2.6% |
| projection_metadata | 4324 | 0.083381 s | 1.3% |
| support_extraction | 3786 | 0.075690 s | 1.2% |
| from_aag | 8 | 0.015262 s | 0.2% |
| variable_cube_construction | 18 | 0.000370 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_cancel-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 10.370 s
- cgroup `memory.peak`: 1.48 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":21484,"final_status":"VERIFIED","peak_live_nodes_sample":524289,"policy_counter_constants":992,"policy_cross_mode_root_reuses":2240,"policy_dependent_gates_per_mode":436,"policy_full_cache_modes":0,"policy_independent_gates":6148,"policy_independent_roots":70,"policy_mode_builds":32,"policy_specialized_gates":13952,"policy_unspecialized_gates":0,"proof_seconds":0.136737861,"requested_roots":1512,"setup_seconds":0.042255466,"successor_applications":180,"successor_substitutions":32}`
  - `target-9` (node cap 33554432): `{"aig_gates_visited":95432,"final_status":"VERIFIED","peak_live_nodes_sample":17825793,"policy_counter_constants":3080,"policy_cross_mode_root_reuses":11088,"policy_dependent_gates_per_mode":1216,"policy_full_cache_modes":0,"policy_independent_gates":23310,"policy_independent_roots":198,"policy_mode_builds":56,"policy_specialized_gates":68096,"policy_unspecialized_gates":0,"proof_seconds":5.952632553,"requested_roots":4296,"setup_seconds":0.081993176,"successor_applications":316,"successor_substitutions":56}`
- Support width min/median/max: 25 / 25 / 25
- Owner-tuple arity histogram: 0: 16, 1: 608
- Subset reuse: 56 distinct; 56 reused; uses min/median/max 73 / 129 / 202; operations instantiation: 5374, projection: 2020
- Mask word-length histogram: 1: 1414, 2: 606
- Actual checker mode counts: 32: 1, 56: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 6.062430 s | 58.5% |
| bdd_projection_relabel | 11232 | 1.491052 s | 14.4% |
| instantiate_templates | 202 | 0.991120 s | 9.6% |
| export | 10366 | 0.774363 s | 7.5% |
| seed_solves | 6 | 0.585674 s | 5.6% |
| seed_monitor_construction | 6 | 0.420283 s | 4.1% |
| policy_construction_skolemization | 2 | 0.412926 s | 4.0% |
| target_monitor_construction | 2 | 0.172580 s | 1.7% |
| projection_metadata | 4646 | 0.087194 s | 0.8% |
| support_extraction | 4066 | 0.076937 s | 0.7% |
| from_aag | 8 | 0.006534 s | 0.1% |
| variable_cube_construction | 18 | 0.000370 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_cancel-n10

- Verdict/exit: REALIZABLE / 0
- Total wall: 25.724 s
- cgroup `memory.peak`: 1.95 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":21484,"final_status":"VERIFIED","peak_live_nodes_sample":524289,"policy_counter_constants":992,"policy_cross_mode_root_reuses":2240,"policy_dependent_gates_per_mode":436,"policy_full_cache_modes":0,"policy_independent_gates":6148,"policy_independent_roots":70,"policy_mode_builds":32,"policy_specialized_gates":13952,"policy_unspecialized_gates":0,"proof_seconds":0.13622859,"requested_roots":1512,"setup_seconds":0.0397326,"successor_applications":180,"successor_substitutions":32}`
  - `target-10` (node cap 33554432): `{"aig_gates_visited":125759,"final_status":"VERIFIED","peak_live_nodes_sample":31195137,"policy_counter_constants":3782,"policy_cross_mode_root_reuses":14880,"policy_dependent_gates_per_mode":1471,"policy_full_cache_modes":0,"policy_independent_gates":29678,"policy_independent_roots":240,"policy_mode_builds":62,"policy_specialized_gates":91202,"policy_unspecialized_gates":0,"proof_seconds":20.520165533,"requested_roots":5212,"setup_seconds":0.081750208,"successor_applications":350,"successor_substitutions":62}`
- Support width min/median/max: 25 / 25 / 25
- Owner-tuple arity histogram: 0: 16, 1: 627
- Subset reuse: 65 distinct; 65 reused; uses min/median/max 73 / 143 / 216; operations instantiation: 7165, projection: 2160
- Mask word-length histogram: 1: 1512, 2: 648
- Actual checker mode counts: 32: 1, 62: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 20.641163 s | 80.2% |
| bdd_projection_relabel | 13429 | 1.881680 s | 7.3% |
| instantiate_templates | 216 | 1.367456 s | 5.3% |
| export | 12018 | 1.063967 s | 4.1% |
| seed_solves | 6 | 0.600038 s | 2.3% |
| policy_construction_skolemization | 2 | 0.566105 s | 2.2% |
| seed_monitor_construction | 6 | 0.421598 s | 1.6% |
| target_monitor_construction | 2 | 0.170997 s | 0.7% |
| projection_metadata | 4968 | 0.109701 s | 0.4% |
| support_extraction | 4346 | 0.085523 s | 0.3% |
| from_aag | 8 | 0.006818 s | 0.0% |
| variable_cube_construction | 18 | 0.000359 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### load_balancer_unreal2-n6

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 2.217 s
- cgroup `memory.peak`: 1.32 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 67108864): `{"aig_gates_visited":7831,"final_status":"VERIFIED","peak_live_nodes_sample":786433,"policy_counter_constants":12,"policy_cross_mode_root_reuses":8,"policy_dependent_gates_per_mode":41,"policy_full_cache_modes":0,"policy_independent_gates":20,"policy_independent_roots":2,"policy_mode_builds":4,"policy_specialized_gates":164,"policy_unspecialized_gates":0,"proof_seconds":0.194770062,"requested_roots":1189,"setup_seconds":0.215841383,"successor_applications":2176,"successor_substitutions":4}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 12, 1: 46, 2: 70
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 4: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 1.454167 s | 65.6% |
| target_check | 1 | 0.472855 s | 21.3% |
| target_monitor_construction | 1 | 0.086324 s | 3.9% |
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
- Total wall: 11.113 s
- cgroup `memory.peak`: 1.58 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 67108864): `{"aig_gates_visited":15334,"final_status":"VERIFIED","peak_live_nodes_sample":3670017,"policy_counter_constants":12,"policy_cross_mode_root_reuses":8,"policy_dependent_gates_per_mode":41,"policy_full_cache_modes":0,"policy_independent_gates":20,"policy_independent_roots":2,"policy_mode_builds":4,"policy_specialized_gates":164,"policy_unspecialized_gates":0,"proof_seconds":0.995878085,"requested_roots":1494,"setup_seconds":0.214939131,"successor_applications":2752,"successor_substitutions":4}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 12, 1: 53, 2: 96
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 4: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_solve | 1 | 9.454153 s | 85.1% |
| target_check | 1 | 1.355763 s | 12.2% |
| target_monitor_construction | 1 | 0.091276 s | 0.8% |
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
- Total wall: 10.158 s
- cgroup `memory.peak`: 1.02 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":12342,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":272,"policy_cross_mode_root_reuses":1292,"policy_dependent_gates_per_mode":492,"policy_full_cache_modes":0,"policy_independent_gates":2600,"policy_independent_roots":76,"policy_mode_builds":17,"policy_specialized_gates":8364,"policy_unspecialized_gates":0,"proof_seconds":0.014744311,"requested_roots":666,"setup_seconds":0.041905641,"successor_applications":224,"successor_substitutions":17}`
  - `target-8` (node cap 33554432): `{"aig_gates_visited":78581,"final_status":"VERIFIED","peak_live_nodes_sample":131073,"policy_counter_constants":650,"policy_cross_mode_root_reuses":11102,"policy_dependent_gates_per_mode":1887,"policy_full_cache_modes":0,"policy_independent_gates":27238,"policy_independent_roots":427,"policy_mode_builds":26,"policy_specialized_gates":49062,"policy_unspecialized_gates":0,"proof_seconds":0.055781028,"requested_roots":1641,"setup_seconds":0.08702605,"successor_applications":344,"successor_substitutions":26}`
- Support width min/median/max: 0 / 20 / 20
- Owner-tuple arity histogram: 0: 96, 1: 249, 2: 46
- Subset reuse: 48 distinct; 48 reused; uses min/median/max 112 / 175 / 287; operations instantiation: 6020, projection: 2870
- Mask word-length histogram: 0: 984, 1: 1886
- Actual checker mode counts: 17: 1, 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| bdd_projection_relabel | 15252 | 5.971560 s | 58.8% |
| policy_construction_skolemization | 2 | 3.325934 s | 32.7% |
| substitute_variables | 287 | 1.008567 s | 9.9% |
| export | 518 | 0.809222 s | 8.0% |
| seed_monitor_construction | 6 | 0.399713 s | 3.9% |
| instantiate_templates | 287 | 0.227805 s | 2.2% |
| target_check | 1 | 0.171792 s | 1.7% |
| seed_solves | 6 | 0.168278 s | 1.7% |
| target_monitor_construction | 2 | 0.142178 s | 1.4% |
| projection_metadata | 6601 | 0.118423 s | 1.2% |
| support_extraction | 6793 | 0.087790 s | 0.9% |
| from_aag | 8 | 0.009814 s | 0.1% |
| variable_cube_construction | 28 | 0.000363 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### load_balancer-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 24.269 s
- cgroup `memory.peak`: 1.06 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":12342,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":272,"policy_cross_mode_root_reuses":1292,"policy_dependent_gates_per_mode":492,"policy_full_cache_modes":0,"policy_independent_gates":2600,"policy_independent_roots":76,"policy_mode_builds":17,"policy_specialized_gates":8364,"policy_unspecialized_gates":0,"proof_seconds":0.014584756,"requested_roots":666,"setup_seconds":0.040851142,"successor_applications":224,"successor_substitutions":17}`
  - `target-9` (node cap 33554432): `{"aig_gates_visited":154106,"final_status":"VERIFIED","peak_live_nodes_sample":196609,"policy_counter_constants":812,"policy_cross_mode_root_reuses":23664,"policy_dependent_gates_per_mode":3198,"policy_full_cache_modes":0,"policy_independent_gates":58760,"policy_independent_roots":816,"policy_mode_builds":29,"policy_specialized_gates":92742,"policy_unspecialized_gates":0,"proof_seconds":0.091948177,"requested_roots":2286,"setup_seconds":0.098609019,"successor_applications":384,"successor_substitutions":29}`
- Support width min/median/max: 0 / 20 / 20
- Owner-tuple arity histogram: 0: 96, 1: 256, 2: 48
- Subset reuse: 56 distinct; 56 reused; uses min/median/max 112 / 196 / 308; operations instantiation: 8176, projection: 3080
- Mask word-length histogram: 0: 1056, 1: 2024
- Actual checker mode counts: 17: 1, 29: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| bdd_projection_relabel | 18082 | 14.815747 s | 61.0% |
| policy_construction_skolemization | 2 | 8.929584 s | 36.8% |
| substitute_variables | 308 | 3.499030 s | 14.4% |
| export | 556 | 1.969193 s | 8.1% |
| seed_monitor_construction | 6 | 0.401605 s | 1.7% |
| instantiate_templates | 308 | 0.288605 s | 1.2% |
| target_check | 1 | 0.255663 s | 1.1% |
| seed_solves | 6 | 0.170978 s | 0.7% |
| target_monitor_construction | 2 | 0.144422 s | 0.6% |
| projection_metadata | 7084 | 0.129471 s | 0.5% |
| support_extraction | 7278 | 0.114029 s | 0.5% |
| from_aag | 8 | 0.009644 s | 0.0% |
| variable_cube_construction | 28 | 0.000402 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### amba_decomposed_lock-n15

- Verdict/exit: REALIZABLE / 0
- Total wall: 35.936 s
- cgroup `memory.peak`: 2.22 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":1188,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":42,"policy_cross_mode_root_reuses":133,"policy_dependent_gates_per_mode":61,"policy_full_cache_modes":0,"policy_independent_gates":461,"policy_independent_roots":19,"policy_mode_builds":7,"policy_specialized_gates":427,"policy_unspecialized_gates":0,"proof_seconds":0.002740799,"requested_roots":143,"setup_seconds":0.039007109,"successor_applications":63,"successor_substitutions":7}`
  - `target-15` (node cap 67108864): `{"aig_gates_visited":397545,"final_status":"VERIFIED","peak_live_nodes_sample":17170433,"policy_counter_constants":272,"policy_cross_mode_root_reuses":833,"policy_dependent_gates_per_mode":171,"policy_full_cache_modes":0,"policy_independent_gates":393593,"policy_independent_roots":49,"policy_mode_builds":17,"policy_specialized_gates":2907,"policy_unspecialized_gates":0,"proof_seconds":6.735307785,"requested_roots":523,"setup_seconds":0.176717141,"successor_applications":153,"successor_substitutions":17}`
- Support width min/median/max: 7 / 7 / 7
- Owner-tuple arity histogram: 0: 80, 1: 190
- Subset reuse: 29 distinct; 29 reused; uses min/median/max 25 / 65 / 90; operations instantiation: 1100, projection: 810
- Mask word-length histogram: 1: 810
- Actual checker mode counts: 7: 1, 17: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| bdd_projection_relabel | 3896 | 21.466085 s | 59.7% |
| policy_construction_skolemization | 2 | 11.547406 s | 32.1% |
| target_check | 1 | 6.976878 s | 19.4% |
| substitute_variables | 90 | 4.457971 s | 12.4% |
| export | 202 | 1.496203 s | 4.2% |
| seed_monitor_construction | 6 | 0.390924 s | 1.1% |
| target_monitor_construction | 2 | 0.175461 s | 0.5% |
| seed_solves | 6 | 0.131646 s | 0.4% |
| instantiate_templates | 90 | 0.035960 s | 0.1% |
| projection_metadata | 1890 | 0.023527 s | 0.1% |
| support_extraction | 2028 | 0.021455 s | 0.1% |
| from_aag | 8 | 0.002895 s | 0.0% |
| variable_cube_construction | 20 | 0.000124 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_buffer-n8

- Verdict/exit: REALIZABLE / 0
- Total wall: 15.054 s
- cgroup `memory.peak`: 1.05 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":1897,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":42,"policy_cross_mode_root_reuses":294,"policy_dependent_gates_per_mode":163,"policy_full_cache_modes":0,"policy_independent_gates":535,"policy_independent_roots":42,"policy_mode_builds":7,"policy_specialized_gates":1141,"policy_unspecialized_gates":0,"proof_seconds":0.001316528,"requested_roots":214,"setup_seconds":0.03913287,"successor_applications":35,"successor_substitutions":7}`
  - `target-8` (node cap 33554432): `{"aig_gates_visited":13346,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":90,"policy_cross_mode_root_reuses":2720,"policy_dependent_gates_per_mode":883,"policy_full_cache_modes":0,"policy_independent_gates":4124,"policy_independent_roots":272,"policy_mode_builds":10,"policy_specialized_gates":8830,"policy_unspecialized_gates":0,"proof_seconds":0.005783374,"requested_roots":612,"setup_seconds":0.090244553,"successor_applications":50,"successor_substitutions":10}`
- Support width min/median/max: 4 / 4 / 4
- Owner-tuple arity histogram: 0: 16, 1: 248
- Subset reuse: 22 distinct; 22 reused; uses min/median/max 13 / 19 / 32; operations instantiation: 217, projection: 288
- Mask word-length histogram: 1: 288
- Actual checker mode counts: 7: 1, 10: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| policy_construction_skolemization | 2 | 9.493444 s | 63.1% |
| bdd_projection_relabel | 1276 | 5.336479 s | 35.4% |
| export | 133 | 1.381044 s | 9.2% |
| seed_monitor_construction | 6 | 0.386971 s | 2.6% |
| substitute_variables | 32 | 0.148455 s | 1.0% |
| target_monitor_construction | 2 | 0.139076 s | 0.9% |
| target_check | 1 | 0.136562 s | 0.9% |
| seed_solves | 6 | 0.131992 s | 0.9% |
| support_extraction | 777 | 0.019470 s | 0.1% |
| projection_metadata | 672 | 0.006368 s | 0.0% |
| instantiate_templates | 32 | 0.004695 s | 0.0% |
| from_aag | 8 | 0.001678 s | 0.0% |
| variable_cube_construction | 20 | 0.000225 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_with_buffer-n9

- Verdict/exit: REALIZABLE / 0
- Total wall: 118.212 s
- cgroup `memory.peak`: 1.87 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":1897,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":42,"policy_cross_mode_root_reuses":294,"policy_dependent_gates_per_mode":163,"policy_full_cache_modes":0,"policy_independent_gates":535,"policy_independent_roots":42,"policy_mode_builds":7,"policy_specialized_gates":1141,"policy_unspecialized_gates":0,"proof_seconds":0.001316094,"requested_roots":214,"setup_seconds":0.039294281,"successor_applications":35,"successor_substitutions":7}`
  - `target-9` (node cap 33554432): `{"aig_gates_visited":26959,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":110,"policy_cross_mode_root_reuses":5830,"policy_dependent_gates_per_mode":1669,"policy_full_cache_modes":0,"policy_independent_gates":8145,"policy_independent_roots":530,"policy_mode_builds":11,"policy_specialized_gates":18359,"policy_unspecialized_gates":0,"proof_seconds":0.00993576,"requested_roots":938,"setup_seconds":0.148808075,"successor_applications":55,"successor_substitutions":11}`
- Support width min/median/max: 4 / 4 / 4
- Owner-tuple arity histogram: 0: 16, 1: 256
- Subset reuse: 23 distinct; 23 reused; uses min/median/max 13 / 21 / 34; operations instantiation: 254, projection: 306
- Mask word-length histogram: 1: 306
- Actual checker mode counts: 7: 1, 11: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| policy_construction_skolemization | 2 | 99.960397 s | 84.6% |
| bdd_projection_relabel | 1380 | 20.485269 s | 17.3% |
| export | 142 | 4.927268 s | 4.2% |
| substitute_variables | 34 | 0.949389 s | 0.8% |
| seed_monitor_construction | 6 | 0.387583 s | 0.3% |
| target_check | 1 | 0.305127 s | 0.3% |
| target_monitor_construction | 2 | 0.142507 s | 0.1% |
| seed_solves | 6 | 0.131430 s | 0.1% |
| support_extraction | 826 | 0.068226 s | 0.1% |
| projection_metadata | 714 | 0.006619 s | 0.0% |
| instantiate_templates | 34 | 0.005509 s | 0.0% |
| from_aag | 8 | 0.001900 s | 0.0% |
| variable_cube_construction | 20 | 0.000237 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_on_inpchange-n6

- Verdict/exit: REALIZABLE / 0
- Total wall: 23.762 s
- cgroup `memory.peak`: 1.69 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":40375,"final_status":"VERIFIED","peak_live_nodes_sample":6881281,"policy_counter_constants":462,"policy_cross_mode_root_reuses":1540,"policy_dependent_gates_per_mode":296,"policy_full_cache_modes":0,"policy_independent_gates":25495,"policy_independent_roots":70,"policy_mode_builds":22,"policy_specialized_gates":6512,"policy_unspecialized_gates":0,"proof_seconds":2.947381104,"requested_roots":897,"setup_seconds":0.042522733,"successor_applications":130,"successor_substitutions":22}`
  - `target-6` (node cap 33554432): `{"aig_gates_visited":59768,"final_status":"VERIFIED","peak_live_nodes_sample":22675457,"policy_counter_constants":650,"policy_cross_mode_root_reuses":2496,"policy_dependent_gates_per_mode":403,"policy_full_cache_modes":0,"policy_independent_gates":37435,"policy_independent_roots":96,"policy_mode_builds":26,"policy_specialized_gates":10478,"policy_unspecialized_gates":0,"proof_seconds":11.864911406,"requested_roots":1206,"setup_seconds":0.084182825,"successor_applications":154,"successor_substitutions":26}`
- Support width min/median/max: 39 / 39 / 39
- Owner-tuple arity histogram: 0: 16, 1: 667
- Subset reuse: 35 distinct; 35 reused; uses min/median/max 53 / 63 / 116; operations instantiation: 1475, projection: 1160
- Mask word-length histogram: 1: 348, 2: 812
- Actual checker mode counts: 22: 1, 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 11.983626 s | 50.4% |
| bdd_projection_relabel | 4839 | 4.714767 s | 19.8% |
| instantiate_templates | 116 | 2.367086 s | 10.0% |
| export | 4510 | 1.453272 s | 6.1% |
| seed_solves | 6 | 1.159835 s | 4.9% |
| policy_construction_skolemization | 2 | 0.749317 s | 3.2% |
| seed_monitor_construction | 6 | 0.441764 s | 1.9% |
| target_monitor_construction | 2 | 0.175046 s | 0.7% |
| projection_metadata | 2668 | 0.076408 s | 0.3% |
| support_extraction | 2350 | 0.072299 s | 0.3% |
| from_aag | 8 | 0.036635 s | 0.2% |
| variable_cube_construction | 18 | 0.000620 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### arbiter_on_inpchange-n7

- Verdict/exit: UNKNOWN / 2
- Total wall: 120.175 s
- cgroup `memory.peak`: 2.07 GiB
- Censored stages: target_check ≥ 106.610 s (absolute_deadline_exhausted, sigterm)
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":40375,"final_status":"VERIFIED","peak_live_nodes_sample":6881281,"policy_counter_constants":462,"policy_cross_mode_root_reuses":1540,"policy_dependent_gates_per_mode":296,"policy_full_cache_modes":0,"policy_independent_gates":25495,"policy_independent_roots":70,"policy_mode_builds":22,"policy_specialized_gates":6512,"policy_unspecialized_gates":0,"proof_seconds":2.975026021,"requested_roots":897,"setup_seconds":0.043159395,"successor_applications":130,"successor_substitutions":22}`
- Support width min/median/max: 39 / 39 / 39
- Owner-tuple arity histogram: 0: 16, 1: 690
- Subset reuse: 41 distinct; 41 reused; uses min/median/max 53 / 73 / 126; operations instantiation: 2063, projection: 1260
- Mask word-length histogram: 1: 378, 2: 882
- Actual checker mode counts: 22: 1, 30: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| bdd_projection_relabel | 5717 | 5.830186 s | 4.9% |
| instantiate_templates | 126 | 3.266429 s | 2.7% |
| export | 5334 | 1.987115 s | 1.7% |
| seed_solves | 6 | 1.185615 s | 1.0% |
| policy_construction_skolemization | 2 | 1.027340 s | 0.9% |
| seed_monitor_construction | 6 | 0.442190 s | 0.4% |
| target_monitor_construction | 2 | 0.173008 s | 0.1% |
| projection_metadata | 2898 | 0.081495 s | 0.1% |
| support_extraction | 2550 | 0.077379 s | 0.1% |
| from_aag | 8 | 0.037492 s | 0.0% |
| variable_cube_construction | 18 | 0.000684 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_check | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### round_robin_arbiter_unreal2-n5

- Verdict/exit: UNREALIZABLE / 1
- Total wall: 2.121 s
- cgroup `memory.peak`: 1.35 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-5` (node cap 67108864): `{"aig_gates_visited":141862,"final_status":"VERIFIED","peak_live_nodes_sample":196609,"policy_counter_constants":240,"policy_cross_mode_root_reuses":270,"policy_dependent_gates_per_mode":132039,"policy_full_cache_modes":16,"policy_independent_gates":326,"policy_independent_roots":270,"policy_mode_builds":16,"policy_specialized_gates":0,"policy_unspecialized_gates":132039,"proof_seconds":0.478303256,"requested_roots":2754,"setup_seconds":0.282901451,"successor_applications":4736,"successor_substitutions":16}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 60, 2: 40
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 16: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 1.018518 s | 48.0% |
| target_solve | 1 | 0.824184 s | 38.9% |
| target_monitor_construction | 1 | 0.078730 s | 3.7% |
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
- Total wall: 7.174 s
- cgroup `memory.peak`: 1.55 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-6` (node cap 67108864): `{"aig_gates_visited":734342,"final_status":"VERIFIED","peak_live_nodes_sample":1310721,"policy_counter_constants":342,"policy_cross_mode_root_reuses":1043,"policy_dependent_gates_per_mode":714747,"policy_full_cache_modes":19,"policy_independent_gates":1112,"policy_independent_roots":1043,"policy_mode_builds":19,"policy_specialized_gates":0,"policy_unspecialized_gates":714747,"proof_seconds":2.531589289,"requested_roots":5059,"setup_seconds":0.201455253,"successor_applications":7744,"successor_substitutions":19}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 72, 2: 60
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 19: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 3.472976 s | 48.4% |
| target_solve | 1 | 3.415684 s | 47.6% |
| target_monitor_construction | 1 | 0.084751 s | 1.2% |
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
- Total wall: 31.006 s
- cgroup `memory.peak`: 2.49 GiB
- Censored stages: none
- Checker `--stats` payloads:
  - `target-7` (node cap 67108864): `{"aig_gates_visited":3646651,"final_status":"VERIFIED","peak_live_nodes_sample":6553601,"policy_counter_constants":462,"policy_cross_mode_root_reuses":4120,"policy_dependent_gates_per_mode":3609146,"policy_full_cache_modes":22,"policy_independent_gates":4202,"policy_independent_roots":4120,"policy_mode_builds":22,"policy_specialized_gates":0,"policy_unspecialized_gates":3609146,"proof_seconds":13.760068948,"requested_roots":10210,"setup_seconds":0.307949663,"successor_applications":11832,"successor_substitutions":22}`
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 2, 1: 84, 2: 84
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: 22: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_check | 1 | 15.930462 s | 51.4% |
| target_solve | 1 | 14.778704 s | 47.7% |
| target_monitor_construction | 1 | 0.093449 s | 0.3% |
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
- Total wall: 119.926 s
- cgroup `memory.peak`: 2.10 GiB
- Censored stages: canonicalize ≥ 119.604 s (subprocess_timeout); target_monitor_construction ≥ 119.604 s (subprocess_timeout)
- Checker `--stats`: no payload captured
- Support width min/median/max: n/a
- Owner-tuple arity histogram: 0: 42, 1: 12
- Subset reuse: 0 distinct; 0 reused; uses min/median/max n/a; operations none
- Mask word-length histogram: none
- Actual checker mode counts: none

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| target_monitor_construction | 1 | 119.603983 s | 99.7% |
| seed_monitor_construction | 1 | 0.072369 s | 0.1% |
| seed_solves | 1 | 0.043686 s | 0.0% |
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
- Total wall: 1.976 s
- cgroup `memory.peak`: 936.18 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":12710,"final_status":"VERIFIED","peak_live_nodes_sample":65537,"policy_counter_constants":462,"policy_cross_mode_root_reuses":1430,"policy_dependent_gates_per_mode":296,"policy_full_cache_modes":0,"policy_independent_gates":5072,"policy_independent_roots":65,"policy_mode_builds":22,"policy_specialized_gates":6512,"policy_unspecialized_gates":0,"proof_seconds":0.029772763,"requested_roots":842,"setup_seconds":0.039548085,"successor_applications":130,"successor_substitutions":22}`
  - `target-6` (node cap 33554432): `{"aig_gates_visited":19687,"final_status":"VERIFIED","peak_live_nodes_sample":131073,"policy_counter_constants":650,"policy_cross_mode_root_reuses":2340,"policy_dependent_gates_per_mode":403,"policy_full_cache_modes":0,"policy_independent_gates":7632,"policy_independent_roots":90,"policy_mode_builds":26,"policy_specialized_gates":10478,"policy_unspecialized_gates":0,"proof_seconds":0.053104834,"requested_roots":1140,"setup_seconds":0.07904038,"successor_applications":154,"successor_substitutions":26}`
- Support width min/median/max: 19 / 19 / 19
- Owner-tuple arity histogram: 0: 12, 1: 325
- Subset reuse: 34 distinct; 34 reused; uses min/median/max 53 / 63 / 116; operations instantiation: 1475, projection: 1044
- Mask word-length histogram: 1: 1044
- Actual checker mode counts: 22: 1, 26: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| bdd_projection_relabel | 4607 | 0.422365 s | 21.4% |
| export | 4510 | 0.277051 s | 14.0% |
| seed_monitor_construction | 4 | 0.270581 s | 13.7% |
| instantiate_templates | 116 | 0.215500 s | 10.9% |
| seed_solves | 4 | 0.185355 s | 9.4% |
| policy_construction_skolemization | 2 | 0.150339 s | 7.6% |
| target_monitor_construction | 2 | 0.146211 s | 7.4% |
| target_check | 1 | 0.137405 s | 7.0% |
| projection_metadata | 2320 | 0.045344 s | 2.3% |
| support_extraction | 2108 | 0.034878 s | 1.8% |
| from_aag | 6 | 0.005485 s | 0.3% |
| variable_cube_construction | 18 | 0.000286 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| substitute_variables | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |

### prioritized_arbiter-n7

- Verdict/exit: REALIZABLE / 0
- Total wall: 1.022 s
- cgroup `memory.peak`: 925.56 MiB
- Censored stages: none
- Checker `--stats` payloads:
  - `next-5` (node cap 16777216): `{"aig_gates_visited":5754,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":156,"policy_cross_mode_root_reuses":611,"policy_dependent_gates_per_mode":378,"policy_full_cache_modes":0,"policy_independent_gates":642,"policy_independent_roots":47,"policy_mode_builds":13,"policy_specialized_gates":4914,"policy_unspecialized_gates":0,"proof_seconds":0.002274765,"requested_roots":394,"setup_seconds":0.038466224,"successor_applications":88,"successor_substitutions":13}`
  - `target-7` (node cap 33554432): `{"aig_gates_visited":19751,"final_status":"VERIFIED","peak_live_nodes_sample":1,"policy_counter_constants":272,"policy_cross_mode_root_reuses":2499,"policy_dependent_gates_per_mode":982,"policy_full_cache_modes":0,"policy_independent_gates":2776,"policy_independent_roots":147,"policy_mode_builds":17,"policy_specialized_gates":16694,"policy_unspecialized_gates":0,"proof_seconds":0.005887047,"requested_roots":704,"setup_seconds":0.077731864,"successor_applications":116,"successor_substitutions":17}`
- Support width min/median/max: 7 / 9 / 9
- Owner-tuple arity histogram: 0: 60, 1: 156
- Subset reuse: 19 distinct; 19 reused; uses min/median/max 30 / 40 / 70; operations instantiation: 430, projection: 490
- Mask word-length histogram: 1: 490
- Actual checker mode counts: 13: 1, 17: 1

| Phase | Calls | Time | Share of total wall |
|---|---:|---:|---:|
| seed_monitor_construction | 4 | 0.258222 s | 25.3% |
| target_monitor_construction | 2 | 0.133869 s | 13.1% |
| seed_solves | 4 | 0.090850 s | 8.9% |
| target_check | 1 | 0.088017 s | 8.6% |
| bdd_projection_relabel | 2256 | 0.046766 s | 4.6% |
| policy_construction_skolemization | 2 | 0.022862 s | 2.2% |
| instantiate_templates | 70 | 0.014978 s | 1.5% |
| projection_metadata | 1120 | 0.012164 s | 1.2% |
| support_extraction | 1344 | 0.011213 s | 1.1% |
| export | 236 | 0.009832 s | 1.0% |
| substitute_variables | 70 | 0.004578 s | 0.4% |
| from_aag | 6 | 0.001328 s | 0.1% |
| variable_cube_construction | 16 | 0.000160 s | 0.0% |
| mode_specialization | 0 | 0.000000 s | 0.0% |
| target_solve | 0 | 0.000000 s | 0.0% |
