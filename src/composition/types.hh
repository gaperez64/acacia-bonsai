#pragma once
#include <memory>
#include "../configuration.hh"
#include "../vectors.hh"
#include "../downsets.hh"
#include "../utils/verbose.hh"
#include <spot/misc/bddlt.hh>
#include <spot/misc/escape.hh>
#include <spot/misc/timer.hh>
#include <spot/tl/formula.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twa/bddprint.hh>
#include <optional>

// downset type that does not depend on the exact automaton
using GenericDownset = downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<vectors::vector_backed<VECTOR_ELT_T>>;

// Safety game: contains the Büchi automaton and the number of nonboolean states
// may also contain a downset which is either the safe region if solved == true, or some overestimation if solved == false
// if this contains no safe region (safe == nullptr), then the game was solved and found to be losing for the controller
// finally it also includes the invariant that was used to solve the game
struct safety_game {
  spot::twa_graph_ptr aut;
  size_t bool_threshold = 0;
  std::shared_ptr<GenericDownset> safe;
  bool solved = false;
  bdd invariant = bddtrue;

  auto set_globals () {
    // set the global variables needed for boolean states to function correctly

    vectors::bool_threshold = bool_threshold; // number of nonboolean states

    // Compute how many boolean states will actually be put in bitsets.
    constexpr auto max_bools_in_bitsets = vectors::nbitsets_to_nbools (STATIC_MAX_BITSETS);
    auto nbitsetbools = aut->num_states () - vectors::bool_threshold;
    if (nbitsetbools > max_bools_in_bitsets) {
      verb_do (1, vout << "Warning: bitsets not large enough, using regular vectors for some Boolean states.\n"
                       /*   */ << "\tTotal # of Boolean-for-bitset states: " << nbitsetbools
                       /*   */ << ", max: " << max_bools_in_bitsets << std::endl);
      nbitsetbools = max_bools_in_bitsets;
    }

    constexpr auto STATIC_ARRAY_CAP_MAX =
    vectors::traits<vectors::ARRAY_IMPL, VECTOR_ELT_T>::capacity_for (STATIC_ARRAY_MAX);

    // Maximize usage of the nonbool implementation
    auto nonbools = aut->num_states () - nbitsetbools;
    size_t actual_nonbools = (nonbools <= STATIC_ARRAY_CAP_MAX) ?
    vectors::traits<vectors::ARRAY_IMPL, VECTOR_ELT_T>::capacity_for (nonbools) :
    vectors::traits<vectors::VECTOR_IMPL, VECTOR_ELT_T>::capacity_for (nonbools);
    if (actual_nonbools >= aut->num_states ())
      nbitsetbools = 0;
    else
      nbitsetbools -= (actual_nonbools - nonbools);

    vectors::bitset_threshold = aut->num_states () - nbitsetbools;

    verb_do (1, vout << "Bitset threshold set at " << vectors::bitset_threshold << "\n");

    // return two values needed for the specialized downset k-bounded safety automaton
    return std::pair<size_t, size_t> (nbitsetbools, actual_nonbools);
  }
};

// cast a vector (state in the safety game) to another type, for example to go from array+bitset to vector
template<typename To, typename From>
To cast_vector (From& f) {
  auto vec = utils::vector_mm<VECTOR_ELT_T> (f.size (), 0);
  for(size_t i = 0; i < f.size (); i++) {
    vec[i] = f[i];
  }
  return To (vec);
}

// cast a downset (set of safety game states) to another type
// TODO some downset types may be much faster with a bulk insert
template<typename To, typename From>
To cast_downset (From& f) {
  using NewVec = To::value_type;
  //To downset (cast_vector<NewVec> (*f.begin ()));
  std::vector<NewVec> vv;
  for(const auto& vec: f) {
    vv.push_back (cast_vector<NewVec> (vec));
    //downset.insert (cast_vector<NewVec> (vec));
  }
  To downset (std::move (vv));
  return downset;
}

// make an empty automaton
spot::twa_graph_ptr new_automaton (spot::bdd_dict_ptr dict) {
  spot::twa_graph_ptr aut = spot::make_twa_graph (dict);

  // single acceptance set, inf(0) acceptance condition, state-based acceptance
  aut->set_generalized_buchi (1);
  aut->set_acceptance (spot::acc_cond::inf ({0}));
  aut->prop_state_acc (true);

  return aut;
}

enum unreal_x_t {
  UNREAL_X_FORMULA = 'f',
  UNREAL_X_AUTOMATON = 'a',
  UNREAL_X_BOTH
};

enum job_type {
  j_solve,
  j_formula,
  j_done
};

enum result_type {
  r_game,
  r_invariant,
  r_null
};
