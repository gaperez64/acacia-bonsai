# Symmetry reduction proofs

A standalone Lean 4.35.0 project using only Lean's bundled `Std` library.
There are no external packages, CI jobs, or Meson changes.

```sh
cd proofs
lake build
```

The default target imports every module, including the witnesses and the #331
reproducer. Warnings are errors. Proofs are checked by the kernel; the project
contains no admitted proofs, custom axioms, native decision procedure, or partial
functions.

The model follows master `df5b2c28`, which includes merged PR #330, commit
`35a8723a`. All C++ line numbers below refer to this master revision, including
the family filter.

## What is proved

An accepted candidate gives state and AP bijections, fixes the initial state,
and preserves rejecting states, the I/O partition, coordinate caps, and labels.
The edge check is one-way, like `verify_full`. Its converse follows from
injectivity on a finite list of extensional edge keys. Parallel duplicate edges
are harmless for universal transition semantics; the degree and label-multiset
checks still retain their multiplicities.

The max/increment successor, safe box, and shrinking CPre commute with the
symmetry. Every simultaneous fixed-point stage is invariant. Iteration is proved
to reach the greatest safe fixed point within the number of concrete game states.
Initial membership is invariant.

The min/subtract backward operation is ported separately. Its preimage theorem
connects the C++ operation on maximal vectors to the game's successor, for
`K >= 1` and safe maxima. Union over outputs, intersection with the old region,
and permutation of downset maxima are proved exact.

The two reuse paths retain the C++ update orders:

- The sweep computes T once at the start of each orbit, intersects all its
  transported members, then uses the updated region for the next orbit.
- The representative path intersects one input's T, then repeatedly intersects
  images of the evolving region under generators.

For a `SweepPlan A side`, both paths return exactly the plain greatest fixed
point and verdict. Its transport evidence is `GeneratedBy`: a composition of
stored swaps, including identity. `close` still iterates the original generator
list. `generated_invariant` extends invariance to these compositions.
`phiFromSigma_comp` and `phiFromSigma_generated` prove that every slot
permutation lifts to a composition of the stored star swaps when the state
and AP layouts agree with those swaps. `generated_verified` also supplies the
resulting automorphism certificate.

Plan coverage and orbit closure concern only opponent letters after
`opponentProjection A side`. Representatives need no assignments to protagonist
APs; `computeT_projected` connects this coverage to the full-letter reference
game in both solver fixed-point proofs. The two solvers need not have the same
intermediate regions as simultaneous CPre. `orbitUpdate_exact` compares member
intersections using the same snapshot; `sweepFinal_eq` preserves the C++ snapshot
schedule.

`OrbitWitnesses.plan side` checks all plan conditions for three clients with
three input and three output APs, for either side. Its stored list is exactly
`[(0,1), (0,2)]`. The `001` orbit uses `(1,2)`, proved absent from that list;
the other transports include three-cycles. `member_layout` connects these
transports to `phi_from_sigma`, and the plan uses the general star-generation
bridge. The four representatives `000,001,011,111` leave protagonist bits false.
Its final-set, verdict, and early-exit corollaries instantiate the solver theorems.

All game theorems take a Boolean protagonist-side parameter. `false` controls
original outputs; `true` controls original inputs. `swapped_engine_cpre` proves
that the latter is the former after exchanging the engine partition. The actual
swap is `src/solver/solver_invoker.cc:1159–1165`. Formula transformation and LTL
translation are outside this symmetry proof.

## Issue #331 obligations

Names below are in namespace `Symmetry` unless prefixed otherwise. Unqualified
witnesses are in `Symmetry.Witnesses`; `OrbitWitnesses` adds the three-client
star plan. Concrete finite hypotheses are checked with `decide`. The final-set
examples apply the general theorems to these checked hypotheses, avoiding
exponential unfolding of the reference solver.

