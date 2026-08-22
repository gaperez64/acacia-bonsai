# Acacia–ltlsynt gap census

This census measures the residual FMCAD'26 gap at the current shipping configuration. It covers every logical `ltlsynt_only` row in the frozen SYNTCOMP24 0s–20s, SYNTCOMP25 panel, and SYNTCOMP26 panel crossovers, plus every `acacia_slow` instance that both tools solve where Acacia takes more than 2× ltlsynt and more than 0.3 s. A later wrapper audit found six rows produced by a malformed empty-side partition; they remain in the instance table as an explicit audit trail but are excluded from the corrected residual counts.

## Instrument and gate

- Source head: `079904a34db8589aa310bd1a1f2fe8d69b1ffdf1`.
- Diagnostics binary SHA-256: `5d114a793cb154acd4f29b7000558087a92b0ad8b4db45783df807b007b8975c`.
- The 104 Meson options match the PR #118 campaign build exactly except
  `acacia_enable_diagnostics=false → true`.
- Historical `benchmarking/regression-gate.sh build_diag_shipping`: 40/40 frozen verdicts and
  `GATE PASS`. Its 112.198 s PAR-2 predates the empty-partition repair and is retained only as
  provenance, not as same-configuration performance evidence.
- The gate was repaired for the content-addressed corpus: results are keyed by logical
  suite/name through `sources.tsv`, including two regression hashes shared across suites.
  The wrapper now rejects an unavailable user systemd manager instead of mistaking the
  launcher's exit status 1 for an unrealizable solver verdict.

Each target ran for 20 s with progress every 32 iterations in an 8 GiB, zero-swap user-systemd scope. Raw solver streams were filtered online; only compact diagnostic CSVs under `_bm-logs/` were retained, and none are committed.

## Coverage and mechanism gate

- Corrected logical coverage: **153/153 `ltlsynt_only`** and **108/108 `acacia_slow`** rows (261 residual rows), plus 6 wrapper-artifact rows retained for audit.
- Physical corpus coverage: 129 unique files for the corrected loss set and 226 for the residual census. The plan's predicted 75 unique loss files was stale; the checked-in post-deduplication maps resolve the 153 logical losses to 129 files.
- Classification: fixed-point children use `summarize-diag-phases.py`'s 20%
  `apply_ms`/`downset_ms` rule; action-construction stalls count as M1 because they
  materialize concrete letters before the loop; translation-depth stalls count as M3.
  M4 overrides those labels only when a timed-out target has a terminal
  `spot-fast-path` / `solved-losing` sibling. All other balanced fixed-point cases are mixed.

| set | M1 letter-loop | M2 downset | M3 translation-stall | M4 one-sided-race | mixed | residual total | wrapper artifacts |
|---|---:|---:|---:|---:|---:|---:|---:|
| syntcomp24 all | 23 | 39 | 11 | 8 | 12 | 93 | 0 |
| syntcomp25 all | 55 | 17 | 22 | 1 | 6 | 101 | 4 |
| syntcomp26 all | 34 | 10 | 15 | 0 | 8 | 67 | 2 |
| **corrected census rows** | **112** | **66** | **48** | **9** | **26** | **261** | **6** |
| **`ltlsynt_only` only** | **47** | **37** | **48** | **9** | **12** | **153** | **3** |

M1 (47 losses), M2 (37), and corrected M3 (48) clear the plan's 15-instance gate. M4 has only 9 losses, so Step 6 is not admitted. The detector emitted the `0/0 star gens` decline on 51 logical census rows (28 losses).

### Step 3a: `0/0 star gens` diagnosis

An exhaustive, unoptimized probe with `ACACIA_SYMMETRY_VERBOSE_DIAGNOSTICS=1` separated two
conservative declines that the compact production diagnostic had conflated:

- `prioritized_arbiter10.ltl` and `prioritized_arbiter6.ltl` are symmetric, and the detector
  verifies every transposition of the remaining output family (`g_0…g_9` and `g_0…g_5`,
  respectively). Their verification matrices are all ones and yield two client-state blocks.
  The up-front `spot::realizability_simplifier` changes both formulas and removes the indexed
  request inputs from the translated automaton, so admission correctly stops at
  `no indexed input AP families`; the input-orbit solver has no indexed input support left to
  reduce.
- `simple_arbiter_with_hints10.ltl` retains both `r_0…r_9` and `g_0…g_9`, but its explicit
  ordered hint requires `g_0`, then `X g_1`, through `X^9 g_9`. Consequently no client
  transposition is a structural automorphism: its exhaustive matrix is the identity and the
  block layout is absent.

Thus the sampled `0/0` results are genuine properties of the final game automata, not a detector
bug. Disabling the realizability simplifier would discard an independently measured major
optimization and would not repair the asymmetric hinted case. Step 3b may therefore evaluate the
numeric admission gates one at a time, as planned.

### Step 3b: numeric admission outcomes

The three gates were made configurable and tested one at a time. Lowering the minimum indexed
client count from 3 to 2 and raising the unhinted-recognition state cap from 512 to 2,048 both
passed the formal gates but produced no panel gain, so their shipping defaults remain unchanged.
Lowering the minimum client-state block payoff from 4 to 2 passed G1 and G3 and is the one
admission change retained:

| experiment | SYNTCOMP25 solved / PAR-2 s, baseline → candidate | SYNTCOMP26 solved / PAR-2 s, baseline → candidate | decision |
|---|---|---|---|
| minimum clients 3 → 2 | 106/180 / 2799.825 → 106/180 / 2797.068 | 134/180 / 1712.260 → 134/180 / 1713.968 | rejected: no new admission or answer |
| maximum states 512 → 2,048 | 106/180 / 2778.691 → 106/180 / 2785.220 | 134/180 / 1714.260 → 133/180 / 1731.718 at 17 s; G3 remeasurement recovered the answer | rejected: no gain, added overhead |
| minimum blocks 4 → 2 | 106/180 / 2780.724 → **107/180 / 2746.223** | 134/180 / 1702.186 → 134/180 / 1706.153 | **landed** |

The landed threshold means verified layouts with two or three client-state blocks now reach the
equivariant solver instead of being declined for low payoff. In the frozen panels this converted
`syntcomp25/arbiter_with_buffer_pb_5_pe_.ltl` from a 17.022 s timeout to REALIZABLE in 7.011 s;
it does not make formulas with no verified group or no indexed input family symmetric.

## Instance table

Numeric telemetry is the maximum reported by any forked child for the instance. Equivariant reasons are the distinct child-level declines; `attempted` means at least one child entered the solver and no child-level decline was reported.

