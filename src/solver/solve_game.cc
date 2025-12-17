#include "solve_game.hh"
#include "boolean_states.hh"
#include "k-bounded_safety_aut.hh"
#include "posets/downsets.hh"
#include "actioners.hh"
#include "input_pickers.hh"
#include "ios_precomputers.hh"
#include "configuration.hh"
#include "posets/vectors.hh"
#include "posets/vectors/traits.hh"
#include "utils/static_switch.hh"
#include "k-bounded_safety_aut.hh"

#include <spot/misc/timer.hh>

bool solve_game (spot::twa_graph_ptr aut, unsigned kmax, unsigned kmin, unsigned kinc, bdd all_inputs,
                 bdd all_outputs) {

  posets::vectors::bool_threshold = (BOOLEAN_STATES::make (aut, kmax)) ();
  verb_do (1, vout << "Found " << posets::vectors::bool_threshold << " boolean states.\n");

  // Compute how many boolean states will actually be put in bitsets.
  constexpr auto max_bools_in_bitsets = posets::vectors::nbitsets_to_nbools (STATIC_MAX_BITSETS);
  auto nbitsetbools = aut->num_states () - posets::vectors::bool_threshold;
  if (nbitsetbools > max_bools_in_bitsets) {
    verb_do (1, vout << "Warning: bitsets not large enough, using regular vectors for some "
                        "Boolean states.\n"
                     /*   */
                     << "\tTotal # of Boolean-for-bitset states: "
                     << nbitsetbools
                     /*   */
                     << ", max: " << max_bools_in_bitsets << std::endl);
    nbitsetbools = max_bools_in_bitsets;
  }

  constexpr auto STATIC_ARRAY_CAP_MAX =
      posets::vectors::traits<posets::vectors::ARRAY_IMPL, VECTOR_ELT_T>::capacity_for (
          STATIC_ARRAY_MAX);

  // Maximize usage of the nonbool implementation
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

  bool realizable = false;

#define UNREACHABLE [] (int x) { assert (false); }

  if (actual_nonbools <= STATIC_ARRAY_CAP_MAX) {  // Array & Bitsets
    static_switch_t<STATIC_ARRAY_CAP_MAX> {}(
        [&] (auto vnonbools) {
          static_switch_t<STATIC_MAX_BITSETS> {}(
              [&] (auto vbitsets) {
                using SpecializedDownset =
                    posets::downsets::ARRAY_AND_BITSET_DOWNSET_IMPL<posets::vectors::x_and_bitset<
                        posets::vectors::ARRAY_IMPL<VECTOR_ELT_T, std::max (vnonbools.value, 1UL)>,
                        vbitsets.value>>;
                using IOsPrecomputationMaker = IOS_PRECOMPUTER;
                using ActionerMaker = ACTIONER<typename SpecializedDownset::value_type>;
                using InputPickerMaker = INPUT_PICKER;
                auto skn = k_bounded_safety_aut_detail<SpecializedDownset, IOsPrecomputationMaker, ActionerMaker,
                                     InputPickerMaker> (game.aut, kmin, kmax, kinc, all_inputs, all_outputs, IOS_PRECOMPUTER (), ACTIONER<typename SpecializedDownset::value_type> (),
                                                  INPUT_PICKER ());
                realizable = skn.solve ().has_value ();
              },
              UNREACHABLE, posets::vectors::nbools_to_nbitsets (nbitsetbools));
        },
        UNREACHABLE, actual_nonbools);
  }
  else {  // Vectors & Bitsets
    static_switch_t<STATIC_MAX_BITSETS> {}(
        [&] (auto vbitsets) {
          using SpecializedDownset =
              posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<posets::vectors::x_and_bitset<
                  posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>, vbitsets.value>>;

          using IOsPrecomputationMaker = IOS_PRECOMPUTER;
          using ActionerMaker = ACTIONER<typename SpecializedDownset::value_type>;
          using InputPickerMaker = INPUT_PICKER;
          auto skn = k_bounded_safety_aut_detail<SpecializedDownset, IOsPrecomputationMaker, ActionerMaker,
                                     InputPickerMaker> (game.aut, kmin, kmax, kinc, all_inputs, all_outputs, IOS_PRECOMPUTER (), ACTIONER<typename SpecializedDownset::value_type> (),
                                                  INPUT_PICKER ());
          realizable = skn.solve ().has_value ();
        },
        UNREACHABLE, posets::vectors::nbools_to_nbitsets (nbitsetbools));
  }

  return realizable;
}