| Obligation | Lean definition or theorem | Concrete witness |
| --- | --- | --- |
| Finite automaton, Boolean formulas, alphabet, I/O | `Automaton`, `Formula.eval`, `Formula.rename`, `eval_rename`, `letters` | `aut`, `ioAut` |
| Guarded Boolean candidate verification | `check`, `familyGuard`, `shapeCheck`, `accepted_sound` | `accepted`, `ioAccepted` |
| Family purity implies per-AP partition preservation | `generated_partition`, `check_matches_source` | `accepted`, `ioAccepted` |
| Both bijections; init, acceptance, partition, caps | `accepted_properties` | `accepted`, `guard_mutants` |
| Matching labeled edges in both directions | `edges_both_directions`, `enabled_commutes` | `edges` |
| Counter successor commutes | `succ_commutes` | `successor` |
| Safety and initial vector preserved | `safe_commutes`, `initial_fixed` | `safety` |
| Literal backward operation and its semantics | `backward_commutes`, `backward_preimage`, `backwardT_exact`, `backward_update_exact` | `backward_step`, `backward_bridge`, `backward_union` |
| Permuting maxima represents the image downset | `below_commutes`, `image_downset` | `maxima_image` |
| CPre equivariance, for either side | `cpre_commutes`, `normal_frame`, `swapped_frame` | `predecessor` |
| Stages and final region invariant | `stage_invariant`, `final_invariant` | `stages`, `final_region` |
| Termination and greatest fixed point | `iterate_stable`, `final_fixed`, `final_greatest` | `fixed`, `greatest_hypotheses`, `greatest` |
| Winner invariant | `winner_invariant` | `safety`, `final_region`, `normal_loses`, `swapped_wins` |
| Representative backward-set reuse exact | `computeT_commutes`, `reuse_exact`, `orbitUpdate_exact` | `reused`, `orbit_update` |
| Stored-generator invariance extends to compositions | `GeneratedBy`, `generated_invariant`, `generated_verified` | `OrbitWitnesses.missing_transport`, `OrbitWitnesses.composition_invariant` |
| Every layout transport is generated by stored star swaps | `phiFromSigma_comp`, `transpositions_generate`, `phiFromSigma_generated` | `OrbitWitnesses.stars`, `OrbitWitnesses.layout_transport`, `OrbitWitnesses.member_layout`, `OrbitWitnesses.layout_generated` |
| Generator closure is invariant and preserves winners | `close_invariant`, `close_preserves` | `closure_hypotheses`, `OrbitWitnesses.plan` |
| Actual sweep preserves W and has the plain final set | `sweep_preserves_winner`, `sweepFinal_eq` | `plan`, `final_sets`, `OrbitWitnesses.final_sets` |
| Representative/closure solver has the plain final set | `representativeFinal_eq`, `verdicts_equal` | `plan`, `final_sets`, `verdicts`, `OrbitWitnesses.verdicts` |
| Actual swapped engine frame | `swapped_engine_cpre`, `both_engine_frames` | `ioAccepted`, `engine_partition` |
| Early initial-out exit preserves the verdict | `early_exit_exact`, `executed_verdicts_equal` | `executed`, `OrbitWitnesses.executed` |
| Uniform bound widening preserves symmetry | `bump_commutes` | `bound_raise` |
| Engine-input coverage ignores protagonist bits, in either frame | `opponentProjection`, `computeT_projected`, `opponentProjection_commutes`, `SweepPlan.covers` | `OrbitWitnesses.plan`, `OrbitWitnesses.input_only` |
| Every crossing transposition rejected | `Reproducer331.guarded_rejects_crossing` | Kernel-checked for all ordered pairs of three APs |
| Actual whole-family filter, including same-side swaps | `Reproducer331.family_rejected`, `Reproducer331.whole_mixed_family_rejected` | Both possible last-registered family tags |
| Old detector accepts; CPre conclusion fails | `Reproducer331.old_accepts_all`, `Reproducer331.old_cpre_fails`, `Reproducer331.old_swapped_cpre_fails` | Exact seven-state #331 automaton |

The reproducer's asymmetric downward closed region forbids a live state 1.
The old structural swap exchanges its `r_0` branch with the `r_1` branch.
An opponent can force `r_0`; a protagonist can choose `r_1` false. CPre therefore
fails to commute in both frames. A structural automorphism alone still commutes
with the full-letter successor: the failed obligation is ownership of choices.

