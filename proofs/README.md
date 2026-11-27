# Symmetry reduction proofs

Lean 4.35.0, core only. The proofs cover the guarded detector, both game frames,
builders, representative sweeps, action tables, picker completion and adaptive K.
Historical line references use master `bbf12523`; current source anchors are
relative to `src/`.

## C++ and Lean correspondence

| C++ source | Lean port |
| --- | --- |
| `solver/symmetry.hh#inline std::optional<std::vector<unsigned>> verify_transposition (`; `solver/symmetry.hh#return (bdd_exist (all_inputs, v) != all_inputs) != f.is_input or` | `Automaton`, `Candidate`, checks, `Verified`, finite edge converse |
| `solver/symmetry.hh#candidate.gen_pairs.push_back ({root, other});`; `solver/symmetry_blocks.hh#inline bool generators_match_layout (const group& G, const block_layout& L) {` | `SourceStars`, `GeneratedBy`, `phiFromSigma_generated`, `alignGroup` |
| `solver/symmetry_blocks.hh#inline std::optional<block_layout> compute_block_layout (const group& G, unsigned num_states) {` | `orbitRoots`, `buckets`, `recognizeBlocks`, `layoutOfRows`, `computeBlockLayout` |
| `solver/equivariant_k_bounded_safety_aut.hh#inline orbit_build_result build_orbits (const spot::twa_graph_ptr& aut, bdd all_inputs,`; `solver/equivariant_k_bounded_safety_aut.hh#} while (std::next_permutation (sequence.begin (), sequence.end ()));` | Family/count/shared-bit builders, stable matching, next-permutation traversal, `builtPlan` |
| `solver/equivariant_k_bounded_safety_aut.hh#if ((q < posets::vectors::bool_threshold) != (g[q] < posets::vectors::bool_threshold))` | `buildReduction`, source stars, alignment and plan caps |
| `actioners/standard.hh#State apply (const State& m, const Payload& avec,`; `solver/k_bounded_safety_aut.hh#void cpre_inplace (SetOfStates& f, const Action& io_action, Actioner& actioner,` | `succ`, `safe`, `initial`, `backward`, `computeT`, `cpre` |
| `solver/k_bounded_safety_aut.hh#std::make_pair (k, std::move (f)));` | `stage`, `final`, `winner`, fixed-point termination |
| `solver/equivariant_k_bounded_safety_aut.hh#bool close_under_generators (SetOfStates& f, const symmetry::group& G,`; `solver/equivariant_k_bounded_safety_aut.hh#result<SetOfStates> solve_orbit_sweep (const spot::twa_graph_ptr& aut, VECTOR_ELT_T kmax,` | Downset transport, snapshot sweep, closure, representative updates and completion |
| `ios_precomputers/mona.hh#auto recurse_outputs = [&] (this const auto& self, auto& tss, bdd bdd_opq) -> void {`; `ios_precomputers/mona.hh#if (bdd_opq == bddfalse or bdd_opq == bddtrue or bdd_var (bdd_opq) >= first_src_var) {`; `ios_precomputers/mona.hh#auto [it, added] = ids_by_row.emplace (endpoints_for_letter, bddfalse);`; `ios_precomputers/standard.hh#void restrict_inputs (bdd allowed_inputs) { input_letters &= allowed_inputs; }` | `Diagram`, `tabulate`, `monaRows`, `explicitTable`, input/output traversal and endpoint interpretation |
| `solver/equivariant_k_bounded_safety_aut.hh#void filter_to_representative_inputs (InputsToIOs& inputs_to_ios,`; `actioners/standard.hh#return (x.second < y.second);` | Cube restriction, whole-row `dedup`, `precomputer_refinement` |
| `input_pickers/critical.hh#actions.splice (actions.begin (), actions, it_act);`; `input_pickers/critical_pq.hh#critical_input = fwd_actions_pq.emplace (priority - 1, ref);` | Nested scans, response splices, signed priorities and exhaustion |
| `boolean_states/forward_saturation.hh#c[e.dst] = c_src_mod;`; `boolean_states/forward_saturation.hh#e.acc = spot::acc_cond::mark_t {};`; `boolean_states/forward_saturation.hh#g.rename_states_ (rename);` | Rank saturation, acceptance clearing, state reordering, `preprocessBoolean_closed` |
| `solver/solve_game_vector.cc#using Vector = posets::vectors::VECTOR_IMPL_AUTO<VECTOR_ELT_T>;`; `../subprojects/posets/include/posets/vectors/X_and_boolvec.hh#bools.push_back (v[i] + 1);` | Scalar `applyAction` versus presence-compressed `storedSucc` |
| `solver/k_bounded_safety_aut.hh#bool raise_bound_or_give_up (SetOfStates& f, VECTOR_ELT_T& k, Actioner& actioner) {`; `solver/k_schedule.hh#? kmax : std::min (kmax, k + kinc);`; `solver/equivariant_k_bounded_safety_aut.hh#const auto next_k = detail::checked_next_k (k, kinc);`; `solver/equivariant_k_bounded_safety_aut.hh#const auto next_k = detail::checked_next_k (k, kinc);` | Early exits, `bump`, `AdaptiveReach`, shared and ordinary schedules (both equivariant increments) |
| `solver/solver_invoker.cc#input_aps.swap (output_aps);` | `swapFrame`, `swapped_engine_cpre` |
| `../tests/equivariant_pre_test.cc#bool run_mixed_io_family_case () {` | `Reproducer331`, mixed I/O regression fixture |
| `solver/symmetric_count.cc#counts[ls & ~reset] += c;` | `push_counts`, `countTransition`, `count_successor` |

