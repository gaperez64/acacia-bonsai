
#include <utility>

#include "python_interface.hh"
#include "solver/solver_invoker.hh"

// TODO: this is to get the verbose debug printing working, needs to refactored out.
utils::voutstream utils::vout;
int               utils::verbose = 0;

// TODO We need to figure out some clean way to define these, instead of copying them everywhere
size_t posets::vectors::bool_threshold = 0;
size_t posets::vectors::bitset_threshold = 0;


io_spec get_io_spec(const std::vector<std::string>& input_aps, const std::vector<std::string>& output_aps) {
  return io_spec{.input_aps = input_aps, .output_aps = output_aps};
}

void prep_unreal_formula (spot::formula& formula, std::vector<std::string>& output_aps) {
  add_x_to_outputs (formula, output_aps);
}

bdd_io_spec create_bdds (const io_spec& io_spec) {
  // Set up the dictionary now: BuDDy's initialization
  spot::bdd_dict_ptr dict = spot::make_bdd_dict ();

  bdd all_inputs = bddtrue;
  bdd all_outputs = bddtrue;
  for (std::string ap : io_spec.input_aps) {
    // TODO: make signed int, that is what reg_prop returns
    const unsigned v = dict->register_proposition (spot::formula::ap (ap), nullptr);
    all_inputs &= bdd_ithvar (v);
  }
  for (std::string ap : io_spec.output_aps) {
    const unsigned v = dict->register_proposition (spot::formula::ap (ap), nullptr);
    all_outputs &= bdd_ithvar (v);
  }

  return bdd_io_spec{.inputs = all_inputs, .outputs = all_outputs, .dict = dict};
}

spot::twa_graph_ptr create_twa (spot::formula& formula, bdd_io_spec& io_spec) {
  spot::option_map extra_options;
  extra_options.set ("simul", 0);
  extra_options.set ("ba-simul", 0);
  extra_options.set ("det-simul", 0);
  extra_options.set ("tls-impl", 1);
  extra_options.set ("wdba-minimize", 2);

  spot::translator trans (io_spec.dict, &extra_options);

  auto aut = create_automaton (formula, trans);

  return aut;
}

void prep_unreal_automaton (spot::twa_graph_ptr twa, bdd_io_spec& io_spec) {
  push_outputs (twa, io_spec.inputs, io_spec.outputs);
}

void preprocess_aut_standard (spot::twa_graph_ptr twa, bdd_io_spec& io_spec, int k_max) {
  aut_preprocessors::standard::make (twa, io_spec.inputs,
    io_spec.outputs, k_max) ();
}

void preprocess_aut_surely_losing (spot::twa_graph_ptr twa, bdd_io_spec& io_spec, int k_max) {
  aut_preprocessors::surely_losing::make (twa, io_spec.inputs,
      io_spec.outputs, k_max) ();
}

void set_bool_thresh_no_bool_states (spot::twa_graph_ptr twa, int k_max) {
  posets::vectors::bool_threshold = boolean_states::no_boolean_states::make (twa, k_max)();
}

void set_bool_thresh_forward_saturation (spot::twa_graph_ptr twa, int k_max) {
  posets::vectors::bool_threshold = boolean_states::forward_saturation::make (twa, k_max)();
}

bool solve_acacia_safety_game (spot::twa_graph_ptr twa, bdd_io_spec& io_spec, int k_max, int k_min, int k_inc) {
  bool res = solve_game (twa, k_max, k_min, k_inc, io_spec.inputs, io_spec.outputs);
  return res;
}

posets::downsets::vector_backed<posets::vectors::simd_vector_backed<char>> get_winning_region_of_game(spot::twa_graph_ptr twa, bdd_io_spec& io_spec, int k_max, int k_min, int k_inc) {
  // TODO: how to handle optional in Python? Can we return None?
  //  Maybe we can already return here the begin() and end() pair.
  //  Perhaps have a custom iteration class that has begin() and end() that points to vector<char>?

  posets::downsets::vector_backed<posets::vectors::simd_vector_backed<char>> winning_region
    = get_winning_region (twa, k_max, k_min, k_inc, io_spec.inputs, io_spec.outputs).value ();

  return winning_region;
}


