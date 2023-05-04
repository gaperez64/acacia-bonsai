//
// Created by nils on 04/05/23.
//

#pragma once

#include "types.hh"

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

thread_local std::vector<unsigned int> rename2;

void f(const std::vector<unsigned int>& v, void*) {
  rename2 = v;
};

class composition {
  private:
  std::vector<unsigned int> rename;
  unsigned int aut_size; // number of states in the final merged automaton

  // Concatenate two vectors, taking into a account a new initial state is added, + the states are renamed
  auto combine_vectors (const auto& m1, const auto& m2) {
    assert(aut_size > 0);
    auto vec = utils::vector_mm<VECTOR_ELT_T>(aut_size, 0);

    for (size_t i = 0; i < m1.size (); ++i) {
      if (rename[i] != -1u) {
        vec[rename[i]] = m1[i];
      }
    }

    for (size_t i = 0; i < m2.size (); ++i) {
      if (rename[i + m1.size () + 1] != -1u) {
        vec[rename[i + m1.size () + 1]] = m2[i];
      }
    }

    //assert(rename[m1.size ()] < 10000);
    //vec[rename[m1.size ()]] = 0; // new init state
    return GenericDownset::value_type (vec);
  }

  public:
  composition () {
    //utils::vout << "I am " << get_typename(*this) << "\n";
    aut_size = 0;
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

    //utils::vout << "Rename:  " << rename << "\n";

    // WARNING: Internal Spot
    auto& g = dest.aut->get_graph();
    g.rename_states_(rename);
    dest.aut->set_init_state(rename[dest.aut->get_init_state_number()]);
    g.sort_edges_();
    g.chain_edges_();
    dest.aut->prop_universal(spot::trival::maybe ());

    //
    //custom_print (utils::vout, dest.aut);

    rename2.clear();
    spot::twa_graph::shift_action w = &f;
    dest.aut->purge_unreachable_states (&w);
    // ^ spot already renames the states again, but we need to remember how they were renamed
    //   to make sure merging the downsets happens correctly

    //utils::vout << "Rename2: " << rename2 << "\n";

    if (!rename2.empty()) {
      // code assumes that the renaming is in order, e.g. [0, -1, 1, 2] to get rid of what was state 1,
      // never [0, -1, 2, 1] for instance.
      assert(rename.size() == rename2.size());

      for (unsigned int s = 0; s < rename.size (); s++) {
        if (rename2[rename[s]] == -1u) {
          //utils::vout << "State " << s << " which was renamed to " << rename[s] << " is now removed ";
          if (rename[s] < (dest.bool_threshold + src.bool_threshold + 1)) {
            dest.bool_threshold--;
            //utils::vout << "(nonbool)\n";
          } //else utils::vout << "(bool)\n";
        }
        //else utils::vout << "State " << s << " which was renamed to " << rename[s] << " is now renamed to " << rename2[rename[s]] << "\n";
        rename[s] = rename2[rename[s]];
      }
    }

    dest.bool_threshold += src.bool_threshold + 1;
    dest.set_globals ();
    aut_size = dest.aut->num_states ();

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