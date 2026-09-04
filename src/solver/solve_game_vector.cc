#include "config/derived_gates.hh"
#include "configuration.hh"
#include "posets/downsets.hh"
#include "posets/vectors.hh"
#include "solver/solve_game_branches.hh"
#include "solver/solve_game_impl.hh"

#include <optional>
#include <spot/twa/twagraph.hh>

namespace acacia::solver_detail {

  std::optional<spot::twa_graph_ptr> solve_game_vector (
      spot::twa_graph_ptr aut, const VECTOR_ELT_T& kmax, const VECTOR_ELT_T& kmin,
      const VECTOR_ELT_T& kinc, const bdd& all_inputs, const bdd& all_outputs, bool do_synthesis,
      const std::vector<symmetry::indexed_family_hint>& hints, acacia::game_backend backend) {
    using Vector = posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>;
    using Downset = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<Vector>;

    return solve_with_downset<Downset> (aut, kmax, kmin, kinc, all_inputs, all_outputs,
                                        do_synthesis, hints, backend);
  }

}  // namespace acacia::solver_detail
