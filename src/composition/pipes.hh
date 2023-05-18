//
// Created by nils on 09/05/23.
//

#pragma once

#include <unistd.h>
#include <cassert>
#include <sstream>

// magic values
const int MESSAGE_START = 0x4603C330;
const int MESSAGE_END = 0x71DA7405;

const int DOWNSET_START = 0x12345678;
const int DOWNSET_END = 0x1BCDEF0A;

const int BDD_START = 0x61C50880;
const int BDD_END = 0x3BB82F55;

const int AUTOMATON_START = 0x1381C72A;
const int AUTOMATON_END = 0x5C5D2324;

const int RESULT_START = 0x6B9A44C0;
const int RESULT_END = 0x08BA7101;



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

  int get_bytes_count () {
    int t = byte_count;
    byte_count = 0;
    return t;
  }

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

  template<class T>
  void assert_read (const T& expected) {
    T result = read_obj<T> ();
    if (result != expected) {
      printf ("Expected %08x got %08x\n", expected, result);
      fflush (stdout);
      assert (false);
    }
  }

  void write_string (const std::string& str) {
    write_obj<size_t> (str.size ());
    for(char c: str) {
      write_obj<char> (c);
    }
  }

  std::string read_string () {
    size_t size = read_obj<size_t> ();
    std::string str;
    str.resize (size);
    for(size_t i = 0; i < size; i++) {
      str[i] = read_obj<char> ();
    }
    return str;
  }

  void write_downset (GenericDownset& downset) {
    write_obj<int> (DOWNSET_START);

    write_obj<int> (downset.size ()); // number of dominating elements
    write_obj<int> ((*downset.begin ()).size ()); // number of values per element (= number of states in automaton)

    for(auto& state: downset) {
      for(auto& value: state) {
        write_obj<VECTOR_ELT_T> (value);
      }
    }

    write_obj<int> (DOWNSET_END);
  }

  std::shared_ptr<GenericDownset> read_downset () {
    assert_read<int> (DOWNSET_START);

    int downset_size = read_obj<int> ();
    int element_size = read_obj<int> ();

    std::shared_ptr<GenericDownset> result;

    for(int j = 0; j < downset_size; j++) {
      auto vec = utils::vector_mm<VECTOR_ELT_T> (element_size, 0);
      for (int i = 0; i < element_size; i++) {
        vec[i] = read_obj<VECTOR_ELT_T> ();
      }
      if (result == nullptr) {
        result = std::make_shared<GenericDownset> (GenericDownset::value_type (vec));
      }
      else result->insert (GenericDownset::value_type (vec));
    }

    assert_read<int> (DOWNSET_END);
    return result;
  }

  void write_formula (spot::formula f) {
    // turn it into a string and write this string
    std::stringstream stream;
    stream << f;
    write_string (stream.str ());
  }

  spot::formula read_formula () {
    std::string str = read_string ();
    return spot::parse_formula (str);
  }

  void write_bdd (bdd b, spot::bdd_dict_ptr dict) {
    write_obj<int> (BDD_START);

    // turn the BDD into a formula, write that
    spot::formula f = spot::bdd_to_formula (b, dict);
    write_formula (f);

    write_obj<int> (BDD_END);
  }

  bdd read_bdd (spot::bdd_dict_ptr dict) {
    assert_read<int> (BDD_START);

    spot::formula formula = read_formula ();
    assert_read<int> (BDD_END);

    bdd res = spot::formula_to_bdd (formula, dict, this);
    dict->unregister_all_my_variables (this);

    return res;
  }

  void write_automaton (spot::twa_graph_ptr aut) {
    write_obj<int> (AUTOMATON_START);

    aut->merge_edges();

    write_obj<unsigned> (aut->num_states ());
    write_obj<unsigned> (aut->num_edges ());
    write_obj<unsigned> (aut->get_init_state_number ());

    for(unsigned i = 0; i < aut->num_states (); i++) {
      bool acc = aut->state_is_accepting (i);
      write_obj<char> (acc);
    }

    unsigned edge_count = 0;
    for(auto& edge: aut->edges ()) {
      //utils::vout << "Edge: " << edge.src << " -> " << edge.dst << " (" << spot::bdd_to_formula(edge.cond, aut->get_dict()) << ")\n";
      write_obj<unsigned> (edge.src);
      write_obj<unsigned> (edge.dst);
      write_bdd (edge.cond, aut->get_dict ());
      edge_count++;
    }
    assert (edge_count == aut->num_edges());

    write_obj<int> (AUTOMATON_END);
  }

  spot::twa_graph_ptr read_automaton (spot::bdd_dict_ptr dict) {
    assert_read<int> (AUTOMATON_START);

    spot::twa_graph_ptr aut = std::make_shared<spot::twa_graph> (dict);
    aut->set_generalized_buchi (1);
    aut->set_acceptance (spot::acc_cond::inf ({0}));
    aut->prop_state_acc (true);

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

    assert_read<int> (AUTOMATON_END);
    return aut;
  }

  void write_result (aut_ret& r) {
    write_obj<int> (RESULT_START);

    if (r.aut) {
      write_obj<char> (1);
      write_automaton (r.aut);
    }
    else write_obj<char> (0);

    write_obj<size_t> (r.bool_threshold);

    if (r.safe) {
      write_obj<char> (1);
      write_downset (*r.safe);
    }
    else write_obj<char> (0);

    write_obj<bool> (r.solved);

    write_obj<int> (RESULT_END);
  }

  aut_ret read_result (spot::bdd_dict_ptr dict) {
    assert_read<int> (RESULT_START);

    aut_ret r;
    char has_aut = read_obj<char> ();
    if (has_aut) r.aut = read_automaton (dict);

    r.bool_threshold = read_obj<size_t> ();

    char has_safe = read_obj<char> ();
    if (has_safe) r.safe = std::shared_ptr<GenericDownset> (read_downset ());

    r.solved = read_obj<bool> ();

    assert_read<int> (RESULT_END);

    return r;
  }
};

