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