`#` marks an exact source line. Run `python3 tests/check-proof-references.py`
from the repository root to check anchor counts and proof reference coverage.
Changing an anchor requires reviewing the correspondence; this check detects
source drift and does not verify C++ semantics.

## Trusted gap

Lean checks mathematical ports, with no admitted steps, custom axioms, native
decision procedures or partial functions. It does not verify the compiled binary.
BDD construction, identity, dictionaries, paths and endpoint decoding must match
the model. `mona_refinement` assumes the diagram denotes the endpoint relation
and satisfies `Diagram.Realizable`; the explicit constructor proves these
properties internally, including injective IDs for empty endpoint rows. AP
identifiers must be distinct.

Containers, antichain operations, maps, caches and machine arithmetic must match
the mathematical interfaces. Both equivariant shared-loop increments now use
`checked_next_k`; their full-step schedule can overshoot kmax, unlike the ordinary
clamped schedule. Refutation at an overshoot implies refutation at kmax; success
certifies actual K. K and lifted ranks must remain representable.

Backward and picker completion require K ≥ 1 and safe maxima. Stored Boolean
forward steps require `BooleanClosed`, supplied by the ported preprocessing.
Adaptive raises require a positive actual delta, uniform counting lift and Boolean
reset. The schedule mutant wins at 3 and loses at configured maximum 2.
Compiled execution, interruption, translation, detector-search completeness and
Boolean optimization as an LTL transformation are outside scope.

## Build

From the repository root:

```sh
proofs/check.sh
```

The script checks all modules and examples with warnings as errors, even after a
Lake cache hit, then audits every declaration's axioms. Only `propext`,
`Classical.choice` and `Quot.sound` are allowed. It rejects `sorry`, `admit`,
custom `axiom`, `native_decide` and `partial`.

```sh
python3 proofs/test-axiom-audit.py
```

Require the **Lean required** branch-protection check. It accepts a skipped Lean
run only when the successful change detector explicitly returned `false`.
Both correspondence checks run on every PR. Lightweight CI regressions:
`python3 -m pytest tests/pytest/test_lean_ci.py -q`.

## Issue #454 obligations

`Quotient.lean` proves orbit quotients of turn-based games sound for any
objective on the priority sequence, parity included. `CountGame.lean` and
`CountTemporal.lean` provide the generic reduction and temporal lemmas.
`CountEngine.lean` implements the concrete predicates, since/breakpoint bits,
events and resets at `symmetric_count.cc` revision `6c6da84a`, and connects them
to the spec's game. Namespaces are `Symmetry.CountGame`, `Symmetry.CountTemporal`
and `Symmetry.CountEngine`. Counts and transition tables are mathematical;
the compiled-code, BDD and machine-arithmetic boundary still applies.
`Translates` assumes only correctness of the global parity translator for the
**encoded** formula. `Clause.correct` proves the original-to-encoded equivalence,
including local Inf/Fin/sink acceptance and position-0 since conjuncts.
`Certified` requires strategy entries only on reachable count keys; the adapter
completes unvisited entries and chooses raw indexed edge witnesses before
canonicalization. `engine_spec_verdict` transfers either certified winner from
the zero-bit initial key and excludes an opposite winner for the original spec.

