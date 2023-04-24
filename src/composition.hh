//
// Created by nils on 08/04/23.
//

#pragma once

#include <iostream>
#include <spot/twa/fwd.hh>
#include <bddx.h>
#include <spot/twa/bddprint.hh>
#include "utils/verbose.hh"
#include "configuration.hh"
#include "vectors.hh"
#include "downsets.hh"
#include "k-bounded_safety_aut.hh"
#include "utils/typeinfo.hh"


using GenericDownset = downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<vectors::vector_backed<VECTOR_ELT_T>>;

struct aut_ret {
  spot::twa_graph_ptr aut;
  size_t bool_threshold;
  //size_t bitset_threshold;
  //size_t actual_nonbools;
  //size_t nbitsetbools;
  bdd all_inputs, all_outputs;
  std::optional<GenericDownset> safe;

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



// from https://spot.lre.epita.fr/tut21.html
void custom_print (std::ostream& out, spot::twa_graph_ptr& aut)
{
  // We need the dictionary to print the BDDs that label the edges
  const spot::bdd_dict_ptr& dict = aut->get_dict();

  // Some meta-data...
  //out << "Acceptance: " << aut->get_acceptance() << '\n';
  //out << "Number of sets: " << aut->num_sets() << '\n';
  out << "Number of states: " << aut->num_states() << '\n';
  out << "Number of edges: " << aut->num_edges() << '\n';
  out << "Initial state: " << aut->get_init_state_number() << '\n';
  out << "Atomic propositions:";
  for (spot::formula ap: aut->ap())
    out << ' ' << ap << " (=" << dict->varnum(ap) << ')';
  out << '\n';

  // Arbitrary data can be attached to automata, by giving them
  // a type and a name.  The HOA parser and printer both use the
  // "automaton-name" to name the automaton.
  if (auto name = aut->get_named_prop<std::string>("automaton-name"))
    out << "Name: " << *name << '\n';

  // For the following prop_*() methods, the return value is an
  // instance of the spot::trival class that can represent
  // yes/maybe/no.  These properties correspond to bits stored in the
  // automaton, so they can be queried in constant time.  They are
  // only set whenever they can be determined at a cheap cost: for
  // instance an algorithm that always produces deterministic automata
  // would set the deterministic property on its output.  In this
  // example, the properties that are set come from the "properties:"
  // line of the input file.
  //out << "Complete: " << aut->prop_complete() << '\n';
  //out << "Deterministic: " << (aut->prop_universal()
  //&& aut->is_existential()) << '\n';
  //out << "Unambiguous: " << aut->prop_unambiguous() << '\n';
  //out << "State-Based Acc: " << aut->prop_state_acc() << '\n';
  //out << "Terminal: " << aut->prop_terminal() << '\n';
  //out << "Weak: " << aut->prop_weak() << '\n';
  //out << "Inherently Weak: " << aut->prop_inherently_weak() << '\n';
  //out << "Stutter Invariant: " << aut->prop_stutter_invariant() << '\n';

  // States are numbered from 0 to n-1
  unsigned n = aut->num_states();
  for (unsigned s = 0; s < n; ++s)
  {
    out << "State " << s << ":\n";

    // The out(s) method returns a fake container that can be
    // iterated over as if the contents was the edges going
    // out of s.  Each of these edges is a quadruplet
    // (src,dst,cond,acc).  Note that because this returns
    // a reference, the edge can also be modified.
    for (auto& t: aut->out(s))
    {
      out << "  edge(" << t.src << " -> " << t.dst << ")\n    label = ";
      spot::bdd_print_formula(out, dict, t.cond);
      out << "\n";
      if (t.acc.count() > 0) out << "    acc sets = " << t.acc << '\n';
    }
  }
}



class composition {
  private:
  std::vector<unsigned int> rename;

  // Concatenate two vectors, taking into a account a new initial state is added, + the states are renamed
  auto combine_vectors (const auto& m1, const auto& m2) {
    //utils::vout << ": " << m1 << " " << m2 << "\n";

    auto vec = utils::vector_mm<VECTOR_ELT_T>(m1.size () + m2.size () + 1, 0);

    for (size_t i = 0; i < m1.size (); ++i)
      vec[rename[i]] = m1[i];

    for (size_t i = 0; i < m2.size (); ++i)
      vec[rename[i + m1.size () + 1]] = m2[i];

    vec[rename[m1.size ()]] = 0; // new init state
    return GenericDownset::value_type (vec);
  }

