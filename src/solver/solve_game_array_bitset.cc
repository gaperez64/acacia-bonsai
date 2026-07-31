#include "solver/solve_game_branches.hh"

#include "posets/downsets.hh"
#include "posets/vectors.hh"
#include "solver/solve_game_impl.hh"
#include "utils/static_capacity_switch.hh"
#include "utils/static_switch.hh"

#include <algorithm>
#include <optional>
#include <spot/twa/twagraph.hh>
#include <utility>

namespace {
#ifdef NO_ARRAY_CAP_MAX
# pragma message("STATIC_ARRAY_CAP_MAX is being set to 0!")
  constexpr auto STATIC_ARRAY_CAP_MAX = 0;
#else
  constexpr auto STATIC_ARRAY_CAP_MAX =
      posets::vectors::traits<posets::vectors::ARRAY_IMPL, VECTOR_ELT_T>::capacity_for (
          STATIC_ARRAY_MAX);
#endif

  constexpr auto array_capacity_step =
      posets::vectors::traits<posets::vectors::ARRAY_IMPL,
                              VECTOR_ELT_T>::qualified_type_t::items_per_block;

  constexpr auto unreachable = [] ([[maybe_unused]] size_t) {
    std::unreachable ();
  };
}  // namespace

namespace acacia::solver_detail {

  std::optional<spot::twa_graph_ptr>
  solve_game_array_bitset (spot::twa_graph_ptr aut, const VECTOR_ELT_T& kmax,
                           const VECTOR_ELT_T& kmin, const VECTOR_ELT_T& kinc,
                           const bdd& all_inputs, const bdd& all_outputs,
                           size_t actual_nonbools, size_t nbitsetbools, bool do_synthesis) {
    std::optional<spot::twa_graph_ptr> res = std::nullopt;
    static_capacity_switch_t<STATIC_ARRAY_CAP_MAX, array_capacity_step> {}(
        [&] (auto vnonbools) {
          static_switch_t<STATIC_MAX_BITSETS> {}(
              [&] (auto vbitsets) {
                using SpecializedDownset =
                    posets::downsets::ARRAY_AND_BITSET_DOWNSET_IMPL<posets::vectors::x_and_bitset<
                        posets::vectors::ARRAY_IMPL<VECTOR_ELT_T, std::max (vnonbools.value, 1UL)>,
                        vbitsets.value>>;
                res = solve_with_downset<SpecializedDownset> (aut, kmax, kmin, kinc, all_inputs,
                                                              all_outputs, do_synthesis);
              },
              unreachable, posets::vectors::nbools_to_nbitsets (nbitsetbools));
        },
        unreachable, actual_nonbools);
    return res;
  }

}  // namespace acacia::solver_detail
