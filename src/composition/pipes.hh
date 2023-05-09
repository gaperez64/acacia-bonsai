//
// Created by nils on 09/05/23.
//

#pragma once

#include <unistd.h>
#include <cassert>
#include <sstream>

// magic values
const int RESULT_START = 0x4603C330;
const int RESULT_END = 0x71DA7405;

const int DOWNSET_START = 0x12345678;
const int DOWNSET_END = 0x1BCDEF0A;

const int BDD_START = 0x61C50880;
const int BDD_END = 0x3BB82F55;



struct pipe_t {
  int r = -1, w = -1;
  int byte_count = 0;

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

  /*
  void write_bdd (bdd b, spot::twa_graph_ptr aut) {
    write_obj<int> (BDD_START);

    std::stringstream stream;
    stream << spot::bdd_to_formula (b, aut->get_dict ());
    std::string str = stream.str ();

    write_obj<int> (str.size ());
    for(char c: str) {
      write_obj<char> (c);
    }

    printf("wrote str: %s\n", str.c_str()); fflush(stdout);


    write_obj<int> (BDD_END);
  }

  bdd read_bdd (spot::twa_graph_ptr aut) {
    assert_read<int> (BDD_START);

    int size = read_obj<int> ();
    std::string str;
    str.resize(size);
    for(int i = 0; i < size; i++) {
      str[i] = read_obj<char> ();
    }
    assert_read<int> (BDD_END);

    printf("str: %s\n", str.c_str()); fflush(stdout);


    spot::formula formula = spot::parse_formula (str);


    bdd res = spot::formula_to_bdd (formula, aut->get_dict (), nullptr);
    return bddtrue;


    utils::vout << "str: " << str << "\n res: " << spot::bdd_to_formula(res, aut->get_dict()) << "\n";
    return res;
  }
  */
};