| Obligation | Definition or theorem |
| --- | --- |
| Group acting by owner-, priority- and edge-preserving maps, orbit-invariant representatives with transporters | `Action`, `Action.canon_act` |
| Quotient game and its plays and strategies | `Action.quotient`, `Action.QWins` |
| Plays and strategy-following project to the quotient | `Action.project_play`, `Action.project_consistent` |
| A transported winning quotient strategy wins, for either player | `Action.lift`, `Action.lift_legal`, `Action.lift_wins` |
| No vertex is won by both players; the certified verdict transfers | `exclusive`, `Action.verdict` |
| Invariant edge outputs are preserved | `Action.lift_output` |
| Indexed Mealy game, simultaneous client permutations, exact state/input count orbits | `Machine`, `game`, `counts_orbit`, `key_orbit`, `canonical_vertex`, `action` |
| Compositions enumerate precisely the input and output stabilizer orbits | `stabilizer_orbit`, `compositions_exact`, `input_edges`, `output_edges` |
| Occupied-cell events and weighted count successors equal concrete successors | `occupied_events`, `push_counts`, `count_successor`, `count_canonical` |
| Successor sharing retains incoming colors and lifts winning strategies | `successor_reuse` |
| Absorbing collapse preserves max-odd parity at every entrance time | `collapse_wins` |
| Computed input/output compositions, normalized targets and count-key edges | `countMoves`, `countGame_edges`, `count_quotient_edges`, `count_normalized_target` |
| Reachable count certificates lift through quotient, sharing and collapse | `Certified.wins`, `Action.key_wins`, `count_certificate` |
| Engine verdict means a winner for the original specification, for either player | `Clause.correct`, `Run.specification`, `spec_wins_iff`, `engine_spec_verdict` |
| Concrete predicate/ready masks, occupied any/all tests, set/test/reset breakpoint order | `engineMachine`, `event_values`, `Run.local_step`, `Run.breakpoint` |
| Actual emitted letter events and all four finite temporal aggregates at every suffix | `Run.letter_event`, `Run.any_event`, `Run.all_event`, `Run.all_inf`, `Run.any_inf`, `Run.all_fin`, `Run.any_fin` |
| Prefix independence and substitution in arbitrary contexts and polarities | `gf_suffix`, `fg_suffix`, `context` |
| Deterministic local Inf/Fin runs and permanent missing-transition rejection | `Run.local_run`, `Run.mark`, `Run.sink`, `Run.local_encoding` |
| Initially false since monitors, position-0 rewrite and later-position counterexample | `Run.since_history`, `Run.since_encoding`, `since_later_counterexample` |
| Flattening exposes positive conjuncts at the original position only | `flatten`, `flatten_correct`, `top_conjunct_replacement` |

## Issue #482 obligations

`CountCardinality.lean` proves that invariance under every index transposition
makes a Boolean current-letter constraint depend only on its true count
(`count_factor`, `accepted_weights`), and that the constraint survives count
canonicalization (`quotient_constraint`). `weighted_constraint` identifies the
weighted cell sum with the concrete letter count. The cardinality case in
`CountEngine.event_values`, `Run.cardinality_event` and `Atom.correct` connects
these events to `Clause.correct` and `engine_spec_verdict`, including arbitrary
temporal contexts.

`restrict_safety` proves both players' strategy outcome conditions equivalent
when system choices violating an unconditional G conjunct are removed. System
strategies must keep every outcome safe; environment strategies need only check
the remaining safe outcomes, since every removed outcome already loses for the
system. The source objective
implying the safety conjunct is an explicit premise; it is unavailable for F, X,
assumptions or disjunctions. `engine_safety_restriction` applies this equivalence
to specification strategies. `safetyCompletion` constructs the filtered graph:
keep safe system edges, or one original output edge when none is safe.
`safetyCompletion_empty` covers that empty-output case and
`safetyCompletion_total` proves totality. C++ chooses the all-zero output letter;
the original G monitor still rejects it. No new self-loop is added.
`translated_cardinality_G` derives accepted weights on winning original runs
from membership of the selected unconditional G clause and translation correctness.
`safetyCompletion_excluded` derives excluded-play loss from the retained G
monitor's implication to edge safety. `pruned_certificate` transfers either
winner's certificate, and `engine_pruned_verdict` instantiates the constructed
completion and feeds its certificate into `engine_spec_verdict`.
The implication from the translated monitor to the selected safe edges remains
an explicit correspondence obligation, not a winner-preservation assumption or
a consequence of anchor checking. Selecting a positive unconditional G clause,
output ownership, BDD identity, accepted-weight reconstruction and exact
enumeration remain within the compiled-code boundary above. Local templates
exclude generated events; since antecedents use only actual global APs.
Split prechecks bound materialized compositions before allocation. Distinct
successors are charged on insertion and copied into a fresh adjacency vector;
duplicate candidates never accumulate in its storage or retained capacity.

