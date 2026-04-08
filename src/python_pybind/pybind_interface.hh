#pragma once

// TODO: this uses the global default configuration.hh, but there might be a conflict somewhere, resulting in duplicate template instatiation
#include "solver/solver_invoker.hh"
#include <bddx.h>
#include <spot/tl/formula.hh>
#include <spot/twa/bdddict.hh>
#include <spot/twa/fwd.hh>

struct io_spec {
  std::vector<std::string> input_aps;
  std::vector<std::string> output_aps;
};

io_spec get_io_spec(const std::vector<std::string>& input_aps, const std::vector<std::string>& output_aps);

// needed for UNREAL_X_FORMULA
void prep_unreal_formula(spot::formula& formula, std::vector<std::string>& output_aps);

struct bdd_io_spec {
    bdd inputs;
    bdd outputs;
    spot::bdd_dict_ptr dict;
};

bdd_io_spec create_bdds(const io_spec& output_aps);


spot::twa_graph_ptr create_twa(spot::formula& formula, bdd_io_spec& io_spec);

// needed for UNREAL_X_AUTOMATON
void prep_unreal_automaton(spot::twa_graph_ptr twa, bdd_io_spec& io_spec);


void preprocess_aut_standard(spot::twa_graph_ptr twa, bdd_io_spec& io_spec, int k_max);


void preprocess_aut_surely_losing(spot::twa_graph_ptr twa, bdd_io_spec& io_spec, int k_max);


void set_bool_thresh_no_bool_states(spot::twa_graph_ptr twa, int k_max);


void set_bool_thresh_forward_saturation(spot::twa_graph_ptr twa, int k_max);


bool solve_acacia_safety_game(spot::twa_graph_ptr twa, bdd_io_spec& io_spec, int k_max, int k_min, int k_inc);

/**
 * The type of vectors in a winning region.
 */
using vector_type = posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>;

/**
 * The type of winning region.
 */
using winreg_type = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<vector_type>;


class vector_wrapper {
  private:
    const vector_type& vec;
  public:
    vector_wrapper(const vector_type& v) : vec{v} {}

    [[nodiscard]]
    std::size_t len() const { return vec.end() - vec.begin(); }

    [[nodiscard]]
    const vector_type& get_vec() const {
      return vec;
    }
};


/**
 * Exposes a single vector in a winning region as an iterator.
 */
class vector_iterator {
  private:
    decltype(static_cast<const vector_type*> (nullptr)->begin()) cur;
    decltype(static_cast<const vector_type*> (nullptr)->end()) end;

  public:
    explicit vector_iterator (const vector_wrapper& v)
        : cur{v.get_vec ().begin()}, end{v.get_vec ().end()} {}

    [[nodiscard]]
    bool has_next() const {
      return cur != end;
    }

    // VECTOR_ELT_T next() {
    //   VECTOR_ELT_T const value = *cur;
    //   ++cur;
    //   return value;
    // }

    VECTOR_ELT_T next() {
      if (cur == end) {
        throw std::out_of_range("iterator exhausted");
      }
      return *cur++;
    }

    [[nodiscard]]
    size_t len() const {
      return end - cur;
    }
};

/**
 * Exposes a winning region as an iterator.
 */
class winreg_iterator {
  private:
    decltype(static_cast<const winreg_type*> (nullptr)->begin()) cur;
    decltype(static_cast<const winreg_type*> (nullptr)->end()) end;

  public:
    explicit winreg_iterator (const winreg_type& s)
        : cur{s.begin()}, end{s.end()} {}

    [[nodiscard]]
    bool has_next() const {
      return cur != end;
    }

    vector_wrapper* next() {
      vector_wrapper* value = new vector_wrapper(*cur);
      ++cur;
      return value;
    }

    [[nodiscard]]
    size_t len() const {
      return end - cur;
    }
};

/**
 * Returns an iterator over the winning region if such a region exists, returns nullptr otherwise.
 * @param twa
 * @param io_spec
 * @param k_max
 * @param k_min
 * @param k_inc
 * @return
 */
const winreg_iterator* get_winning_region_of_game (spot::twa_graph_ptr twa, bdd_io_spec& io_spec,
                                                   int k_max, int k_min, int k_inc);
