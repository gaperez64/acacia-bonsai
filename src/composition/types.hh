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

using GenericDownset = downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<vectors::vector_backed<VECTOR_ELT_T>>;

struct aut_ret {
  spot::twa_graph_ptr aut;
  size_t bool_threshold;
  //bdd all_inputs, all_outputs;
  std::shared_ptr<GenericDownset> safe;
  bool solved;

  auto set_globals () {
    vectors::bool_threshold = bool_threshold; // number of boolean states

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

    //utils::vout << "Bitset threshold set at " << vectors::bitset_threshold << "\n";
    //utils::vout << "Thought it was " << bitset_threshold << "\n";
    //bitset_threshold = vectors::bitset_threshold;

    return std::pair<size_t, size_t>(nbitsetbools, actual_nonbools);
  }
};


template<typename To, typename From>
To cast_vector (From& f) {
  auto vec = utils::vector_mm<VECTOR_ELT_T>(f.size(), 0);
  for(size_t i = 0; i < f.size(); i++) {
    vec[i] = f[i];
  }
  return To(vec);
}

template<typename To, typename From>
To cast_downset (From& f) {
  using NewVec = To::value_type;
  To downset(cast_vector<NewVec>(*f.begin()));
  for(const auto& vec: f) {
    downset.insert(cast_vector<NewVec>(vec));
  }
  return downset;
}