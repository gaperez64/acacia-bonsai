#include "solver/solve_game.hh"

#include "posets/vectors.hh"
#include "posets/vectors/traits.hh"
#include "solver/solve_game_branches.hh"
#include "utils/verbose.hh"

#include <algorithm>
#include <optional>
#include <spot/twa/twagraph.hh>

namespace {
#ifdef NO_ARRAY_CAP_MAX
# pragma message("STATIC_ARRAY_CAP_MAX is being set to 0!")
  constexpr auto STATIC_ARRAY_CAP_MAX = 0;
#else
  constexpr auto STATIC_ARRAY_CAP_MAX =
      posets::vectors::traits<posets::vectors::ARRAY_IMPL, VECTOR_ELT_T>::capacity_for (
          STATIC_ARRAY_MAX);
#endif
}  // namespace

/**
 * Complicated construction to make sure that we solve the game while making
 * good use of the downsets library. In a nutshell, we check how many boolean
 * states we have to fit their counters into an array of bitsets or a vector of
 * bools while keeping the rest of the counters in a proper downset. Since array
 * sizes have to be fixed during compile time, we need to prepare a few sizes
 * in advance here and otherwise default to other means.
 *
 * Two macros can be used to control the switching:
 * - NO_ARRAY_CAP_MAX
 * - USE_BOOLVEC_OVER_BITSET
 */
std::optional<spot::twa_graph_ptr> solve_game (spot::twa_graph_ptr aut, const VECTOR_ELT_T& kmax,
                                               const VECTOR_ELT_T& kmin, const VECTOR_ELT_T& kinc,
                                               const bdd& all_inputs, const bdd& all_outputs,
                                               bool do_synthesis) {
  if (all_outputs == bddtrue)
    verb_do (2, vout << "Warning: synthesis without output APs\n");

  // Compute how many boolean states will actually be put in bitsets.
  constexpr auto max_bools_in_bitsets = posets::vectors::nbitsets_to_nbools (STATIC_MAX_BITSETS);
  auto nbitsetbools = aut->num_states () - posets::vectors::bool_threshold;
  if (nbitsetbools > max_bools_in_bitsets) {
    verb_do (1, vout << "Warning: bitsets not large enough, using regular vectors for some "
                        "Boolean states.\n"
                     << "\tTotal # of Boolean-for-bitset states: " << nbitsetbools
                     << ", max: " << max_bools_in_bitsets << std::endl);
    nbitsetbools = max_bools_in_bitsets;
  }

  // Maximize usage of the nonbool implementation.
  auto nonbools = aut->num_states () - nbitsetbools;
  size_t actual_nonbools =
      (nonbools <= STATIC_ARRAY_CAP_MAX)
          ? posets::vectors::traits<posets::vectors::ARRAY_IMPL, VECTOR_ELT_T>::capacity_for (
                nonbools)
          : posets::vectors::traits<posets::vectors::VECTOR_IMPL, VECTOR_ELT_T>::capacity_for (
                nonbools);
  if (actual_nonbools >= aut->num_states ())
    nbitsetbools = 0;
  else
    nbitsetbools -= (actual_nonbools - nonbools);

  posets::vectors::bitset_threshold = aut->num_states () - nbitsetbools;

  verb_do (1, vout << "Bitset threshold set at " << posets::vectors::bitset_threshold << "\n");

  if (actual_nonbools <= STATIC_ARRAY_CAP_MAX)
    return acacia::solver_detail::solve_game_array_bitset (
        aut, kmax, kmin, kinc, all_inputs, all_outputs, actual_nonbools, nbitsetbools,
        do_synthesis);

#ifndef USE_BOOLVEC_OVER_BITSET
  return acacia::solver_detail::solve_game_vector_bitset (
      aut, kmax, kmin, kinc, all_inputs, all_outputs, nbitsetbools, do_synthesis);
#else
  return acacia::solver_detail::solve_game_vector_boolvec (
      aut, kmax, kmin, kinc, all_inputs, all_outputs, do_synthesis);
#endif
}
