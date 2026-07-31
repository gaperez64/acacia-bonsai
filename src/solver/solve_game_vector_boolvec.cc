#include "solver/solve_game_branches.hh"

#ifdef USE_BOOLVEC_OVER_BITSET

#include "posets/downsets.hh"
#include "posets/vectors.hh"
#include "solver/solve_game_impl.hh"

#include <optional>
#include <spot/twa/twagraph.hh>

namespace acacia::solver_detail {

  std::optional<spot::twa_graph_ptr>
  solve_game_vector_boolvec (spot::twa_graph_ptr aut, const VECTOR_ELT_T& kmax,
                             const VECTOR_ELT_T& kmin, const VECTOR_ELT_T& kinc,
                             const bdd& all_inputs, const bdd& all_outputs, bool do_synthesis) {
    using SpecializedDownset = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<
        posets::vectors::x_and_boolvec<posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>>>;

    return solve_with_downset<SpecializedDownset> (aut, kmax, kmin, kinc, all_inputs, all_outputs,
                                                  do_synthesis);
  }

}  // namespace acacia::solver_detail

#endif