The positive two-client automaton has rejecting client states, a nonidentity
state/AP swap, and both Boolean and counting coordinates. `ioAut` additionally
moves nontrivial input and output families separately. `guard_mutants` checks
rejection after independently breaking bijectivity, init, acceptance, caps,
edge structure, label renaming, or I/O preservation.

## C++ ↔ Lean correspondence

Paths in the first column are relative to `src/`.

| C++ source | Lean port |
| --- | --- |
| `solver/symmetry.hh:108–139` | `Automaton`, `Edge`, `shapeCheck` |
| `solver/symmetry.hh:144–209,235–277` | `Candidate`, `structuralCheck`, `shapeCheck`, `accepted_sound` |
| `solver/symmetry.hh:260–269` | `Formula.rename`, `act`, `keyPerm` |
| PR #330 `solver/symmetry.hh:310–335` | `Candidate.families`, `familyGuard`, `Generated`, `partitionGuard` |
| `solver/symmetry_certificate.hh:8–17` | Candidate provenance only; `Verified` is the Lean soundness certificate, not this C++ struct |
| `solver/k_bounded_safety_aut.hh:101–110` | `initial`, `safe`, `State` |
| `actioners/standard.hh:149–195` | `contribution`, `succ`, `backwardConstraint`, `backward` |
| `actioners/standard.hh:52–56,141–145` | `safeCap` |
| `solver/k_bounded_safety_aut.hh:244–308` | `computeT`, `cpre`, `backwardT`, `backward_update_exact` |
| `solver/k_bounded_safety_aut.hh:129–165` | `stage`, `final`, `winner`, `representativeRound` |
| `solver/equivariant_k_bounded_safety_aut.hh:57–79` | `letters`, `Automaton.enabled` |
| `solver/equivariant_k_bounded_safety_aut.hh:151–193,217–273,288–334` | `opponentProjection`, `orbitLetters`, `SweepPlan A side`; count/representative enumerator correspondence remains a trusted gap |
| `solver/equivariant_k_bounded_safety_aut.hh:374–383`; `solver/symmetry_blocks.hh:36–48,170–185` | `BlockLayout`, `BlockLayout.phiFromSigma`, `phiFromSigma_comp` |
| `solver/symmetry.hh:532–559`; `solver/symmetry_blocks.hh:274–302` | `GeneratedBy`, `Verified.comp`, `phiFromSigma_generated`; assumes valid layout and star agreement |
| `solver/equivariant_k_bounded_safety_aut.hh:388–432` | `act`, `image`, `reuseT`, `image_downset` |
| `solver/equivariant_k_bounded_safety_aut.hh:434–466` | `backwardT`, `computeT`, `backwardT_exact` |
| `solver/equivariant_k_bounded_safety_aut.hh:558–581` | `closePass`, `close` on the original list; `generated_invariant` for its products |
| `solver/equivariant_k_bounded_safety_aut.hh:636–643,851–853` | `Automaton.counting`, `capGuard` |
| `solver/equivariant_k_bounded_safety_aut.hh:721–774` | `orbitUpdate`, `sweep`, `sweepFinal` |
| `solver/equivariant_k_bounded_safety_aut.hh:923–947` | `representativeUpdate`, `representativeRound`, `representativeFinal` |
| `solver/k_bounded_safety_aut.hh:157–162`; equivariant `:746–750,952–956` | `runUntilInitialOut`, `early_exit_exact` |
| `solver/k_bounded_safety_aut.hh:220–238`; equivariant `:963–976` | `bump`, `bump_commutes` |
| `solver/solver_invoker.cc:1159–1165` | `swapFrame`, `swapped_engine_cpre` |

`Finite.lean` contains the finite-set converse argument. `FixedPoint.lean`
contains termination and greatestness. They contain helper proofs without adding
C++ algorithms. `Transports.lean` proves the composition/layout bridge. The two
`*Witnesses.lean` modules contain concrete fixtures and their checks.

## Trusted gap

The proofs cover the Lean port and its stated interfaces. Compiler verification
and extraction from the C++ binary are outside their scope. The orbit-plan
interface still requires a proof of correspondence with C++.

