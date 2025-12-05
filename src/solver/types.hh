#pragma once
#include <memory>
#include "../configuration.hh"
#include <posets/vectors.hh>
#include <posets/downsets.hh>
#include "../utils/verbose.hh"
#include <spot/misc/bddlt.hh>
#include <spot/misc/escape.hh>
#include <spot/misc/timer.hh>
#include <spot/tl/formula.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twa/bddprint.hh>
#include <optional>


// cast a vector (state in the safety game) to another type, for example to go from array+bitset to vector
template<typename To, typename From>
To cast_vector (From& f) {
  auto vec = posets::utils::vector_mm<VECTOR_ELT_T> (f.size (), 0);
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
inline spot::twa_graph_ptr new_automaton (spot::bdd_dict_ptr dict) {
  spot::twa_graph_ptr aut = spot::make_twa_graph (dict);

  // single acceptance set, inf(0) acceptance condition, state-based acceptance
  aut->set_generalized_buchi (1);
  aut->set_acceptance (spot::acc_cond::inf ({0}));
  aut->prop_state_acc (true);

  return aut;
}
