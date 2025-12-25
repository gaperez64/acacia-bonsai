#pragma once

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

