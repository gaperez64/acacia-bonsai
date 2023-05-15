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
      printf("Expected %08x got %08x\n", expected, result);
      fflush(stdout);
      assert(false);
    }
  }

  void write_downset (GenericDownset& downset) {
    write_obj<int> (DOWNSET_START);

    write_obj<int> (downset.size ()); // number of dominating elements
    write_obj<int> ((*downset.begin()).size ()); // number of values per element (= number of states in automaton)

    for(auto& state: downset) {
      //printf("j: %d\n", j++); fflush(stdout);
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

  void write_bdd (bdd b, spot::bdd_dict_ptr dict) {
    write_obj<int> (BDD_START);

    // turn the BDD into a formula, turn that into a string, and send this string
    spot::formula f = spot::bdd_to_formula (b, dict);
    std::stringstream stream;
    stream << f;
    std::string str = stream.str ();

    write_obj<int> (str.size ());
    for(char c: str) {
      write_obj<char> (c);
    }

    //printf("wrote str: %s\n", str.c_str()); fflush(stdout);


    write_obj<int> (BDD_END);
  }

  bdd read_bdd (spot::bdd_dict_ptr dict) {
    assert_read<int> (BDD_START);

    // read a string, parse it into a formula
    int size = read_obj<int> ();
    std::string str;
    str.resize(size);
    for(int i = 0; i < size; i++) {
      str[i] = read_obj<char> ();
    }
    assert_read<int> (BDD_END);

    //printf("str: %s\n", str.c_str()); fflush(stdout);


    spot::formula formula = spot::parse_formula (str);
    bdd res = spot::formula_to_bdd (formula, dict, this);
    dict->unregister_all_my_variables (this);

    //utils::vout << "str: " << str << "\n res: " << spot::bdd_to_formula(res, dict) << "\n";
    return res;
  }

  void write_automaton (spot::twa_graph_ptr aut) {
    write_obj<int> (AUTOMATON_START);

    aut->merge_edges();

    write_obj<int> (aut->num_states ());

    int num_edges = 0;
    for(auto& _: aut->edges ()) num_edges++;
    assert(num_edges == (int)aut->num_edges ());
    //write_obj<int> (num_edges);
    //utils::vout << "Number of edges: " << num_edges << " (func gives " << aut->num_edges () << ")\n";
    write_obj<int> (aut->num_edges ());
    write_obj<int> (aut->get_init_state_number ());

    for(unsigned i = 0; i < aut->num_states (); i++) {
      bool acc = aut->state_is_accepting (i);
      write_obj<char> (acc);
    }

    for(auto& edge: aut->edges ()) {
      //utils::vout << "Edge: " << edge.src << " -> " << edge.dst << " (" << spot::bdd_to_formula(edge.cond, aut->get_dict()) << ")\n";
      write_obj<int> (edge.src);
      write_obj<int> (edge.dst);
      write_bdd (edge.cond, aut->get_dict ());
    }
    //assert(edge_count == aut->num_edges());

    write_obj<int> (AUTOMATON_END);
  }

  spot::twa_graph_ptr read_automaton (spot::bdd_dict_ptr dict) {
    assert_read<int> (AUTOMATON_START);

    spot::twa_graph_ptr aut = std::make_shared<spot::twa_graph> (dict);
    aut->set_generalized_buchi (1);
    aut->set_acceptance (spot::acc_cond::inf ({0}));
    aut->prop_state_acc (true);

    int states = read_obj<int> ();
    for(int i = 0; i < states; i++) {
      assert ((int)aut->new_state () == i);
    }

    int edges = read_obj<int> ();
    int init = read_obj<int> ();
    aut->set_init_state (init);

    std::vector<bool> acc (states);
    for(int i = 0; i < states; i++) {
      acc[i] = read_obj<char> ();
    }

    for(int i = 0; i < edges; i++) {
      int src = read_obj<int> ();
      int dst = read_obj<int> ();
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

