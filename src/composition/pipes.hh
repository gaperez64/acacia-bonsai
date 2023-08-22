//
// Created by nils on 09/05/23.
//

#pragma once

#include <unistd.h>
#include <cassert>
#include <sstream>
#include "types.hh"

// magic values
const int MESSAGE_START = 0x4603C330;
const int MESSAGE_END = 0x71DA7405;

const int DOWNSET_START = 0x12345678;
const int DOWNSET_END = 0x1BCDEF0A;

const int BDD_START = 0x61C50880;
const int BDD_END = 0x3BB82F55;

const int AUTOMATON_START = 0x1381C72A;
const int AUTOMATON_END = 0x5C5D2324;

const int SAFETYGAME_START = 0x6B9A44C0;
const int SAFETYGAME_END = 0x08BA7101;

const int FORMULA_START = 0x2A053F12;
const int FORMULA_END = 0x198AB311;

const int STRING_START = 0x093184CD;
const int STRING_END = 0x5E183BD2;

// wrapper around a pipe with functions to read/write basic types and bigger structs
class pipe_t {
  public:

  union {
    struct { int r, w; };
    int fd[2];
  };
  int byte_count = 0;

  public:
  pipe_t () {
    // let's not create the pipe() here because we're also not closing it in the destructor
    r = w = -1;
  }

  int create_pipe () {
    return pipe (fd);
  }

  // return how many bytes were read/written + reset the counter
  int get_bytes_count () {
    int t = byte_count;
    byte_count = 0;
    return t;
  }

  // write/read any basic type
  template<class T>
  void write_obj (const T& value) {
    int ret = write (w, &value, sizeof (T));
    assert(ret > 0);
    byte_count += ret;
  }

  template<class T>
  T read_obj () {
    T value;
    int ret = read (r, &value, sizeof (T));
    assert(ret > 0);
    byte_count += ret;
    return value;
  }

  // write/read guards around structs for debugging purposes
#ifndef NDEBUG
  void write_guard (int value) {
    write_obj<int> (value);
  }

  void read_guard (int expected) {
    int result = read_obj<int> ();
    if (result != expected) {
      printf ("Expected %08x got %08x\n", expected, result);
      fflush (stdout);
      assert (false);
    }
  }
#else
  void write_guard (int value) {
    return;
  }

  void read_guard (int expected) {
    return;
  }
#endif

  void write_string (const std::string& str) {
    write_guard (STRING_START);
    write_obj<size_t> (str.size ());
    for(char c: str) {
      write_obj<char> (c);
    }
    write_guard (STRING_END);
  }

  std::string read_string () {
    read_guard (STRING_START);
    size_t size = read_obj<size_t> ();
    std::string str;
    str.resize (size);
    for(size_t i = 0; i < size; i++) {
      str[i] = read_obj<char> ();
    }
    read_guard (STRING_END);
    return str;
  }

  void write_downset (GenericDownset& downset) {
    write_guard (DOWNSET_START);

    write_obj<int> (downset.size ()); // number of dominating elements
    write_obj<int> ((*downset.begin ()).size ()); // number of values per element (= number of states in automaton)

    for(auto& state: downset) {
      for(auto& value: state) {
        write_obj<VECTOR_ELT_T> (value);
      }
    }

    write_guard (DOWNSET_END);
  }

  std::shared_ptr<GenericDownset> read_downset () {
    read_guard (DOWNSET_START);

    int downset_size = read_obj<int> ();
    int element_size = read_obj<int> ();

    std::shared_ptr<GenericDownset> result;
    std::vector<GenericDownset::value_type> elements;

    for(int j = 0; j < downset_size; j++) {
      auto vec = utils::vector_mm<VECTOR_ELT_T> (element_size, 0);
      for (int i = 0; i < element_size; i++) {
        vec[i] = read_obj<VECTOR_ELT_T> ();
      }
      /*if (result == nullptr) {
        result = std::make_shared<GenericDownset> (GenericDownset::value_type (vec));
      }
      else result->insert (GenericDownset::value_type (vec));
      */
      elements.push_back (GenericDownset::value_type (vec));
    }
    result = std::make_shared<GenericDownset> (std::move (elements));

    read_guard (DOWNSET_END);
    return result;
  }

