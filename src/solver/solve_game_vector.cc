#include "configuration.hh"
#include "posets/downsets.hh"
#include "posets/vectors.hh"
#include "solver/solve_game.hh"
#include "solver/solve_game_impl.hh"
#include "utils/verbose.hh"

#include <optional>
#include <spot/twa/twagraph.hh>

std::optional<spot::twa_graph_ptr> solve_game (
    spot::twa_graph_ptr aut, const VECTOR_ELT_T& kmax, const VECTOR_ELT_T& kmin,
    const VECTOR_ELT_T& kinc, const bdd& all_inputs, const bdd& all_outputs, bool do_synthesis,
    const std::vector<symmetry::indexed_family_hint>& hints, acacia::game_backend backend,
    acacia::candidate_mode candidate, bool equivariance) {
  if (all_outputs == bddtrue)
    verb_do (2, vout << "Warning: synthesis without output APs\n");

  using Vector = posets::vectors::simd_vector_backed<VECTOR_ELT_T>;
  using Downset = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<Vector>;

  return acacia::solver_detail::solve_with_downset<Downset> (
      aut, kmax, kmin, kinc, all_inputs, all_outputs, do_synthesis, hints, backend, candidate,
      equivariance);
}