| C++ source | Lean port |
| --- | --- |
| `solver/symmetric_count.cc#const bool equal = bdd_replace (original, pair) == original;`; `solver/symmetric_count.cc#if (reconstructed != original)` | `invariant`, `count_factor`, `accepted_weights`, `quotient_constraint` |
| `solver/symmetric_count.cc#cardinalities[j] += c;`; `solver/symmetric_count.cc#fired = b.cardinalities[e.pred].truth[cardinalities[e.pred]];` | `weighted_constraint`, `event_values`, `Run.cardinality_event`, `Atom.correct` |
| `solver/symmetric_count.cc#if (under_top_g (under_top_g, lowered, false)) {`; `solver/symmetric_count.cc#feasible = feasible and reachable;`; `solver/symmetric_count.cc#if (successors.empty ()) {`; `solver/symmetric_count.cc#emit (0);` | `restrict_safety`, `translated_cardinality_G`, `engine_safety_restriction`, `safetyCompletion_empty`, `safetyCompletion_excluded`, `engine_pruned_verdict` |
| `solver/symmetric_count.cc#if (not successors.count (w)) {`; `solver/symmetric_count.cc#succ[u].assign (successors.begin (), successors.end ());` | Set-valued `Game.edge`, `countGame_edges`; bounded workspace and distinct-edge accounting |

## Issue #471 certification obligations

`EncodedCount` connects bit-cube equality to decoding and proves that
transporting an unused code keeps every selector false. The two-time table
uses the current and next raw letters at every position. `generator_checked`
combines pointwise atom transport and whole-objective renaming.
`raw_objective_preserved` takes the independently checked reconstruction
of the raw objective as a premise and transfers preservation to it.

The C++ boundary supplies the domains, owners, bit orders, disjoint code swaps
and the exact objective with its ordered input and output partitions.
The certificate covers the checked expanded code map and its action on that
converted objective. Source-level selector intent, including absence of
wraparound, is outside its scope.
Reconstruction compares the converted formula with the expanded abstract
formula using BDD equality on Boolean shells, temporal congruence, and the
infinite-word `X` and `G` rewrites. The inspector runs certification in a
bounded worker and treats worker failure as a decline.

The stored generators are simultaneous coordinate stars on selected codes
and linked aliases, and separate representative stars in each residual
class. A consumer must bind the certificate to the same objective and ordered
partitions and supply a `Generated` witness from these stored swaps for each
permutation it uses. The theorem covers products and inverses of the recorded
generators. This makes generation a consumer obligation when using a
coordinate/class product representation.

| C++ source | Lean port |
| --- | --- |
| `solver/encoded_family.cc#if (f != cube (bus, code, next))`; `solver/encoded_family.cc#require (truth[raw] == (raw == code));` | `cube_decodes`, `decode_encode`, selector trace premise of `encoded_decomposition_correct` |
| `solver/encoded_family.cc#require (rebuild (rebuild, 0, 0) == original);` | Pointwise table trace premise of `encoded_decomposition_correct` |
| `solver/encoded_family.cc#if (p.truth[row] != p.truth[image])` | `table_action` |
| `solver/encoded_family.cc#swaps.push_back ({b, bus.codes[0], bus.codes[i]});`; `solver/encoded_family.cc#require (equal (result.abstract, substitute (result.abstract, rename)));` | `decode_action`, `unused_action`, `generator_checked`, `family_check_sound` |
| `solver/encoded_family.cc#require (check.equal (exact.f, check.substitute (result.abstract, check.substitutions)));` | `reconstruct` premise of `raw_objective_preserved` |
