# Acacia–ltlsynt gap census

This census measures the residual FMCAD'26 gap at the current shipping configuration. It covers every logical `ltlsynt_only` row in the frozen SYNTCOMP24 0s–20s, SYNTCOMP25 panel, and SYNTCOMP26 panel crossovers, plus every `acacia_slow` instance that both tools solve where Acacia takes more than 2× ltlsynt and more than 0.3 s. A later wrapper audit found six rows produced by a malformed empty-side partition; they remain in `gap-census.tsv` as an explicit audit trail but are excluded from the corrected residual counts.

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

The per-instance telemetry now lives in `benchmarking/gap-census.tsv`, with 267 rows covering
suite, instance, set, mechanism, numeric child telemetry, the equivariant decline reason, and the
two solver times.

## Empty-partition wrapper correction

The six rows marked `wrapper_artifact` in `gap-census.tsv` were not solver stalls. The Meson wrapper turned a bare
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

The `any` race policy answered 0/34 and is rejected; the shipping portfolio stays `small`. That
experiment varied the postprocessor preference only. Because `src/solver/create_automaton.hh`
hard-codes `trans.set_type (spot::postprocessor::BA)`, it could not test the census hypothesis
that the gap comes from degeneralizing to Buechi where `ltlsynt` uses a deterministic parity
automaton. That hypothesis was therefore measured directly, outside the solver.

The 34 logical rows resolve to 31 unique corpus files. Each was translated with
`ltl2tgba --ba --small`, `ltl2tgba --tgba --small`, and
`ltl2tgba --deterministic --generic --parity`, on both the formula and its negation, sequentially,
with a 15 s cap and `--stats=%s`. Raw data is kept
outside the tree in `_bm-logs/gap-step7-translation-type/`. Six of these formulas exceed the
128 KiB Linux per-argument limit and must be passed by file; formula size ranges from 210 B to
822,990 B with a median of 58,869 B.

The headline result is that there were zero files where the Buechi request capped while the TGBA
request completed. Requesting `BA` costs nothing over a generalized automaton on this set, so the
degeneralization hypothesis is falsified. Completion counts out of 31 files were: positive
polarity `ba` 0, `tgba` 0, and `parity` 6; negated polarity `ba` 1, `tgba` 1, and `parity` 8.

The 34 rows divide into three groups. Twelve are misclassified. The census telemetry itself
records a translated automaton for them, with 105 to 775 states built in 250 to 7,728 ms, so
translation succeeded and the child died in the fixed point instead. They are `LedMatrix` (both
suites), `sort51`, `tasks-unreal0`, `tasks-unreal1`, `thermostat-GF-unreal2` (both),
`unordered-visits-charging0` (both), `load_balancer_unreal1_pb_6_4_pe_` (both), and
`helipad-real`.

Four rows show a genuine parity advantage: no automaton is built, yet the deterministic parity
route completes with 1 to 130 states while both Buechi and TGBA cap. They are
`syntcomp25/robot-to-target-charging-real`, `syntcomp25/robot-to-target-charging19`,
`syntcomp26/robot-to-target-charging-real`, and `syntcomp26/chomp_pb_2_5_pe_`; `ltlsynt` answers
all four in 0.045 to 0.090 s. Acacia cannot exploit this: the K-bounded counting fixed point is
defined on a universal co-Buechi automaton, so a parity or generalized automaton is not
consumable, and a deterministic result is already claimed by the `spot_fast` short circuit before
the fixed point runs.

The remaining 18 rows are inherent translation hardness: every route caps at 15 s at both
polarities. On five of these `ltlsynt` itself needs more than 10 s: `abcg_arbiter4` takes
16.799 s, `arbiter_with_cancel7` 16.005 s, `arbiter_with_cancel_pb_7_pe_` 16.752 s and
16.796 s, and `robot_repair5` 10.866 s. These clear the 17 s cap only narrowly, so they are not
architectural losses.

The architectural component of M3 is therefore 4 rows, not 48, and it is not actionable within
Acacia's automaton contract. The one remaining lead is `robot-to-target-charging-real`: this
129,272-byte formula yields a 1-state parity automaton, pointing at formula-level simplification
rather than automaton type.

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