  public:
  composition () {
    //utils::vout << "I am " << get_typename(*this) << "\n";
  }

  // Merge src automaton into dest
  void merge_aut (aut_ret& dest, aut_ret& src) {
    //utils::vout << "Merge\n";
    //utils::vout << "------------\n";
    //custom_print (utils::vout, dest.aut);
    //utils::vout << "------------\n";
    //custom_print (utils::vout, src.aut);
    //utils::vout << "------------\n";

    // note to self: don't need new init state if original init state (in one of the two automata) has no incoming transitions
    unsigned int offset = dest.aut->num_states () + 1; // + 1 because of the new init state

    // add new initial state which has all transitions of both initial states
    unsigned int new_init = dest.aut->new_state ();

    // copy states/transitions from src to dest
    for(unsigned int s = 0; s < src.aut->num_states (); s++) {
      unsigned int s_new = dest.aut->new_state ();
      for(auto& t: src.aut->out (s)) {
        assert(s_new == t.src+offset);
        dest.aut->new_edge(s_new, t.dst + offset, t.cond, t.acc);

        if (s == src.aut->get_init_state_number ()) {
          dest.aut->new_edge(new_init, t.dst + offset, t.cond, t.acc);
        }
      }
    }

    // copy dest's initial state's transitions to the new initial state
    for(unsigned int s = 0; s < offset-1; s++) {
      if (s == dest.aut->get_init_state_number ()) {
        for(auto& t: dest.aut->out (s)) {
          dest.aut->new_edge (new_init, t.dst, t.cond, t.acc);
        }
      }
    }
    dest.aut->set_init_state (new_init);

    // rename states to put boolean states at the end
    unsigned int index_nonbool = 0; // where non-boolean states start
    unsigned int index_bool = dest.bool_threshold + src.bool_threshold + 1; // where boolean states start

    rename.resize(dest.aut->num_states ());
    for(unsigned int s = 0; s < rename.size (); s++) {
      if (s < new_init) {
        // this state belonged to dest.aut
        if (s < dest.bool_threshold) {
          //utils::vout << s << " is nonbool\n";
          rename[s] = index_nonbool++;
        }
        else {
          //utils::vout << s << " is bool\n";
          rename[s] = index_bool++;
        }
      }
      else if (s == new_init) {
        // new initial state is non bool?
        //utils::vout << s << " is nonbool?\n";
        rename[s] = index_nonbool++;
      }
      else {
        // this state belonged to src.aut
        if ((s - new_init - 1) < src.bool_threshold) {
          //utils::vout << s << " is nonbool\n";
          rename[s] = index_nonbool++;
        }
        else {
          //utils::vout << s << " is bool\n";
          rename[s] = index_bool++;
        }
      }
    }
    assert(index_nonbool == dest.bool_threshold + src.bool_threshold + 1);
    assert(index_bool == dest.aut->num_states ());

    //utils::vout << "Rename: " << rename << "\n";

    // WARNING: Internal Spot
    auto& g = dest.aut->get_graph();
    g.rename_states_(rename);
    dest.aut->set_init_state(rename[dest.aut->get_init_state_number()]);
    g.sort_edges_();
    g.chain_edges_();
    dest.aut->prop_universal(spot::trival::maybe ());


    dest.bool_threshold += src.bool_threshold + 1;
    dest.set_globals ();

    //utils::vout << "---------------------->\n";
    //custom_print (utils::vout, dest.aut);
  }



  auto merge_saferegions (GenericDownset& F1, GenericDownset& F2) {
    GenericDownset merged(combine_vectors(*F1.begin(), *F2.begin())); // need a first element for the constructor to work
    for(const auto& m1: F1) {
      for(const auto& m2: F2) {
        merged.insert(combine_vectors(m1, m2));
      }
    }

    return merged;
  }
};
