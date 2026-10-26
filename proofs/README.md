# Symmetry reduction proofs

A standalone Lean 4.35.0 project using only Lean's bundled core library.
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
point and verdict. `buildReduction` now constructs that plan from raw family
tables and completed detector candidates. It checks the family sides, recovers
state blocks and their AP-index alignment, and checks the stored state swaps.
The count/shared-bit loops, stable slot matching, and permutation traversal
then supply all plan premises; callers do not supply coverage or orbit closure.
Its transport evidence is `GeneratedBy`: a composition of
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
It now has two state blocks, meeting the production payoff gate. Its final-set,
verdict, and early-exit corollaries instantiate the solver theorems.
`BuilderWitnesses.builder_result` proves that the raw builder returns
`produced` for this fixture in both frames. `RepresentativeWitness` has five clients and two
blocks, so it exercises the production representative-path gates as well.

The critical pickers are ported with their nested scans, response splices,
signed priorities, and no-input branch. Exhaustion checks the maxima, which
suffice for the whole downset by successor monotonicity. `picker_completed`
connects this to literal backward constraints through `backward_update_exact`,
then to the final region and successful verdict. `PickerReach` retains arbitrary
input selection and the initial-out test between picker calls; it does not
replace the stopping test with complete rounds.

Forward Boolean storage needs a graph condition: a Boolean destination is
nonrejecting and has no incoming counting-state edge. `BooleanBoundary.lean`
ports rank saturation, acceptance clearing, and state reordering, and proves
that preprocessing supplies this condition. The saturation port terminates
because each strict pass raises a bounded sum of ranks. `storedSucc_exact`
then connects the stored forward result to the scalar backward-preimage bridge.
The disabled-Booleanization case has only counting coordinates and satisfies
the condition vacuously. See the concrete mutant below for why it matters.

All game theorems take a Boolean protagonist-side parameter. `false` controls
original outputs; `true` controls original inputs. `swapped_engine_cpre` proves
that the latter is the former after exchanging the engine partition. The actual
swap is `src/solver/solver_invoker.cc:1159–1165`. Formula transformation and LTL
translation are outside this symmetry proof.

## Issue #331 obligations

Names below are in namespace `Symmetry` unless prefixed otherwise. Unqualified
witnesses are in `Symmetry.Witnesses`; `OrbitWitnesses` adds the three-client
star plan. Small finite hypotheses are checked with `decide`.
Builder witnesses prove each stage's result once, then use `reduction_of_stages`
to compose them. Their final-set examples apply the general solver theorems;
they do not unfold the fixed-point solver.

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
| Successful raw builder supplies all plan premises | `buildFamilies`, `computeBlockLayout`, `alignGroup`, `buildReduction`, `reduction_final_eq`, `sharedBuilder_generators` | `BuilderWitnesses.builder_result`, `builder_accepts`, `original_generators`, `final_sets`; `RepresentativeWitness.builder_result` |
| Block and AP inverse lookup construction | `layoutOfRows`, `recognizeBlocks_exact`, `reindexLayout_swap` | `BuilderWitnesses.recognized_eq`, `layout_mutants`; two-block `OrbitWitnesses.stateLayout` |
| Count/shared-bit coverage and stable matching | `mem_typeCounts`, `counts_cover`, `letter_covers`, `matchEntries_complete`, `matchSlots_types`, `builtOrbits_cover` | `BuilderWitnesses.enumeration`, `BuilderWitnesses.enumeration_mutants`, both frames of `builder_accepts` |
| Complete, nonduplicating next-permutation traversal | `nextPermutation_exact`, `traversal_complete`, `traversal_unique` | repeated types and six distinct permutations in `BuilderWitnesses.enumeration`; unsorted-start mutant |
| Complete deduplicated response actions | `responseActions_complete`, `responseAction_succ` | `BuilderWitnesses.responses`; `RepresentativeWitness.identical_responses` |
| Cached and priority picker exhaustion | `criticalCached_none`, `criticalPQCached_none`, `criticalPQ_none` | `PickerWitnesses.cached_exhausted`, `pq_exhausted`, `caching`, `priority_hit`; `RepresentativeWitness.priority_completed` |
| Forward Boolean storage agrees with scalar successor | `storedSucc_exact`, `stored_picker_none` | `PickerWitnesses.closed`, `preprocessing_excludes_mutant` |
| Preprocessing supplies the Boolean graph condition | `saturateRanks`, `saturationPass_edges`, `booleanized_closed`, `renameStates_closed`, `preprocessBoolean_closed` | `PickerWitnesses.saturation_witnesses`, `preprocessing_excludes_mutant` |
| No-input branch satisfies backward constraints and returns the right winner | `picker_none_backward`, `picker_none_fixed`, `picker_completed`, `preprocessed_picker_completed`, `priority_picker_completed` | `PickerWitnesses.completion`, `preprocessed_success`, `zero_bound_mutant`; five-client `RepresentativeWitness.completed`, `priority_completed` |
| Initial-out branch for actual picker schedules | `selected_preserves`, `picker_reach_invariants`, `picker_initial_out` | `PickerWitnesses.reached`; existing early-exit witnesses remain |
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