  void write_formula (spot::formula f) {
    write_guard (FORMULA_START);
    // turn it into a string and write this string
    // maybe write the parse tree instead?
    std::stringstream stream;
    stream << f;
    write_string (stream.str ());
    write_guard (FORMULA_END);
  }

  spot::formula read_formula () {
    read_guard (FORMULA_START);
    std::string str = read_string ();
    read_guard (FORMULA_END);
    return spot::parse_formula (str);
  }

  void write_bdd (bdd b, spot::bdd_dict_ptr dict) {
    write_guard (BDD_START);

    // turn the BDD into a formula, write that
    spot::formula f = spot::bdd_to_formula (b, dict);
    write_formula (f);

    write_guard (BDD_END);
  }

  bdd read_bdd (spot::bdd_dict_ptr dict) {
    read_guard (BDD_START);

    spot::formula formula = read_formula ();
    bdd res = spot::formula_to_bdd (formula, dict, this);
    dict->unregister_all_my_variables (this);

    read_guard (BDD_END);
    return res;
  }

  void write_automaton (spot::twa_graph_ptr aut) {
    write_guard (AUTOMATON_START);

    aut->merge_edges();

    // write the number of states/edges + init state number
    write_obj<unsigned> (aut->num_states ());
    write_obj<unsigned> (aut->num_edges ());
    write_obj<unsigned> (aut->get_init_state_number ());

    // write for every state whether or not it is accepting
    for(unsigned i = 0; i < aut->num_states (); i++) {
      bool acc = aut->state_is_accepting (i);
      write_obj<char> (acc);
    }

    // write all the edges
    unsigned edge_count = 0;
    for(auto& edge: aut->edges ()) {
      write_obj<unsigned> (edge.src);
      write_obj<unsigned> (edge.dst);
      write_bdd (edge.cond, aut->get_dict ());
      edge_count++;
    }
    assert (edge_count == aut->num_edges ());

    write_guard (AUTOMATON_END);
  }

  spot::twa_graph_ptr read_automaton (spot::bdd_dict_ptr dict) {
    read_guard (AUTOMATON_START);

    spot::twa_graph_ptr aut = new_automaton (dict);

    unsigned states = read_obj<unsigned> ();
    for(unsigned i = 0; i < states; i++) {
      assert (aut->new_state () == i);
    }

    unsigned edges = read_obj<unsigned> ();
    unsigned init = read_obj<unsigned> ();
    aut->set_init_state (init);

    std::vector<bool> acc (states);
    for(unsigned i = 0; i < states; i++) {
      acc[i] = read_obj<char> ();
    }

    for(unsigned i = 0; i < edges; i++) {
      unsigned src = read_obj<unsigned> ();
      unsigned dst = read_obj<unsigned> ();
      bdd cond = read_bdd (dict);
      if (acc[src]) {
        aut->new_acc_edge (src, dst, cond);
      } else {
        aut->new_edge (src, dst, cond);
      }
    }

    read_guard (AUTOMATON_END);
    return aut;
  }

  void write_safety_game (safety_game& r) {
    write_guard (SAFETYGAME_START);

    // write the automaton, if it has one
    if (r.aut) {
      write_obj<char> (1);
      write_automaton (r.aut);
      // + invariant here as we use the automaton dict
      write_bdd (r.invariant, r.aut->get_dict ());
    }
    else write_obj<char> (0);

    // number of nonboolean states
    write_obj<size_t> (r.bool_threshold);

    // write the safe region if it has one
    if (r.safe) {
      write_obj<char> (1);
      write_downset (*r.safe);
    }
    else write_obj<char> (0);

    // write whether this safe region is exact or not
    write_obj<char> (r.solved);

    write_guard (SAFETYGAME_END);
  }

  safety_game read_safety_game (spot::bdd_dict_ptr dict) {
    read_guard (SAFETYGAME_START);

    safety_game r;
    char has_aut = read_obj<char> ();
    r.aut = has_aut ? read_automaton (dict) : nullptr;
    //r.invariant = has_aut ? read_bdd (dict) : bddtrue;
    r.invariant = bddtrue;
    if (has_aut) r.invariant = read_bdd (dict);

    r.bool_threshold = read_obj<size_t> ();

    char has_safe = read_obj<char> ();
    r.safe = has_safe ? std::shared_ptr<GenericDownset> (read_downset ()) : nullptr;

    r.solved = read_obj<char> ();

    read_guard (SAFETYGAME_END);
    return r;
  }
};

