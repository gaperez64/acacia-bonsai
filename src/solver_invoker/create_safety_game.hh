#pragma once

#include <spot/twaalgos/hoa.hh>
#include <spot/twaalgos/translate.hh>
#include <utility>

#include "aut_preprocessors.hh"
#include "boolean_states.hh"
#include "types.hh"


// downset type that does not depend on the exact automaton
using GenericDownset = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<posets::vectors::vector_backed<VECTOR_ELT_T>>;

// Safety game: contains the Büchi automaton and the number of nonboolean states
// may also contain a downset which is either the safe region if solved == true, or some overestimation if solved == false
// if this contains no safe region (safe == nullptr), then the game was solved and found to be losing for the controller
// finally it also includes the invariant that was used to solve the game
// TODO: this needs to be cleaned up made into a proper class.
struct safety_game {
  spot::twa_graph_ptr aut;
  size_t bool_threshold = 0;
  std::shared_ptr<GenericDownset> safe;
  bool solved = false;
  bdd invariant = bddtrue;

  auto set_globals () {
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
};


inline spot::twa_graph_ptr create_automaton(spot::formula f, spot::translator &trans) {
  // To Universal co-Büchi Automaton
  trans.set_type(spot::postprocessor::BA);
  // "Desired characteristics": Small and state-based acceptance (implied by BA).
  trans.set_pref(spot::postprocessor::Small |
                  //spot::postprocessor::Complete | // TODO: We did not need that originally; do we now?
                  spot::postprocessor::SBAcc);
  f = spot::formula::Not (f);
  verb_do (1, vout << "Formula: " << f << std::endl);
  auto aut = trans.run (&f);
  return aut;
}


inline safety_game create_game(spot::twa_graph_ptr aut, size_t bool_threshold, unsigned K_min) {
  safety_game ret;
  ret.aut = aut;
  ret.bool_threshold = posets::vectors::bool_threshold;
  ret.solved = false;
  ret.set_globals ();

  auto all_k = posets::utils::vector_mm<VECTOR_ELT_T> (aut->num_states (), K_min - 1);
  for (size_t i = posets::vectors::bool_threshold; i < aut->num_states (); ++i)
    all_k[i] = 0;
  ret.safe = std::make_shared<GenericDownset> (GenericDownset::value_type (all_k));

  return ret;
}

// TODO: move to CTOR
inline safety_game prepare_formula (spot::formula f, spot::translator &trans, bdd all_inputs, bdd all_outputs,
  unsigned K, unsigned K_min) {
  // Note: this function is only run once with unrealizability as there is no composition -> swapping the inputs/outputs only happens once

  // TODO: move outside
  spot::process_timer timer;
  timer.start ();

  spot::stopwatch sw, sw_nospot;
  bool want_time = true; // Hardcoded

  if (want_time) {
    sw.start ();
  }

  auto aut = create_automaton(std::move(f), trans);

  if (want_time) {
    double trans_time = sw.stop ();
    verb_do (1, vout << "Translating formula done in "
                << trans_time << " seconds\n");
    verb_do (1, vout << "Automaton has " << aut->num_states ()
                << " states and " << aut->num_sets () << " colors\n");
  }

  ////////////////////////////////////////////////////////////////////////
  // Preprocess automaton

  if (want_time) {
    sw.start();
    sw_nospot.start ();
  }

  // TODO: move outside
  // auto aut_preprocessors_maker = AUT_PREPROCESSOR ();
  // // NOTE: this warns about non-trivial types going into variadic args. This is only relevant
  // //  for the "no_preprocessing" implementation.
  // (aut_preprocessors_maker.make (aut, all_inputs, all_outputs, K)) ();

  aut_preprocessors::standard::make (aut, all_inputs, all_outputs, K) ();
  // aut_preprocessors::surely_losing::make (aut, all_inputs, all_outputs, K) ();

  if (want_time) {
    double merge_time = sw.stop();
    verb_do (1, vout << "Preprocessing done in " << merge_time
                << " seconds\nDPA has " << aut->num_states()
                << " states\n");
  }
  verb_do (2, spot::print_hoa (utils::vout, aut, nullptr));

  ////////////////////////////////////////////////////////////////////////
  // Boolean states

  // TODO: all this timer stuff will be moved to "acacia-bonsai" main function.
  if (want_time)
    sw.start ();

  // only once, but need to choose
  // TODO: make argument, move outside
  auto boolean_states_maker = BOOLEAN_STATES ();
  posets::vectors::bool_threshold = (boolean_states_maker.make (aut, K)) ();

  if (want_time) {
    double boolean_states_time = sw.stop ();
    verb_do (1, vout << "Computation of boolean states in " << boolean_states_time
      /*          */ << "seconds , found " << posets::vectors::bool_threshold << " nonboolean states.\n");
  }


  ////////////////////////////////////////////////////////////////////////
  // Build S^K_N game, solve it.

  if (want_time)
    sw.start ();

  auto ret = create_game(aut, posets::vectors::bool_threshold, K_min);


  if (want_time) {
    double solve_time = sw.stop ();
    verb_do (1, vout << "Safety game created in " << solve_time << " seconds\n");
    verb_do (1, vout << "Time disregarding Spot translation: " << sw_nospot.stop () << " seconds\n");
  }

  timer.stop ();

  return ret;
}