## Picker completion and Boolean storage

Positive K and safe maxima alone are insufficient for the stored-forward test.
`PickerWitnesses.completion_counterexample` is a one-state, zero-AP example:
K is 1, the state is Boolean and rejecting, and it has an unconditional self-loop.
The region has the single maximum 0 in C++ coordinates (1 in Lean's shifted
coordinates). Both pickers return no input, but the representative backward
constraint removes that maximum. The scalar reference verdict is losing.

The numbers explain the failure. Forward apply produces 1, but Boolean storage
converts it back to presence, read as 0. Backward apply subtracts the acceptance
increment and produces -1. In Lean these values are 2, 1, and 0 respectively.
`counterexample_bridge` checks each value. The relevant current source lines are:

- `actioners/standard.hh:163–166,195`: increment, then construct `State`.
- `../subprojects/posets/include/posets/vectors/X_and_boolvec.hh:26–27,69–71`: store presence and read it as 0/-1.
- `input_pickers/critical.hh:49–54,76–78` and `critical_pq.hh:41–46,69–71`:
  forward membership makes the picker report exhaustion.
- `solver/equivariant_k_bounded_safety_aut.hh:936–942`: exhaustion returns success.
- `actioners/standard.hh:175–181`: backward subtraction rejects the maximum.

This is a mutant of the preprocessing contract, not a demonstrated wrong answer
from the shipped solver. The Boolean preprocessing port excludes it:
`preprocessBoolean_closed` proves the required graph condition, and
`preprocessing_excludes_mutant` checks that the processed example cannot be the
invalid graph. `preprocessed_success` exercises that connection on the positive one-state
fixture. `zero_bound_mutant` separately checks why the successful-verdict
statement requires positive K, even with counting storage. It does not assume that forward compression preserves
arbitrary safe vectors of arbitrary Boolean-labeled automata.

## C++ ↔ Lean correspondence

Paths in the first column are relative to `src/`, except the posets path
marked with `../subprojects/`.

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
| `solver/equivariant_k_bounded_safety_aut.hh:81–193,217–273,288–334` | `FamilyTable`, `buildFamilies`, `typeCounts`, `expandCounts`, `packBits`, `FamilyLayout.letter`, `builtOrbits`, `builtOrbits_cover` |
| `solver/symmetry_blocks.hh:52–187,274–302` | `orbitRoots`, `buckets`, `recognizeBlocks`, `layoutOfRows`, `computeBlockLayout`, `alignGroup`, `checkLayoutGroup`, `alignGroup_generators`, `alignGroup_families` |
| `solver/equivariant_k_bounded_safety_aut.hh:358–383,731–744` | `takeType`, `matchEntries`, `matchSlots`, `layoutTransport`, `nextPermutation`, `permutationWalk`, `traversal_complete`, `traversal_unique` |
| `solver/symmetry.hh:552–589`; `solver/symmetry_blocks.hh:274–302` | `sourceCandidate`, `buildSourceStars`, `GeneratedBy`, `phiFromSigma_generated`, `alignGroup_generators`; recovered layouts and checked stars supply the premises |
| `solver/equivariant_k_bounded_safety_aut.hh:388–432` | `act`, `image`, `reuseT`, `image_downset` |
| `solver/equivariant_k_bounded_safety_aut.hh:434–466` | `backwardT`, `computeT`, `backwardT_exact` |
| `solver/equivariant_k_bounded_safety_aut.hh:558–581` | `closePass`, `close` on the original list; `generated_invariant` for its products |
| `solver/equivariant_k_bounded_safety_aut.hh:636–643,851–853` | `Automaton.counting`, `capGuard` |
| `solver/equivariant_k_bounded_safety_aut.hh:721–774` | `orbitUpdate`, `sweep`, `sweepFinal` |
| `solver/equivariant_k_bounded_safety_aut.hh:923–955` | `representativeUpdate`, `PickerReach`, `picker_completed`, `picker_initial_out`; `representativeRound` remains the complete-round reference |
| `input_pickers/critical.hh:24–87`; `critical_pq.hh:19–87` | `takeResponse`, `scanCriticalRows`, `scanCriticalMaxima`, `criticalCached`, `criticalPQCached`, exhaustion equivalences |
| `actioners/standard.hh:195`; `../subprojects/posets/include/posets/vectors/X_and_boolvec.hh:21–30,69–71` | `storedSucc`, `storedSucc_exact` |
| `boolean_states/forward_saturation.hh:29–87` | `saturation`, `saturateRanks`, `saturationPass`, `booleanized`, `renameStates`, `preprocessBoolean_closed` |
| `solver/k_bounded_safety_aut.hh:157–162`; equivariant `:746–750,952–956` | `runUntilInitialOut`, `early_exit_exact` |
| `solver/k_bounded_safety_aut.hh:220–238`; equivariant `:963–976` | `bump`, `bump_commutes` |
| `solver/solver_invoker.cc:1159–1165` | `swapFrame`, `swapped_engine_cpre` |