- Orbit construction and layout. `phiFromSigma_generated` proves that every
  slot `Perm` is generated using exactly the original
  stored star list. It assumes inverse block/slot tables (`BlockLayout`) for
  states and indexed AP families, and agreement of every root swap with both
  tables. Transports need not be stored. The concrete three-client mixed-I/O
  plan proves these hypotheses and projected coverage
  in both frames, without adding output assignments or closure generators.

  Successful C++ builders must still be proved to supply these premises:
  (1) `compute_block_layout` and `generators_match_layout` establish the inverse
  tables and simultaneous state/AP star agreement (`symmetry_blocks.hh:74–187,
  274–302`; family/index lookup at equivariant `:81–121`);
  (2) type-count enumeration, shared-bit enumeration, and family layout checks
  produce all projected opponent representatives (`equivariant:123–193,
  217–273,288–334`);
  (3) `match_slots` returns a bijective slot map taking canonical types to the
  current sequence, and `next_permutation` covers the whole orbit, establishing
  projected orbit closure (`equivariant:358–383,731–744`). The fixture supplies
  the stable matching maps by hand. Those algorithms, their order, and their
  decline/cap checks remain unverified. The final-set and verdict theorems
  cover plans with generated transports and projected coverage; applying them
  to arbitrary C++ invocations requires these builder premises.
- BDDs and supports. BDD labels become Boolean formulas and truth-table
  equality (`symmetry.hh:112–117,260–269`). `eval_rename` proves substitution
  semantics. Spot/BuDDy label construction and canonical BDD identity are
  trusted. `isInput` assumes a total disjoint I/O partition; malformed supports
  containing an AP on both/neither side are outside this model
  (PR #330 `symmetry.hh:310–335`). `computeT_projected` proves that enumerating
  full letters instead of partial cubes adds only redundant unused bits.
- Candidate search and hints. The completed candidate check includes the
  source's degree/label-multiset tests and explicit inverse-table validation.
  Backtracking order, signature strings, hash implementation, name parsing,
  frontend hints, time budgets, and search caps are not implemented
  (`symmetry.hh:96–277,304–384`; `symmetry_certificate.hh:8–17`). The latter
  contains declaration hints; these still require validation. Only
  checked candidates enter the game proofs. `Generated` records the
  within-family, involutive AP maps constructed by source transpositions;
  `check_matches_source` proves that the extra direct partition assertion is
  redundant there. The Lean soundness check also includes the downstream cap
  guard.
- Acceptance and counters. Acceptance is state based, as used by
  `equivariant_k_bounded_safety_aut.hh:190–201`. Transition-based acceptance
  configurations are outside this port. Shifted counters encode C++ `-1..K`
  as `0..K+1`; safe counting bounds are `<=K` and Boolean bounds are `<=1`.
  The successor retains scalar overflow before Boolean storage. Equivalence
  with the compressed *forward* solver is outside this proof.
  `backward_preimage` proves exact agreement with the backward operation used
  here (`actioners/standard.hh:153–195`). Its outputs obey the Boolean cap, so
  Boolean storage preserves them (`posets/vectors/X_and_boolvec.hh:21–30,69–71`).
  Naturals replace machine integers; C++ overflow/range validation is trusted.
- Downsets. Regions use concrete membership, and `downset` interprets a
  finite list of maximal bounds. Backward apply, union over outputs, and
  coordinate permutation are connected by theorems. Correctness of the posets
  container's insertion, intersection, maximal-element pruning, SIMD storage,
  and unchecked bulk factory remains unproved at the binary level
  (`k_bounded_safety_aut.hh:278–308`; equivariant `:388–432,473–506`).
- Iteration and bounds. The proof is for arbitrary fixed K; the backward
  bridge assumes `K>=1`. Finite complete rounds replace the picker/while-loop
  stopping machinery (`k_bounded_safety_aut.hh:129–165`; equivariant
  `:721–774,923–977`). The C++ snapshot/intersection orders within those rounds
  are retained. `bump` ports maximal-vector widening and proves it commutes;
  fixed-K early initial-out exits are proved exact by `early_exit_exact`.
  Adaptive K scheduling, fallback routing, and end-to-end LTL synthesis are
  not verified by this project.
