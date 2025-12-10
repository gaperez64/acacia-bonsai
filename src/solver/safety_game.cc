

#include <utility>

#include <spot/twaalgos/hoa.hh>


#include "types.hh"

#include "safety_game.hh"

  std::pair<unsigned long, unsigned long> safety_game::set_globals () {
    // set the global variables needed for boolean states to function correctly

    posets::vectors::bool_threshold = bool_threshold; // number of nonboolean states

    // Compute how many boolean states will actually be put in bitsets.
    constexpr auto max_bools_in_bitsets = posets::vectors::nbitsets_to_nbools (STATIC_MAX_BITSETS);
    auto nbitsetbools = aut->num_states () - posets::vectors::bool_threshold;
    if (nbitsetbools > max_bools_in_bitsets) {
      verb_do (1, vout << "Warning: bitsets not large enough, using regular vectors for some Boolean states.\n"
                       /*   */ << "\tTotal # of Boolean-for-bitset states: " << nbitsetbools
                       /*   */ << ", max: " << max_bools_in_bitsets << std::endl);
      nbitsetbools = max_bools_in_bitsets;
    }

    constexpr auto STATIC_ARRAY_CAP_MAX =
      posets::vectors::traits<posets::vectors::ARRAY_IMPL, VECTOR_ELT_T>::capacity_for (STATIC_ARRAY_MAX);

    // Maximize usage of the nonbool implementation
    auto nonbools = aut->num_states () - nbitsetbools;
    size_t actual_nonbools = (nonbools <= STATIC_ARRAY_CAP_MAX) ?
    posets::vectors::traits<posets::vectors::ARRAY_IMPL, VECTOR_ELT_T>::capacity_for (nonbools) :
    posets::vectors::traits<posets::vectors::VECTOR_IMPL, VECTOR_ELT_T>::capacity_for (nonbools);
    if (actual_nonbools >= aut->num_states ())
      nbitsetbools = 0;
    else
      nbitsetbools -= (actual_nonbools - nonbools);

    posets::vectors::bitset_threshold = aut->num_states () - nbitsetbools;

    verb_do (1, vout << "Bitset threshold set at " << posets::vectors::bitset_threshold << "\n");

    // return two values needed for the specialized downset k-bounded safety automaton
    return std::pair<size_t, size_t> (nbitsetbools, actual_nonbools);
  }

  safety_game::safety_game(spot::twa_graph_ptr aut, unsigned k_min, size_t bool_threshold) {
    this->aut = aut;
    this->bool_threshold = bool_threshold;
    this->solved = false;
    this->set_globals ();

    auto all_k = posets::utils::vector_mm<VECTOR_ELT_T> (aut->num_states (), k_min - 1);
    for (size_t i = posets::vectors::bool_threshold; i < aut->num_states (); ++i)
      all_k[i] = 0;
    this->safe = std::make_shared<GenericDownset> (GenericDownset::value_type (all_k));
  }