`Finite.lean` contains the finite-set converse argument. `FixedPoint.lean`
contains termination and greatestness. They contain helper proofs without adding
C++ algorithms. `Transports.lean` proves the composition/layout bridge. The
`*Witnesses.lean` and `RepresentativeWitness.lean` modules contain concrete
fixtures and their checks.

## Trusted boundary and scope

Within the port, the remaining implementation boundary is BDD interpretation,
container representation, and machine arithmetic. Compiler correctness and a
formal extraction/refinement of the C++ binary are outside this project.

- BDDs, supports, and dictionary tables. Labels are Boolean truth tables.
  Spot/BuDDy construction, canonical identity, substitution, and support-cube
  enumeration remain trusted. `isInput` interprets a total, disjoint engine
  partition. `FamilyTable.unique` is the distinct-variable invariant of the AP
  dictionary's family/index entries; `buildFamilies` derives the inverse AP
  layout and checks family sides. Parsing names into those dictionary entries
  is outside the port. Grouped precomputer cubes are interpreted extensionally
  as their represented input valuations and compatible response actions.
  In `picker_completed`, `retained` is the table interface's coverage contract
  after restriction, and `compatible` is its input/action contract. These are
  BDD/precomputer representation assumptions; exhaustion itself and its
  implication for every representative backward constraint are proved.
  `computeT_projected` removes unused input bits; `responseActions_complete`
  and `responseAction_succ` justify response deduplication.
- Containers. `downset` interprets a finite list of maxima. Antichain insertion,
  meet/intersection, pruning, SIMD and Boolean storage implementation, BDD-table
  restriction, and the unchecked bulk factory remain trusted at their data
  structure interfaces. `image_downset`, `backward_update_exact`, and
  `storedSucc_exact` prove the corresponding mathematical operations. The picker
  cache proof preserves row references and response multisets; queue tie order
  is immaterial to exhaustion.
- Arithmetic and execution limits. Naturals/integers replace machine counters,
  ranks, bit shifts, and signed priority counts. C++ range/overflow handling,
  interruption and worker cancellation remain trusted. The port retains the
  successful-plan cap conditions: the 4097th count is a decline sentinel, not
  a truncated successful orbit universe. `count_cap_complete` proves this.

The builder no longer assumes `SweepPlan` coverage or closure. Its input is a
well-formed AP dictionary table and raw completed state candidates; successful
checks construct state/AP inverses and stored stars. Root buckets are a partition
for any root map, so the inverse proof does not require trusting union-find's
choice of root or path compression. Stable type matching and the full
pivot/swap/reverse traversal are proved, including repeated types. AP/state slot
alignment preserves the original generator list and its order.

The result is for fixed K, with K >= 1 for the backward bridge and actual picker
success branch. Arbitrary in-place picker schedules and both initial-out and
no-input branches are covered. Adaptive-K warm-start correctness, formula/LTL
translation, completeness of detector search or hint selection, and general
correctness of the Boolean optimization as an LTL transformation are outside
this symmetry proof. The needed Boolean graph/storage property of the ported
preprocessing is proved here; it is no longer an assumed picker bridge.
The preprocessor uses state-based acceptance, matching this solver configuration.

C++ is unchanged. No runtime monitor or additional solver validation was added.

## Memory-limited validation

The library config uses one worker per compiler and synchronous elaboration.
Lake can still launch independent modules together. This sequence builds targets
in dependency order, with one compiler at a time, under the 2,800,000 KiB virtual
memory limit. Run it from `proofs/`:

```sh
export LEAN_NUM_THREADS=1 LEAN_STACK_SIZE_KB=2048 LEAN_MAIN_USE_THREAD=0
(
  ulimit -v 2800000
  ulimit -c 0
  for module in Automaton Finite Detector Game Equivariance FixedPoint \
    Transports Reuse Backward Closure Frames Solver Reproducer331 Witnesses \
    OrbitWitnesses Enumeration NextPermutation LayoutBuilder PlanBuilder \
    Pickers CachedPickers BooleanBoundary PickerCompletion PickerWitnesses \
    BuilderWitnesses RepresentativeWitness; do
    timeout -k 5 300 lake build "SymmetryProofs.$module" || exit $?
  done
  timeout -k 5 1200 lake build
)
```

The two builder fixtures use three and five clients, with two state blocks each.
Their recognition and detector results are proved once and kept as explicit
tables. This avoids repeated kernel evaluation of the whole builder when a
later theorem reads a layout field. The compiler no longer imports all of
`Std`; the core imports contain everything these proofs use.

Validation on Lean 4.35.0: the default `lake build` passed warning-free (29 jobs)
under that limit. An axiom audit of 69 main definitions, theorems, and witnesses
found only Lean's standard `propext`, `Classical.choice`, and `Quot.sound`.