| suite | instance | set | mechanism | actions_seen | max_f | loops | aut_states | translation_ms | equivariant decline | Acacia s | ltlsynt s |
|---|---|---|---|---:|---:|---:|---:|---:|---|---:|---:|
| syntcomp24 | `Automata16S.ltl` | ltlsynt_only | M4 one-sided-race | 0 | 0 | 0 | 36 | 3 | not run | 17.057966 | 0.023956 |
| syntcomp24 | `Automata32S.ltl` | ltlsynt_only | M4 one-sided-race | 0 | 0 | 0 | 68 | 5 | not run | 17.061623 | 0.026680 |
| syntcomp24 | `FelixSpecFixed2_fa4d4ce3.ltl` | ltlsynt_only | M4 one-sided-race | 0 | 0 | 0 | 2 | 66 | not run | 17.048403 | 0.028394 |
| syntcomp24 | `FelixSpecFixed3_b0840146.ltl` | ltlsynt_only | M4 one-sided-race | 0 | 0 | 0 | 2 | 62 | not run | 17.065379 | 0.028430 |
| syntcomp24 | `FelixSpecFixed4_3916ec59.ltl` | ltlsynt_only | M4 one-sided-race | 0 | 0 | 0 | 2 | 62 | not run | 17.057857 | 0.032574 |
| syntcomp24 | `FelixSpecFixed4_dd3a27e1.ltl` | ltlsynt_only | M4 one-sided-race | 0 | 0 | 0 | 2 | 65 | not run | 17.067925 | 0.029553 |
| syntcomp24 | `FelixSpecFixed_2418b67e.ltl` | ltlsynt_only | M4 one-sided-race | 0 | 0 | 0 | 2 | 62 | not run | 17.064967 | 0.028883 |
| syntcomp24 | `KitchenTimerV9.ltl` | acacia_slow | mixed | 16832 | 3504 | 1052 | 146 | 282 | no indexed AP families | 0.613594 | 0.028215 |
| syntcomp24 | `LedMatrix.ltl` | ltlsynt_only | M3 translation-stall | 1006 | 1 | 302 | 775 | 6770 | no indexed AP families | 17.059467 | 0.365001 |
| syntcomp24 | `OneCounter.ltl` | acacia_slow | M1 letter-loop | 2571128 | 6 | 6560 | 67 | 2963 | no indexed AP families | 2.618658 | 0.023377 |
| syntcomp24 | `OneCounterGuiA6.ltl` | acacia_slow | M1 letter-loop | 250824 | 94 | 4480 | 55 | 315 | no indexed AP families | 0.334154 | 0.019073 |
| syntcomp24 | `OneCounterGuiA7.ltl` | acacia_slow | M1 letter-loop | 672966 | 121 | 6867 | 55 | 901 | no indexed AP families | 0.847893 | 0.018517 |
| syntcomp24 | `OneCounterGuiA8.ltl` | acacia_slow | M1 letter-loop | 1345932 | 242 | 6867 | 67 | 3484 | no indexed AP families | 3.260171 | 0.021691 |
| syntcomp24 | `OneCounterGuiA9.ltl` | acacia_slow | M1 letter-loop | 2508408 | 6 | 6400 | 67 | 3045 | no indexed AP families | 3.026728 | 0.025182 |
| syntcomp24 | `SPI.ltl` | acacia_slow | M2 downset | 27820 | 15 | 2140 | 33 | 251 | no indexed AP families | 0.322244 | 0.017163 |
| syntcomp24 | `SPIReadManag.ltl` | acacia_slow | mixed | 7600 | 132 | 36 | 50 | 131 | no indexed AP families | 0.310156 | 0.019571 |
| syntcomp24 | `TwoCounters3.ltl` | acacia_slow | mixed | 31714 | 1 | 101 | 26 | 1681 | no indexed AP families | 1.292385 | 0.022189 |
| syntcomp24 | `TwoCountersDisButA8.ltl` | acacia_slow | M2 downset | 992 | 1 | 3 | 38 | 144 | no indexed AP families | 0.308658 | 0.019916 |
| syntcomp24 | `TwoCountersDisButA9.ltl` | acacia_slow | mixed | 2040 | 1 | 3 | 55 | 207 | no indexed AP families | 0.588563 | 0.018326 |
| syntcomp24 | `TwoCountersDisButAC.ltl` | acacia_slow | M1 letter-loop | 2040 | 1 | 3 | 55 | 187 | no indexed AP families | 0.584370 | 0.017910 |
| syntcomp24 | `abcg_arbiter3.ltl` | ltlsynt_only | M2 downset | 43776 | 1884 | 32 | 2235 | 614 | too many automaton states | 17.048238 | 0.255378 |
| syntcomp24 | `abcg_arbiter4.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.050744 | 16.798562 |
| syntcomp24 | `amba_decomposed_lock10.ltl` | acacia_slow | M2 downset | 131072 | 1 | 2 | 10 | 2 | no indexed input AP families | 0.470261 | 0.022216 |
| syntcomp24 | `amba_decomposed_lock11.ltl` | acacia_slow | M2 downset | 2195456 | 1 | 8 | 10 | 2 | no indexed input AP families | 1.966193 | 0.020337 |
| syntcomp24 | `amba_decomposed_lock12.ltl` | acacia_slow | M2 downset | 16777216 | 1 | 26 | 10 | 3 | no indexed input AP families | 8.137156 | 0.023343 |
| syntcomp24 | `amba_decomposed_lock13.ltl` | ltlsynt_only | mixed | 33554432 | 1 | 23 | 10 | 7 | no indexed input AP families | 17.167234 | 0.023663 |
| syntcomp24 | `amba_decomposed_lock14.ltl` | ltlsynt_only | mixed | 10420224 | 1 | 4 | 10 | 22 | no indexed input AP families | 17.320070 | 0.029270 |
| syntcomp24 | `amba_decomposed_lock15.ltl` | ltlsynt_only | M1 letter-loop | 0 | 0 | 0 | 10 | 72 | no indexed input AP families | 17.124518 | 0.059343 |
| syntcomp24 | `amba_decomposed_lock16.ltl` | ltlsynt_only | M1 letter-loop | 0 | 0 | 0 | 10 | 295 | no indexed input AP families | 17.112303 | 0.107094 |
| syntcomp24 | `arbiter5.ltl` | acacia_slow | M1 letter-loop | 64480 | 743 | 2015 | 353 | 30 | 0/0 star gens | 0.881678 | 0.040295 |
| syntcomp24 | `arbiter6.ltl` | ltlsynt_only | M1 letter-loop | 174144 | 729 | 2721 | 929 | 174 | too many automaton states | 17.034216 | 0.131735 |
| syntcomp24 | `arbiter7.ltl` | ltlsynt_only | M2 downset | 1024 | 2187 | 8 | 2368 | 1210 | attempted | 17.043697 | 0.702966 |
| syntcomp24 | `arbiter8.ltl` | ltlsynt_only | M2 downset | 1024 | 6561 | 4 | 5888 | 8716 | attempted | 17.055227 | 4.525146 |
| syntcomp24 | `arbiter_with_buffer5.ltl` | ltlsynt_only | M2 downset | 9216 | 3872 | 10 | 497 | 53 | low block payoff | 17.035393 | 0.166079 |
| syntcomp24 | `arbiter_with_buffer6.ltl` | ltlsynt_only | M2 downset | 32768 | 1984 | 8 | 1377 | 357 | low block payoff | 17.045566 | 1.558267 |
| syntcomp24 | `arbiter_with_buffer7.ltl` | ltlsynt_only | mixed | 65536 | 1024 | 4 | 3649 | 3353 | low block payoff | 17.252744 | 15.436936 |
| syntcomp24 | `arbiter_with_cancel5.ltl` | acacia_slow | M2 downset | 240813 | 1043 | 992 | 433 | 626 | 0/0 star gens | 2.848843 | 0.198137 |
| syntcomp24 | `arbiter_with_cancel6.ltl` | ltlsynt_only | M2 downset | 512 | 1179 | 8 | 1120 | 7968 | attempted | 17.034598 | 1.706583 |
| syntcomp24 | `arbiter_with_cancel7.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.040421 | 16.005075 |
| syntcomp24 | `detector_unreal12.ltl` | acacia_slow | M2 downset | 1278 | 2 | 640 | 44 | 381 | no indexed input AP families; 0/0 star gens | 0.484572 | 0.049335 |
| syntcomp24 | `detector_unreal13.ltl` | acacia_slow | M2 downset | 1470 | 2 | 736 | 47 | 909 | no indexed input AP families; 0/0 star gens | 1.176585 | 0.078457 |
| syntcomp24 | `detector_unreal14.ltl` | acacia_slow | M2 downset | 1790 | 2 | 896 | 50 | 2554 | no indexed input AP families; 0/0 star gens | 2.994562 | 0.152534 |
| syntcomp24 | `detector_unreal15.ltl` | acacia_slow | M2 downset | 1662 | 2 | 832 | 53 | 6490 | no indexed input AP families; 0/0 star gens | 7.339232 | 0.313585 |
| syntcomp24 | `detector_unreal16.ltl` | ltlsynt_only | M2 downset | 1342 | 2 | 672 | 56 | 16100 | no indexed input AP families; 0/0 star gens | 17.074595 | 0.692441 |
| syntcomp24 | `finding_nemo_1.ltl` | ltlsynt_only | M2 downset | 14342 | 574 | 5420 | 375 | 69 | no indexed input AP families; too few indexed clients | 17.027872 | 0.025603 |
| syntcomp24 | `finding_nemo_2.ltl` | ltlsynt_only | mixed | 25252 | 87 | 9023 | 2763 | 2055 | too few indexed clients; too many automaton states | 17.034095 | 0.038849 |
| syntcomp24 | `finding_nemo_3.ltl` | ltlsynt_only | mixed | 43908 | 141 | 12624 | 122 | 77 | 0/0 star gens | 17.021976 | 0.087324 |
| syntcomp24 | `finding_nemo_4.ltl` | ltlsynt_only | mixed | 61620 | 361 | 19026 | 159 | 1070 | 0/0 star gens | 17.036287 | 0.435441 |
| syntcomp24 | `finding_nemo_5.ltl` | ltlsynt_only | M2 downset | 12400 | 1191 | 3712 | 197 | 19948 | 0/0 star gens | 17.048868 | 6.093620 |
| syntcomp24 | `full_arbiter_enc8.ltl` | ltlsynt_only | M2 downset | 64 | 128 | 2 | 22760 | 2885 | attempted | 17.025015 | 1.212506 |
| syntcomp24 | `lift3.ltl` | ltlsynt_only | M2 downset | 1528 | 14060 | 192 | 169 | 11 | too few indexed clients | 17.028254 | 0.028085 |
| syntcomp24 | `lift4.ltl` | ltlsynt_only | M2 downset | 512 | 5580 | 32 | 594 | 58 | too few indexed clients; too many automaton states | 17.017717 | 0.043160 |
| syntcomp24 | `lift5.ltl` | ltlsynt_only | M2 downset | 1024 | 6915 | 32 | 1896 | 478 | too many automaton states | 17.041677 | 0.138964 |
| syntcomp24 | `lift6.ltl` | ltlsynt_only | M2 downset | 2048 | 1310 | 32 | 5550 | 2303 | too many automaton states | 17.079908 | 0.705637 |
| syntcomp24 | `lift7.ltl` | ltlsynt_only | M1 letter-loop | 0 | 0 | 0 | 15284 | 11354 | not run | 17.035262 | 4.071522 |
| syntcomp24 | `lift_gr1+3.ltl` | ltlsynt_only | mixed | 3091424 | 9262 | 96608 | 270 | 38 | too few indexed clients | 17.030518 | 0.447355 |
| syntcomp24 | `lift_gr13.ltl` | ltlsynt_only | mixed | 4384752 | 9066 | 274048 | 225 | 22 | too few indexed clients | 17.030136 | 0.087162 |
| syntcomp24 | `lift_unary_enc3.ltl` | ltlsynt_only | M2 downset | 1272 | 18404 | 160 | 615 | 32 | 0/0 star gens | 17.030262 | 0.043653 |
| syntcomp24 | `lift_unary_enc4.ltl` | ltlsynt_only | M2 downset | 1024 | 2794 | 64 | 4219 | 1372 | too many automaton states | 17.046468 | 0.666258 |
| syntcomp24 | `load_balancer6.ltl` | acacia_slow | M2 downset | 1164 | 352 | 20 | 2911 | 312 | attempted | 0.494368 | 0.245991 |
| syntcomp24 | `ltl2dba_theta10.ltl` | acacia_slow | M1 letter-loop | 33792 | 22 | 256 | 51 | 413 | no indexed input AP families; 0/0 star gens | 0.627543 | 0.035879 |
| syntcomp24 | `ltl2dba_theta12.ltl` | acacia_slow | M1 letter-loop | 159744 | 26 | 192 | 59 | 2204 | no indexed input AP families; 0/0 star gens | 3.308119 | 0.097762 |
| syntcomp24 | `ltl2dba_theta14.ltl` | ltlsynt_only | M1 letter-loop | 262144 | 20 | 96 | 67 | 13996 | no indexed input AP families; 0/0 star gens | 17.064250 | 0.384453 |
| syntcomp24 | `ltl2dba_theta16.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.053773 | 2.350942 |
| syntcomp24 | `ltl2dpa13.ltl` | acacia_slow | M2 downset | 49020 | 105 | 12256 | 52 | 3 | no indexed input AP families; 0/0 star gens | 0.452927 | 0.022364 |
| syntcomp24 | `ltl2dpa19.ltl` | acacia_slow | M2 downset | 3320 | 1920 | 416 | 96 | 11 | no indexed input AP families; 0/0 star gens | 0.636357 | 0.022258 |
| syntcomp24 | `prioritized_arbiter10.ltl` | ltlsynt_only | M2 downset | 4759762 | 5761 | 2379881 | 65 | 5 | no indexed input AP families; 0/0 star gens | 17.027434 | 0.019669 |
| syntcomp24 | `prioritized_arbiter12.ltl` | ltlsynt_only | M2 downset | 5706500 | 145 | 2853250 | 77 | 20 | no indexed input AP families; 0/0 star gens | 17.029926 | 0.022488 |
| syntcomp24 | `prioritized_arbiter6.ltl` | acacia_slow | M2 downset | 1401598 | 1441 | 700800 | 41 | 1 | no indexed input AP families; 0/0 star gens | 2.782279 | 0.015582 |
| syntcomp24 | `prioritized_arbiter7.ltl` | ltlsynt_only | M2 downset | 3329314 | 1051 | 1664657 | 47 | 1 | no indexed input AP families; 0/0 star gens | 17.028313 | 0.015556 |
| syntcomp24 | `prioritized_arbiter8.ltl` | ltlsynt_only | M2 downset | 3806130 | 2017 | 1903065 | 53 | 2 | no indexed input AP families; 0/0 star gens | 17.032713 | 0.016318 |
| syntcomp24 | `prioritized_arbiter9.ltl` | ltlsynt_only | M2 downset | 4282946 | 3529 | 2141473 | 59 | 3 | no indexed input AP families; 0/0 star gens | 17.027508 | 0.016690 |
| syntcomp24 | `prioritized_arbiter_enc10.ltl` | ltlsynt_only | M1 letter-loop | 128 | 1344 | 8 | 94723 | 10672 | no usable block layout | 17.034196 | 6.931961 |
| syntcomp24 | `prioritized_arbiter_enc8.ltl` | ltlsynt_only | M2 downset | 128 | 1567 | 16 | 18102 | 1100 | too many automaton states | 17.044425 | 0.533335 |
| syntcomp24 | `prioritized_arbiter_unreal2100.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 73 | not run | 17.053673 | 0.099396 |
| syntcomp24 | `prioritized_arbiter_unreal230.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 7 | not run | 17.058814 | 0.022514 |
| syntcomp24 | `prioritized_arbiter_unreal260.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 24 | not run | 17.041320 | 0.039549 |
| syntcomp24 | `robot_grid2_2.ltl` | ltlsynt_only | M2 downset | 512 | 9383 | 32 | 1185 | 330 | too few indexed clients; too many automaton states | 17.029659 | 0.660307 |
| syntcomp24 | `round_robin_arbiter4.ltl` | acacia_slow | M2 downset | 14000 | 7154 | 875 | 453 | 18 | 0/0 star gens | 7.726195 | 0.038743 |
| syntcomp24 | `round_robin_arbiter5.ltl` | ltlsynt_only | M1 letter-loop | 40928 | 60 | 1279 | 1705 | 159 | 0/0 star gens; too many automaton states | 17.037560 | 0.241399 |
| syntcomp24 | `round_robin_arbiter6.ltl` | ltlsynt_only | M1 letter-loop | 114176 | 72 | 1784 | 6331 | 1763 | 0/0 star gens; too many automaton states | 17.114720 | 2.744774 |
| syntcomp24 | `simple_arbiter_enc10.ltl` | ltlsynt_only | M1 letter-loop | 64 | 166 | 4 | 71168 | 6765 | no usable block layout | 17.029535 | 10.610229 |
| syntcomp24 | `simple_arbiter_enc8.ltl` | ltlsynt_only | M1 letter-loop | 41664 | 64 | 5208 | 5888 | 260 | too many automaton states | 17.028397 | 0.179436 |
| syntcomp24 | `simple_arbiter_unreal225.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 3 | not run | 17.058568 | 0.025882 |
| syntcomp24 | `simple_arbiter_unreal250.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 10 | not run | 17.059619 | 0.038791 |
| syntcomp24 | `simple_arbiter_unreal260.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 14 | not run | 17.060334 | 0.047126 |
| syntcomp24 | `simple_arbiter_unreal275.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 22 | not run | 17.059304 | 0.056934 |
| syntcomp24 | `simple_arbiter_with_hints10.ltl` | ltlsynt_only | M1 letter-loop | 8192 | 103 | 8 | 6153 | 8501 | 0/0 star gens | 17.181815 | 7.875966 |
| syntcomp24 | `simple_arbiter_with_hints6.ltl` | acacia_slow | M2 downset | 224960 | 3396 | 3515 | 804 | 47 | 0/0 star gens; too many automaton states | 12.384362 | 0.045022 |
| syntcomp24 | `simple_arbiter_with_hints8.ltl` | ltlsynt_only | M4 one-sided-race | 2048 | 67 | 8 | 1607 | 1210 | 0/0 star gens | 17.036781 | 0.323517 |
| syntcomp24 | `tmp_13cfc6f2.ltl` | ltlsynt_only | M1 letter-loop | 1482720 | 614 | 46336 | 566 | 465 | no indexed AP families; too many automaton states | 17.015161 | 1.036507 |
| syntcomp24 | `workstation_resupply_3.ltl` | ltlsynt_only | M1 letter-loop | 93552 | 1683 | 11694 | 169 | 244 | no usable block layout; 0/0 star gens | 17.031463 | 0.189808 |
| syntcomp24 | `workstation_resupply_4.ltl` | ltlsynt_only | M1 letter-loop | 23648 | 1749 | 8 | 389 | 3250 | 0/0 star gens | 17.083234 | 1.879784 |
| syntcomp25 | `Automata32S.ltl` | ltlsynt_only | M4 one-sided-race | 0 | 0 | 0 | 68 | 5 | not run | 17.061691 | 0.026567 |
| syntcomp25 | `F-G-contradiction-111.ltl` | ltlsynt_only | M1 letter-loop | 131072 | 1024 | 17 | 10226 | 5369 | no indexed input AP families; too many automaton states | 17.117163 | 0.069245 |
| syntcomp25 | `F-G-contradiction-13.ltl` | acacia_slow | mixed | 20400 | 4096 | 69 | 557 | 46 | no indexed input AP families; too many automaton states | 0.665113 | 0.024803 |
| syntcomp25 | `F-G-contradiction-14.ltl` | acacia_slow | mixed | 32956 | 4096 | 108 | 649 | 47 | no indexed input AP families; too many automaton states | 1.405390 | 0.028269 |
| syntcomp25 | `GF-G-contradiction2.ltl` | acacia_slow | M1 letter-loop | 35280 | 768 | 85 | 317 | 40 | too few indexed clients | 1.588270 | 0.029109 |
| syntcomp25 | `GF-G-contradiction4.ltl` | ltlsynt_only | M1 letter-loop | 82120 | 4335 | 18 | 1922 | 725 | too few indexed clients; too many automaton states | 17.072921 | 0.100188 |
| syntcomp25 | `GF-G-contradiction6.ltl` | ltlsynt_only | M1 letter-loop | 0 | 0 | 0 | 119 | 13131 | too few indexed clients | 17.037886 | 4.548886 |
| syntcomp25 | `LedMatrix.ltl` | ltlsynt_only | M3 translation-stall | 1006 | 1 | 302 | 775 | 6928 | no indexed AP families | 17.061334 | 0.363401 |
| syntcomp25 | `OneCounterGuiA8.ltl` | acacia_slow | M1 letter-loop | 1345932 | 242 | 6867 | 67 | 3404 | no indexed AP families | 3.265683 | 0.025476 |
| syntcomp25 | `TwoCounters3.ltl` | acacia_slow | M2 downset | 31714 | 1 | 101 | 26 | 1589 | no indexed AP families | 1.407102 | 0.023237 |
| syntcomp25 | `amba_case_study_unreal_pb_2_pe_.ltl` | ltlsynt_only | M1 letter-loop | 1008512 | 152 | 4653 | 700 | 704 | too few indexed clients | 17.029116 | 9.854469 |
| syntcomp25 | `amba_decomposed_lock_pb_12_pe_.ltl` | acacia_slow | M2 downset | 16777216 | 1 | 26 | 10 | 3 | no indexed input AP families | 7.398904 | 0.023731 |
| syntcomp25 | `amba_decomposed_lock_pb_13_pe_.ltl` | ltlsynt_only | M2 downset | 46727168 | 1 | 32 | 10 | 8 | no indexed input AP families | 17.167597 | 0.023889 |
| syntcomp25 | `amba_decomposed_lock_pb_16_pe_.ltl` | ltlsynt_only | M1 letter-loop | 0 | 0 | 0 | 10 | 272 | no indexed input AP families | 17.113447 | 0.102793 |
| syntcomp25 | `arbiter_pb_5_pe_.ltl` | acacia_slow | M1 letter-loop | 64480 | 743 | 2015 | 353 | 27 | 0/0 star gens | 0.868379 | 0.041961 |
| syntcomp25 | `arbiter_pb_6_pe_.ltl` | ltlsynt_only | M2 downset | 174144 | 2439 | 2721 | 929 | 174 | too many automaton states | 17.036728 | 0.131722 |
| syntcomp25 | `arbiter_with_buffer_pb_5_pe_.ltl` | ltlsynt_only | M2 downset | 9216 | 3872 | 10 | 497 | 50 | low block payoff | 17.032543 | 0.173940 |
| syntcomp25 | `arbiter_with_cancel_pb_5_pe_.ltl` | acacia_slow | M2 downset | 209709 | 1043 | 864 | 433 | 628 | 0/0 star gens | 2.696421 | 0.202399 |
| syntcomp25 | `arbiter_with_cancel_pb_6_pe_.ltl` | ltlsynt_only | M2 downset | 512 | 1179 | 8 | 1120 | 8087 | attempted | 17.035824 | 1.729422 |
| syntcomp25 | `arbiter_with_cancel_pb_7_pe_.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.042600 | 16.752201 |
| syntcomp25 | `chain-simple-30-real.ltl` | acacia_slow | mixed | 6 | 1 | 7 | 2046 | 267 | no indexed input AP families; too many automaton states | 0.410429 | 0.071235 |
| syntcomp25 | `chain-simple-70-real.ltl` | acacia_slow | M2 downset | 6 | 1 | 7 | 4606 | 715 | no indexed input AP families; too many automaton states | 1.103868 | 0.131021 |
| syntcomp25 | `chain-simple-param-70-real.ltl` | acacia_slow | M1 letter-loop | 63 | 144 | 32 | 1039 | 1186 | no indexed input AP families; too many automaton states | 1.149078 | 0.032899 |
| syntcomp25 | `chomp_pb_2_3_pe_.ltl` | acacia_slow | M1 letter-loop | 14690 | 2 | 1256 | 47 | 3568 | too few indexed clients | 3.351960 | 0.021970 |
| syntcomp25 | `chomp_pb_3_4_pe_.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.054780 | 0.120549 |
| syntcomp25 | `evasion0.ltl` | ltlsynt_only | mixed | 40000 | 2902 | 17 | 8741 | 14872 | no indexed input AP families; too many automaton states | 17.047869 | 0.048795 |
| syntcomp25 | `f-real-real.ltl` | ltlsynt_only | M1 letter-loop | 1070280 | 190 | 992 | 56 | 22 | no indexed input AP families | 17.031899 | 0.033671 |
| syntcomp25 | `follow2.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.043569 | 2.289294 |
| syntcomp25 | `follow3.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.064103 | 2.886440 |
| syntcomp25 | `g-real0.ltl` | acacia_slow | M1 letter-loop | 49152 | 847 | 6754 | 280 | 24 | too few indexed clients | 6.005981 | 0.036740 |
| syntcomp25 | `g-unreal-1-unreal.ltl` | ltlsynt_only | M1 letter-loop | 131072 | 512 | 110 | 957 | 48 | no indexed input AP families | 17.029396 | 0.036561 |
| syntcomp25 | `g-unreal-111.ltl` | acacia_slow | M1 letter-loop | 86336 | 264 | 143 | 529 | 26 | no indexed input AP families | 3.939082 | 0.028720 |
| syntcomp25 | `g-unreal-116.ltl` | ltlsynt_only | M1 letter-loop | 148480 | 452 | 128 | 749 | 40 | no indexed input AP families | 17.029019 | 0.033895 |
| syntcomp25 | `g-unreal-117.ltl` | ltlsynt_only | M1 letter-loop | 149504 | 464 | 128 | 793 | 44 | no indexed input AP families | 17.028147 | 0.034777 |
| syntcomp25 | `g-unreal-19.ltl` | acacia_slow | M1 letter-loop | 66896 | 240 | 114 | 441 | 21 | no indexed input AP families | 2.502453 | 0.027151 |
| syntcomp25 | `g-unreal-32.ltl` | acacia_slow | M1 letter-loop | 44100 | 1024 | 106 | 315 | 40 | too few indexed clients | 3.520284 | 0.026034 |
| syntcomp25 | `gf-unreal21.ltl` | wrapper_artifact | resolved empty-side partition | 0 | 0 | 0 | 0 | 4 | not run | 0.010 | 0.024979 |
| syntcomp25 | `gf-unreal28.ltl` | wrapper_artifact | resolved empty-side partition | 0 | 0 | 0 | 0 | 5 | not run | 0.010 | 0.026769 |
| syntcomp25 | `gf-unreal37.ltl` | wrapper_artifact | resolved empty-side partition | 0 | 0 | 0 | 0 | 8 | not run | 0.015 | 0.032028 |
| syntcomp25 | `gf-unreal46.ltl` | wrapper_artifact | resolved empty-side partition | 0 | 0 | 0 | 0 | 10 | not run | 0.017 | 0.033605 |
| syntcomp25 | `heim-buechi-real.ltl` | acacia_slow | M1 letter-loop | 32768 | 318 | 31 | 3244 | 1828 | no indexed input AP families; too many automaton states | 2.046293 | 0.029513 |
| syntcomp25 | `heim-double-x1.ltl` | acacia_slow | M1 letter-loop | 32768 | 1396 | 2357 | 253 | 68 | no indexed input AP families; too few indexed clients | 3.233697 | 0.024450 |
| syntcomp25 | `heim-double-x11.ltl` | ltlsynt_only | M2 downset | 49152 | 1777 | 5209 | 1629 | 467 | no indexed input AP families; too many automaton states | 17.057388 | 0.031418 |
| syntcomp25 | `heim-double-x2.ltl` | acacia_slow | M1 letter-loop | 24576 | 1629 | 2493 | 349 | 77 | no indexed input AP families; too few indexed clients | 2.926439 | 0.024412 |
| syntcomp25 | `heim-double-x3.ltl` | acacia_slow | M1 letter-loop | 65536 | 1771 | 3069 | 457 | 110 | no indexed input AP families; too few indexed clients | 8.126814 | 0.024657 |
| syntcomp25 | `heim-double-x9.ltl` | ltlsynt_only | M2 downset | 49152 | 1777 | 4665 | 1309 | 399 | no indexed input AP families; too many automaton states | 17.057131 | 0.030989 |
| syntcomp25 | `infinite-race-u2.ltl` | acacia_slow | M1 letter-loop | 111952 | 3072 | 32 | 1519 | 396 | no indexed input AP families; too many automaton states | 1.998018 | 0.030229 |
| syntcomp25 | `infinite-race-u24.ltl` | ltlsynt_only | M1 letter-loop | 90371 | 1883 | 384 | 10414 | 3198 | no indexed input AP families; too many automaton states | 17.079344 | 0.100237 |
| syntcomp25 | `infinite-race-u3.ltl` | acacia_slow | M1 letter-loop | 131072 | 285 | 36 | 1874 | 461 | no indexed input AP families; too many automaton states | 2.408999 | 0.032547 |
| syntcomp25 | `infinite-race-u5.ltl` | acacia_slow | M1 letter-loop | 262144 | 597 | 64 | 2833 | 660 | no indexed input AP families; too many automaton states | 15.088322 | 0.039769 |
| syntcomp25 | `infinite-race-unequal-22.ltl` | acacia_slow | mixed | 108736 | 2688 | 416 | 964 | 327 | no indexed input AP families; too many automaton states | 2.521227 | 0.032717 |
| syntcomp25 | `infinite-race-unequal-26.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.049490 | 4.556982 |
| syntcomp25 | `lift_gr1_pb_3_pe_.ltl` | ltlsynt_only | mixed | 4105200 | 9066 | 256576 | 225 | 22 | too few indexed clients | 17.030876 | 0.086468 |
| syntcomp25 | `lift_pb_3_pe_.ltl` | ltlsynt_only | M2 downset | 1528 | 14060 | 192 | 169 | 10 | too few indexed clients | 17.030066 | 0.026541 |
| syntcomp25 | `load_balancer_unreal1_pb_6_4_pe_.ltl` | ltlsynt_only | M3 translation-stall | 14284 | 245 | 237 | 214 | 714 | 0/0 star gens | 17.057717 | 16.617502 |
| syntcomp25 | `ltl2dba_C2_unreal_pb_14_pe_.ltl` | acacia_slow | M2 downset | 1470 | 2 | 736 | 50 | 2516 | no indexed input AP families; 0/0 star gens | 3.296036 | 0.144633 |
| syntcomp25 | `ltl2dba_C2_unreal_pb_15_pe_.ltl` | acacia_slow | M2 downset | 1726 | 2 | 864 | 53 | 6803 | no indexed input AP families; 0/0 star gens | 7.835383 | 0.305850 |
| syntcomp25 | `ltl2dba_C2_unreal_pb_16_pe_.ltl` | ltlsynt_only | M2 downset | 1278 | 2 | 640 | 56 | 16508 | no indexed input AP families; 0/0 star gens | 17.073740 | 0.674709 |
| syntcomp25 | `ltl2dba_theta_pb_12_pe_.ltl` | acacia_slow | M1 letter-loop | 159744 | 26 | 160 | 59 | 2363 | no indexed input AP families; 0/0 star gens | 3.804407 | 0.109182 |
| syntcomp25 | `ltl2dba_theta_pb_14_pe_.ltl` | ltlsynt_only | M1 letter-loop | 64 | 15 | 32 | 67 | 16017 | no indexed input AP families; 0/0 star gens | 17.078809 | 0.412299 |
| syntcomp25 | `ltl2dba_theta_pb_16_pe_.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.051809 | 2.392561 |
| syntcomp25 | `ltl2dpa19.ltl` | acacia_slow | M2 downset | 3320 | 1920 | 416 | 96 | 11 | no indexed input AP families; 0/0 star gens | 0.690225 | 0.029384 |
| syntcomp25 | `patrolling-alarm17.ltl` | acacia_slow | M1 letter-loop | 301716 | 498 | 298 | 702 | 325 | too few indexed clients; too many automaton states | 6.709105 | 0.048826 |
| syntcomp25 | `patrolling-alarm21.ltl` | acacia_slow | M1 letter-loop | 450000 | 634 | 416 | 846 | 434 | too few indexed clients; too many automaton states | 15.462324 | 0.048276 |
| syntcomp25 | `patrolling-alarm23.ltl` | ltlsynt_only | M1 letter-loop | 410832 | 4096 | 352 | 918 | 422 | too few indexed clients; too many automaton states | 17.035185 | 0.050930 |
| syntcomp25 | `patrolling10.ltl` | acacia_slow | M1 letter-loop | 92040 | 256 | 160 | 450 | 114 | too few indexed clients | 1.145336 | 0.035461 |
| syntcomp25 | `patrolling22.ltl` | ltlsynt_only | M1 letter-loop | 406224 | 406 | 288 | 882 | 415 | too few indexed clients; too many automaton states | 17.035067 | 0.058892 |
| syntcomp25 | `patrolling9.ltl` | acacia_slow | M1 letter-loop | 91064 | 172 | 128 | 414 | 100 | too few indexed clients | 1.033471 | 0.033734 |
| syntcomp25 | `prioritized_arbiter_enc_pb_10_pe_.ltl` | ltlsynt_only | M1 letter-loop | 128 | 1344 | 8 | 94723 | 12072 | no usable block layout | 17.035171 | 7.588421 |
| syntcomp25 | `prioritized_arbiter_pb_6_pe_.ltl` | acacia_slow | M2 downset | 1325694 | 1441 | 662848 | 41 | 1 | no indexed input AP families; 0/0 star gens | 3.050204 | 0.021662 |
| syntcomp25 | `prioritized_arbiter_pb_8_pe_.ltl` | ltlsynt_only | M2 downset | 3806130 | 2017 | 1903065 | 53 | 2 | no indexed input AP families; 0/0 star gens | 17.021647 | 0.020375 |
| syntcomp25 | `prioritized_arbiter_unreal2_pb_60_pe_.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 29 | not run | 17.056806 | 0.042097 |
| syntcomp25 | `reversible-lane-u-unreal.ltl` | acacia_slow | M1 letter-loop | 155512 | 360 | 30 | 51 | 81 | no indexed input AP families | 9.784350 | 0.287172 |
| syntcomp25 | `reversible-lane-u0.ltl` | acacia_slow | M1 letter-loop | 142008 | 150 | 27 | 45 | 88 | no indexed input AP families | 3.582161 | 0.186739 |
| syntcomp25 | `reversible-lane-u1.ltl` | acacia_slow | M1 letter-loop | 145204 | 231 | 28 | 48 | 76 | no indexed input AP families | 5.570724 | 0.237359 |
| syntcomp25 | `robot-cat-real-1d-real.ltl` | acacia_slow | M1 letter-loop | 47168 | 1296 | 41 | 2201 | 1905 | no indexed input AP families; too many automaton states | 4.017092 | 0.038584 |
| syntcomp25 | `robot-cat-unreal-1d-unreal.ltl` | acacia_slow | M1 letter-loop | 26316 | 996 | 800 | 2677 | 2116 | no indexed input AP families; too many automaton states | 2.703307 | 0.038199 |
| syntcomp25 | `robot-grid-commute-1d-real.ltl` | acacia_slow | M1 letter-loop | 17548 | 509 | 13 | 3610 | 1594 | no indexed input AP families; too many automaton states | 2.109692 | 0.033088 |
| syntcomp25 | `robot-resource-1d-unreal.ltl` | acacia_slow | M1 letter-loop | 176982 | 614 | 1529 | 1576 | 214 | no indexed input AP families; too many automaton states | 12.932279 | 0.027934 |
| syntcomp25 | `robot-resource-1d1.ltl` | acacia_slow | M1 letter-loop | 29794 | 323 | 985 | 1062 | 131 | no indexed input AP families; too many automaton states | 1.066614 | 0.025385 |
| syntcomp25 | `robot-resource-2d0.ltl` | acacia_slow | M1 letter-loop | 131072 | 422 | 24 | 45 | 28 | no indexed input AP families | 7.036927 | 0.031396 |
| syntcomp25 | `robot-resource-2d1.ltl` | ltlsynt_only | M1 letter-loop | 178784 | 765 | 16 | 50 | 51 | no indexed input AP families | 17.045481 | 0.037717 |
| syntcomp25 | `robot-resource-2d17.ltl` | ltlsynt_only | M1 letter-loop | 0 | 0 | 0 | 123 | 7974 | no indexed input AP families | 17.049000 | 0.104649 |
| syntcomp25 | `robot-to-target-charging-real.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.064114 | 0.078990 |
| syntcomp25 | `robot-to-target-charging19.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.053923 | 0.070020 |
| syntcomp25 | `robot-to-target0.ltl` | ltlsynt_only | M1 letter-loop | 0 | 0 | 0 | 15461 | 16171 | not run | 17.042551 | 1.205307 |
| syntcomp25 | `robot_repair0.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.063324 | 0.093719 |
| syntcomp25 | `robot_repair1.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.052375 | 0.407133 |
| syntcomp25 | `robot_running-real.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.064240 | 0.482079 |
| syntcomp25 | `round_robin_arbiter_pb_6_pe_.ltl` | ltlsynt_only | M1 letter-loop | 114176 | 72 | 1784 | 6331 | 2106 | 0/0 star gens; too many automaton states | 17.098640 | 2.949549 |
| syntcomp25 | `simple_arbiter_unreal2_pb_25_pe_.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 3 | not run | 17.065068 | 0.025589 |
| syntcomp25 | `simple_arbiter_unreal2_pb_50_pe_.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 10 | not run | 17.042614 | 0.034707 |
| syntcomp25 | `simple_arbiter_unreal2_pb_60_pe_.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 16 | not run | 17.061151 | 0.043312 |
| syntcomp25 | `simple_arbiter_unreal2_pb_75_pe_.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 22 | not run | 17.057081 | 0.060483 |
| syntcomp25 | `simple_arbiter_with_hints_pb_10_pe_.ltl` | ltlsynt_only | M1 letter-loop | 8192 | 103 | 8 | 6153 | 10342 | 0/0 star gens | 17.166705 | 8.507592 |
| syntcomp25 | `sort40.ltl` | acacia_slow | M1 letter-loop | 57258 | 384 | 8 | 78 | 170 | too few indexed clients | 2.679675 | 0.042922 |
| syntcomp25 | `sort51.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 137 | 7728 | too few indexed clients | 17.058729 | 0.372187 |
| syntcomp25 | `square5x51.ltl` | ltlsynt_only | M1 letter-loop | 42624 | 1056 | 8 | 270 | 1248 | no indexed input AP families | 17.081451 | 0.093111 |
| syntcomp25 | `tasks-unreal0.ltl` | ltlsynt_only | M3 translation-stall | 56055 | 4645 | 16 | 105 | 304 | too few indexed clients | 17.068459 | 0.073626 |
| syntcomp25 | `taxi-service-u-unreal.ltl` | acacia_slow | M1 letter-loop | 16480 | 392 | 6 | 106 | 55 | no indexed input AP families | 1.452662 | 0.045052 |
| syntcomp25 | `thermostat-F-real.ltl` | ltlsynt_only | M1 letter-loop | 149504 | 1044 | 1659 | 1569 | 1478 | no indexed input AP families; too many automaton states | 17.034986 | 0.034791 |
| syntcomp25 | `thermostat-GF-unreal2.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 119 | 1277 | not run | 17.031730 | 1.500196 |
| syntcomp25 | `tmp_13cfc6f2.ltl` | ltlsynt_only | M1 letter-loop | 1381344 | 614 | 43168 | 566 | 556 | no indexed AP families; too many automaton states | 17.033455 | 1.091749 |
| syntcomp25 | `unordered-visits-charging0.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 147 | 250 | too few indexed clients | 17.049591 | 2.750413 |
| syntcomp25 | `workstation_resupply_pb_3_pe_.ltl` | ltlsynt_only | M1 letter-loop | 93552 | 1683 | 11694 | 169 | 291 | no usable block layout; 0/0 star gens | 17.033066 | 0.194710 |
| syntcomp26 | `F-G-contradiction-111.ltl` | ltlsynt_only | M1 letter-loop | 131072 | 1024 | 17 | 10226 | 6184 | no indexed input AP families; too many automaton states | 17.107910 | 0.071184 |
| syntcomp26 | `F-G-contradiction-15.ltl` | acacia_slow | mixed | 35708 | 8192 | 114 | 721 | 58 | no indexed input AP families; too many automaton states | 1.734967 | 0.032692 |
| syntcomp26 | `GF-G-contradiction2.ltl` | acacia_slow | M1 letter-loop | 35280 | 768 | 85 | 317 | 42 | too few indexed clients | 1.810802 | 0.033873 |
| syntcomp26 | `KitchenTimerV9.ltl` | acacia_slow | mixed | 16832 | 3504 | 1052 | 146 | 306 | no indexed AP families | 0.555099 | 0.027855 |
| syntcomp26 | `OneCounter.ltl` | acacia_slow | M1 letter-loop | 2608760 | 6 | 6656 | 67 | 3021 | no indexed AP families | 2.764552 | 0.030864 |
| syntcomp26 | `TwoCounters3.ltl` | acacia_slow | M2 downset | 31714 | 1 | 101 | 26 | 1738 | no indexed AP families | 1.544448 | 0.021834 |
| syntcomp26 | `TwoCountersDisButA9.ltl` | acacia_slow | mixed | 2040 | 1 | 3 | 55 | 200 | no indexed AP families | 0.619565 | 0.020985 |
| syntcomp26 | `amba_decomposed_lock_pb_11_pe_.ltl` | acacia_slow | M2 downset | 2195456 | 1 | 8 | 10 | 3 | no indexed input AP families | 1.771903 | 0.022861 |
| syntcomp26 | `amba_decomposed_lock_pb_12_pe_.ltl` | acacia_slow | mixed | 20062208 | 1 | 32 | 10 | 3 | no indexed input AP families | 7.718522 | 0.022530 |
| syntcomp26 | `amba_decomposed_lock_pb_13_pe_.ltl` | ltlsynt_only | mixed | 33554432 | 1 | 23 | 10 | 7 | no indexed input AP families | 17.159195 | 0.024027 |
| syntcomp26 | `arbiter_pb_5_pe_.ltl` | acacia_slow | M1 letter-loop | 64480 | 743 | 2015 | 353 | 28 | 0/0 star gens | 0.845620 | 0.042510 |
| syntcomp26 | `arbiter_pb_7_pe_.ltl` | ltlsynt_only | M2 downset | 1024 | 2187 | 8 | 2368 | 1291 | attempted | 17.043984 | 0.696288 |
| syntcomp26 | `arbiter_with_cancel_pb_7_pe_.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.048722 | 16.795657 |
| syntcomp26 | `box-real.ltl` | ltlsynt_only | M1 letter-loop | 922488 | 4096 | 6784 | 300 | 21 | no indexed input AP families; too few indexed clients | 17.030141 | 0.028153 |
| syntcomp26 | `chain-simple-30-real.ltl` | acacia_slow | M2 downset | 6 | 1 | 7 | 2046 | 278 | no indexed input AP families; too many automaton states | 0.438382 | 0.067689 |
| syntcomp26 | `chain-simple-50-real.ltl` | acacia_slow | M2 downset | 6 | 1 | 7 | 3326 | 476 | no indexed input AP families; too many automaton states | 0.692692 | 0.095998 |
| syntcomp26 | `chomp_pb_2_5_pe_.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.057988 | 0.044651 |
| syntcomp26 | `chomp_pb_3_2_pe_.ltl` | acacia_slow | M1 letter-loop | 15566 | 2 | 1256 | 47 | 3733 | too few indexed clients | 3.353340 | 0.026081 |
| syntcomp26 | `evasion-real.ltl` | ltlsynt_only | M1 letter-loop | 86528 | 1586 | 32 | 171 | 285 | no indexed input AP families | 17.049531 | 0.058459 |
| syntcomp26 | `evasion0.ltl` | ltlsynt_only | mixed | 40000 | 2902 | 16 | 8741 | 16039 | no indexed input AP families; too many automaton states | 17.049184 | 0.050702 |
| syntcomp26 | `g-real0.ltl` | acacia_slow | M1 letter-loop | 49152 | 847 | 6754 | 280 | 23 | too few indexed clients | 6.117231 | 0.038433 |
| syntcomp26 | `g-unreal-1-unreal.ltl` | ltlsynt_only | M1 letter-loop | 131072 | 512 | 110 | 957 | 55 | no indexed input AP families | 17.030941 | 0.037323 |
| syntcomp26 | `g-unreal-113.ltl` | acacia_slow | M1 letter-loop | 108576 | 288 | 175 | 617 | 31 | no indexed input AP families | 6.197572 | 0.030614 |
| syntcomp26 | `g-unreal-18.ltl` | acacia_slow | M1 letter-loop | 51976 | 228 | 90 | 397 | 19 | no indexed input AP families | 1.716537 | 0.029387 |
| syntcomp26 | `gf-unreal20.ltl` | wrapper_artifact | resolved empty-side partition | 0 | 0 | 0 | 0 | 4 | not run | 0.010 | 0.026052 |
| syntcomp26 | `gf-unreal46.ltl` | wrapper_artifact | resolved empty-side partition | 0 | 0 | 0 | 0 | 9 | not run | 0.016 | 0.034561 |
| syntcomp26 | `heim-double-x-real.ltl` | ltlsynt_only | M2 downset | 49152 | 1777 | 1795 | 669 | 407 | no indexed input AP families; too many automaton states | 17.055654 | 0.034639 |
| syntcomp26 | `heim-double-x1.ltl` | acacia_slow | M1 letter-loop | 32768 | 1396 | 2357 | 253 | 67 | no indexed input AP families; too few indexed clients | 3.014632 | 0.025106 |
| syntcomp26 | `helipad-real.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 110 | 512 | too few indexed clients | 17.058163 | 0.818163 |
| syntcomp26 | `infinite-race-u3.ltl` | acacia_slow | M1 letter-loop | 131072 | 285 | 36 | 1874 | 487 | no indexed input AP families; too many automaton states | 2.275215 | 0.034992 |
| syntcomp26 | `infinite-race-unequal-22.ltl` | acacia_slow | mixed | 108736 | 2688 | 416 | 964 | 323 | no indexed input AP families; too many automaton states | 2.559135 | 0.034288 |
| syntcomp26 | `lift_pb_6_pe_.ltl` | ltlsynt_only | M2 downset | 2048 | 1310 | 32 | 5550 | 2538 | too many automaton states | 17.078465 | 0.682833 |
| syntcomp26 | `load_balancer_unreal1_pb_6_4_pe_.ltl` | ltlsynt_only | M3 translation-stall | 14284 | 245 | 237 | 214 | 722 | 0/0 star gens | 17.063187 | 16.959755 |
| syntcomp26 | `ltl2dba_C2_unreal_pb_12_pe_.ltl` | acacia_slow | M2 downset | 1150 | 2 | 576 | 44 | 386 | no indexed input AP families; 0/0 star gens | 0.565276 | 0.054929 |
| syntcomp26 | `ltl2dba_theta_pb_12_pe_.ltl` | acacia_slow | M1 letter-loop | 159744 | 26 | 160 | 59 | 2412 | no indexed input AP families; 0/0 star gens | 3.800093 | 0.099975 |
| syntcomp26 | `ltl2dba_theta_pb_16_pe_.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.051093 | 2.770736 |
| syntcomp26 | `patrolling-alarm21.ltl` | acacia_slow | M1 letter-loop | 450000 | 634 | 416 | 846 | 436 | too few indexed clients; too many automaton states | 15.556283 | 0.043808 |
| syntcomp26 | `patrolling-alarm7.ltl` | acacia_slow | M1 letter-loop | 34796 | 4096 | 128 | 342 | 83 | too few indexed clients | 0.396328 | 0.028583 |
| syntcomp26 | `patrolling11.ltl` | acacia_slow | M1 letter-loop | 123120 | 216 | 160 | 486 | 122 | too few indexed clients | 1.659265 | 0.031604 |
| syntcomp26 | `patrolling15.ltl` | acacia_slow | M1 letter-loop | 298948 | 4096 | 260 | 630 | 287 | too few indexed clients; too many automaton states | 6.536530 | 0.052954 |
| syntcomp26 | `prioritized_arbiter_pb_9_pe_.ltl` | ltlsynt_only | M2 downset | 4282946 | 3529 | 2141473 | 59 | 3 | no indexed input AP families; 0/0 star gens | 17.030674 | 0.023279 |
| syntcomp26 | `reversible-lane-u-unreal.ltl` | acacia_slow | M1 letter-loop | 155512 | 360 | 30 | 51 | 82 | no indexed input AP families | 9.776528 | 0.293364 |
| syntcomp26 | `reversible-lane-u0.ltl` | acacia_slow | M1 letter-loop | 142008 | 150 | 27 | 45 | 78 | no indexed input AP families | 3.248411 | 0.168218 |
| syntcomp26 | `reversible-lane-u1.ltl` | acacia_slow | M1 letter-loop | 145204 | 231 | 28 | 48 | 82 | no indexed input AP families | 5.776598 | 0.224999 |
| syntcomp26 | `robot-cat-unreal-1d-unreal.ltl` | acacia_slow | M1 letter-loop | 26316 | 996 | 928 | 2677 | 2159 | no indexed input AP families; too many automaton states | 2.652753 | 0.038964 |
| syntcomp26 | `robot-grid-commute-1d-real.ltl` | acacia_slow | M1 letter-loop | 17548 | 509 | 13 | 3610 | 1707 | no indexed input AP families; too many automaton states | 2.055884 | 0.034407 |
| syntcomp26 | `robot-grid-reach-2d-real.ltl` | acacia_slow | M1 letter-loop | 55676 | 417 | 32 | 3335 | 2254 | no indexed input AP families; too many automaton states | 2.989942 | 0.032935 |
| syntcomp26 | `robot-resource-1d-unreal.ltl` | acacia_slow | M1 letter-loop | 176982 | 614 | 1529 | 1576 | 205 | no indexed input AP families; too many automaton states | 13.258856 | 0.032433 |
| syntcomp26 | `robot-resource-1d1.ltl` | acacia_slow | M1 letter-loop | 29794 | 323 | 985 | 1062 | 131 | no indexed input AP families; too many automaton states | 1.021671 | 0.028007 |
| syntcomp26 | `robot-to-target-charging-real.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.062466 | 0.090106 |
| syntcomp26 | `robot-to-target-charging6.ltl` | ltlsynt_only | M1 letter-loop | 0 | 0 | 0 | 105 | 6783 | no indexed input AP families | 17.040560 | 0.046856 |
| syntcomp26 | `robot_collect_samples_v1-real.ltl` | ltlsynt_only | M1 letter-loop | 0 | 0 | 0 | 91 | 7740 | no indexed input AP families | 17.054781 | 0.060377 |
| syntcomp26 | `robot_deliver_products_1-real.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.061895 | 2.191520 |
| syntcomp26 | `robot_repair2.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.061867 | 0.431041 |
| syntcomp26 | `robot_repair5.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.054001 | 10.866213 |
| syntcomp26 | `robot_running-real.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 0 | not run | 17.056070 | 0.462424 |
| syntcomp26 | `scheduler-real.ltl` | ltlsynt_only | M1 letter-loop | 0 | 0 | 0 | 223 | 9957 | not run | 17.042395 | 0.242310 |
| syntcomp26 | `simple_arbiter_unreal2_pb_25_pe_.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 3 | not run | 17.060730 | 0.021867 |
| syntcomp26 | `simple_arbiter_unreal2_pb_75_pe_.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 0 | 24 | not run | 17.055249 | 0.057956 |
| syntcomp26 | `sort40.ltl` | acacia_slow | M1 letter-loop | 57258 | 384 | 8 | 78 | 178 | too few indexed clients | 2.878450 | 0.040358 |
| syntcomp26 | `square5x5-real.ltl` | ltlsynt_only | M2 downset | 65536 | 640 | 6 | 519 | 10332 | no indexed input AP families; too many automaton states | 17.081977 | 0.229203 |
| syntcomp26 | `tasks-unreal1.ltl` | ltlsynt_only | M3 translation-stall | 32768 | 1547 | 5 | 130 | 752 | too few indexed clients | 17.107008 | 0.107700 |
| syntcomp26 | `taxi-service-u-unreal.ltl` | acacia_slow | M1 letter-loop | 16480 | 392 | 6 | 106 | 53 | no indexed input AP families | 1.474394 | 0.048672 |
| syntcomp26 | `thermostat-GF-unreal2.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 119 | 1261 | not run | 17.044012 | 1.551799 |
| syntcomp26 | `tmp_13cfc6f2.ltl` | ltlsynt_only | M1 letter-loop | 1354720 | 614 | 42336 | 566 | 561 | no indexed AP families; too many automaton states | 17.031721 | 1.120885 |
| syntcomp26 | `unordered-visits-charging0.ltl` | ltlsynt_only | M3 translation-stall | 0 | 0 | 0 | 147 | 253 | too few indexed clients | 17.055614 | 2.741578 |
| syntcomp26 | `workstation_resupply_pb_2_pe_.ltl` | acacia_slow | mixed | 23696 | 315 | 5924 | 84 | 22 | too few indexed clients | 0.612807 | 0.037964 |
| syntcomp26 | `workstation_resupply_pb_3_pe_.ltl` | ltlsynt_only | M1 letter-loop | 93552 | 1683 | 11694 | 169 | 279 | no usable block layout; 0/0 star gens | 17.033184 | 0.192184 |
| syntcomp26 | `workstation_resupply_pb_4_pe_.ltl` | ltlsynt_only | M1 letter-loop | 23648 | 1749 | 8 | 389 | 3360 | 0/0 star gens | 17.079396 | 1.939163 |

## Empty-partition wrapper correction

The six `wrapper_artifact` rows above were not solver stalls. The Meson wrapper turned a bare
`.outputs` line into the literal output AP `.outputs` and invoked the binary with that undeclared
name. The corrected parser preserves an empty argument and quotes both partition sides. A sweep of
all 187 empty-side corpus partitions now produces 177 UNREALIZABLE and 10 REALIZABLE verdicts,
with every case below 0.5 s. The CLI also rejects leaked literal `.inputs`/`.outputs` partition
markers with an explicit argument error, while legitimate unused interface APs remain allowed.
When simplification projects away every declared output, the MONA path now handles the empty
output support directly instead of asking BuDDy for the variable of `bddtrue`.

The panel pipeline already passed empty strings correctly, so its measurements give the corrected
campaign headline: SYNTCOMP25 is 106/180 rather than 104/180 (`gf-unreal37` 0.015 s and
`gf-unreal46` 0.017 s), and SYNTCOMP26 is 134/180 rather than 133/180 (`gf-unreal46` 0.016 s).
The three other corrected rows (`gf-unreal21`, `gf-unreal28`, and SYNTCOMP26 `gf-unreal20`) also
fall to about 0.01 s and no longer meet the slow-row definition.

## Confirmed anchor cases

- `syntcomp25/Automata32S.ltl`: the unreal-automaton child terminates with
  `final_reason=spot-fast-path`, `fast_verdict=solved-losing`, 68 states, and 5 ms
  translation while the target still times out: M4.
- `syntcomp24/simple_arbiter_unreal225.ltl`: Spot throws `Too many acceptance sets used. The
  limit is 64.` while translating both unreal workers. The post-simplification formula contains
  an oversized `G` of 301 conjuncts; `translation_pref=any` fails identically, ruling out the
  `small` preference, and no `push_aps` limit is reached. The translation wrapper now records
  `final_reason=translation-acceptance-set-limit` before propagating the exception. For the
  unreal workers, Acacia first tries up to eight sound consequences consisting of the formula's
  syntactic safety core and one liveness obligation. A winning unreal worker now ends with
  `result=solved final_reason=unreal-safety-core-witness total_ms=5`. Release runs solve the
  25/50/60/75-client family UNREALIZABLE in 0.009/0.012/0.014/0.020 s. Each witness is weaker
  than the original formula, so proving a witness unrealizable proves the original unrealizable;
  an inconclusive witness leaves the ordinary solver path unchanged.
- `syntcomp24/prioritized_arbiter10.ltl`: one child reports 4,759,762 actions over
  2,379,881 loops; the real child instead reaches a 5,761-element antichain and is
  interrupted before intersection. This target is classified M2 by the measured aggregate,
  illustrating why family names alone cannot select a mechanism.
- `syntcomp24/robot_grid2_2.ltl`: fixed-point/downset-bound with a 9,383-element peak
  antichain in this shipping diagnostics build.
- `syntcomp26/amba_decomposed_lock_pb_13_pe_.ltl`: 10 automaton states and at least
  33,554,432 actions in one child, but `apply_ms` and `downset_ms` remain within the
  classifier's 20% band, so the instance is conservatively mixed.

## Corrected M3 follow-up

After removing the three empty-partition losses, M3 contains 48 loss rows. The safety-core
witness addresses the 14 `*_arbiter_unreal2*` acceptance-set cases, leaving 34 genuine cases for
the translation experiment (`follow2`, `follow3`, `robot_repair*`, `chomp*`,
`arbiter_with_cancel7`, `ltl2dba_theta16`, and related logical duplicates).

The proposed per-child translation budget is not applicable to the current process architecture:
the real, unreal-formula, and unreal-automaton children are forked before the parent waits, and all
consume the same external wall-clock interval concurrently. Ending one translating child early
cannot transfer time to either sibling; it can only remove that portfolio member. The experiment
therefore stopped before adding a timer that would strictly reduce available work.

The remaining 34 genuine cases were then measured with the existing `small` portfolio and the
proposed `any` race policy. Both modes returned 0/34 answers: all 4 SYNTCOMP24, 17 SYNTCOMP25,
and 13 SYNTCOMP26 cases timed out. Aggregate wall times were effectively identical within each
suite (68.167/68.164 s, 289.786/289.797 s, and 221.665/221.673 s for `small`/`any`). With no gain
and no freed sibling budget, `any` is rejected and the shipping portfolio remains `small`.

Witness attempts now run inside a diagnostics transaction. An inconclusive witness rolls back its
phase timers, `result`, and `final_reason`; a proving witness commits them and is labeled
`unreal-safety-core-witness`. This prevents a discarded speculative formula from masking the main
formula's eventual diagnostic classification.

## M1 whole-letter action quotient spike

M1 cleared the 15-instance census gate, so the fixed spike was run on the three highest-action
distinct corpus files. The prototype canonicalized each concrete output letter to its complete
transition-relation action and retained one copy of each relation. This is not the previously
rejected BDD-path memoization experiment: that experiment attempted to share nodes while
descending a nearly-tree-shaped alphabet BDD, whereas this one quotiented semantically identical
whole-letter actions before repeated CPre application.

| target | baseline actions | quotient actions | reduction |
|---|---:|---:|---:|
| SYNTCOMP24 `tmp_13cfc6f2.ltl` | 1,482,720 | 95,120 | 15.59× |
| SYNTCOMP26 `tmp_13cfc6f2.ltl` | 1,354,720 | 90,000 | 15.05× |
| SYNTCOMP25 `f-real-real.ltl` | 1,070,280 | 75,568 | 14.16× |

All three exceeded the preset 10× spike threshold. The first integration measurement appeared to
lose `syntcomp24/Morning_f2774e0b.ltl`, but that comparison mixed a stale baseline timing and an
incorrect flat corpus lookup. Repeating G1 with the re-frozen shipping baseline and suite source
maps solved 40/40 on both sides: PAR-2 improved from 101.867 s to 87.880 s. The required 51 s
remeasure also solved `Morning_f2774e0b.ltl` with both binaries (12.810/14.027 s).

The corrected G3 panels then passed. SYNTCOMP25 improved from 109/180 solved and PAR-2 2681.692 s
to 111/180 and 2590.800 s: `patrolling-alarm23.ltl` and `patrolling22.ltl` changed from timeout to
UNREALIZABLE. SYNTCOMP26 preserved 134/180 answers while PAR-2 moved from 1707.675 s to
1681.441 s.

The mandatory G2s proxy nevertheless rejected the quotient. On
`syntcomp24/round_robin_arbiter4.ltl`, the same-configuration median rose from 32,306,372,637 to
252,184,896,339 cycles: a 680.604% regression, with all three quotient runs reaching the
60-second cap. This is a decisive landing-gate failure despite the favorable G1 and G3 panels,
so the prototype was removed from the final head.

## Final validation

- G0: all 20 release unit tests, all 14 Posets tests, and all 15 focused Python tests passed.
- G1: 40/40 frozen verdicts; baseline PAR-2 101.867 s, final candidate PAR-2 93.346 s; this is the
  re-validation on the final tree, measured after the partition-validation and MONA empty-output
  fixes; `GATE PASS`.
- G2s: all 60 solver-profile samples completed; geometric improvement 9.15%, worst regression
  -3.08%; `GATE PASS`.
- G3: the final minimum-block + safety-core-witness head, with the rejected quotient removed,
  solved 109/180 on SYNTCOMP25 and 134/180 on SYNTCOMP26. The SYNTCOMP21 critical screen held
  91/94 answers on both sides while aggregate time moved from 114.711 s to 103.355 s.
- G4: 624 labeled realizable/unrealizable tests produced 568 correct answers, 56 allowed timeouts,
  0 failures, and 0 opposite-verdict markers.
- G5: all 1,579 native/converted TLSF comparisons completed with 0 frontend errors and 0 opposite
  verdicts; there were 2 native-only answers, 1 converted-only answer, and 3/2 native/converted
  resource limits; `GATE PASS`. The independent 50-file conversion audit regenerated every pair:
  48/50 formula ASTs matched, with one documented enum-validity divergence and one 600 s
  normalization timeout, and all 49 classifiable I/O-list comparisons matched.
