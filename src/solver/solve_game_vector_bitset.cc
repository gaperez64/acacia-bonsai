#include "solver/solve_game_branches.hh"

#ifndef USE_BOOLVEC_OVER_BITSET

#include "posets/downsets.hh"
#include "posets/vectors.hh"
#include "solver/solve_game_impl.hh"
#include "utils/static_switch.hh"

#include <optional>
#include <spot/twa/twagraph.hh>
#include <utility>

namespace {
  constexpr auto unreachable = [] ([[maybe_unused]] size_t) {
    std::unreachable ();
  };
}  // namespace

namespace acacia::solver_detail {

  std::optional<spot::twa_graph_ptr>
  solve_game_vector_bitset (spot::twa_graph_ptr aut, const VECTOR_ELT_T& kmax,
                            const VECTOR_ELT_T& kmin, const VECTOR_ELT_T& kinc,
                            const bdd& all_inputs, const bdd& all_outputs,
                            size_t nbitsetbools, bool do_synthesis,
                            const std::vector<symmetry::indexed_family_hint>& hints) {
    std::optional<spot::twa_graph_ptr> res = std::nullopt;
    static_switch_t<STATIC_MAX_BITSETS> {}(
        [&] (auto vbitsets) {
          using SpecializedDownset =
              posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<posets::vectors::x_and_bitset<
                  posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>, vbitsets.value>>;
          res = solve_with_downset<SpecializedDownset> (aut, kmax, kmin, kinc, all_inputs,
                                                        all_outputs, do_synthesis, hints);
        },
        unreachable, posets::vectors::nbools_to_nbitsets (nbitsetbools));
    return res;
  }

}  // namespace acacia::solver_detail

#endif
