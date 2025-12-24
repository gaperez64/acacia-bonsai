
#include <utility>

#include "python_interface.hh"
#include "solver/solver_invoker.hh"

spot::formula parse_formula (const std::string& input) {
  return parse_ltl_string(input);
}

void prep_unreal_formula (spot::formula& formula, std::vector<std::string>& output_aps) {
  add_x_to_outputs (formula, output_aps);
}

io_spec create_bdds (std::vector<std::string>& input_aps, std::vector<std::string>& output_aps) {
  // Set up the dictionary now: BuDDy's initialization
  spot::bdd_dict_ptr dict = spot::make_bdd_dict ();

  bdd all_inputs = bddtrue;
  bdd all_outputs = bddtrue;
  for (std::string ap : input_aps) {
    // TODO: make signed int, that is what reg_prop returns
    const unsigned v = dict->register_proposition (spot::formula::ap (ap), nullptr);
    all_inputs &= bdd_ithvar (v);
  }
  for (std::string ap : output_aps) {
    const unsigned v = dict->register_proposition (spot::formula::ap (ap), nullptr);
    all_outputs &= bdd_ithvar (v);
  }

  return io_spec{.inputs = all_inputs, .outputs = all_outputs, .dict = dict};
}

spot::twa_graph_ptr create_twa (spot::formula& formula, io_spec& io_spec) {
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

void prep_unreal_automaton (spot::twa_graph_ptr twa, io_spec& io_spec) {
  push_outputs (twa, io_spec.inputs, io_spec.outputs);
}

void preprocess_aut_standard (spot::twa_graph_ptr twa, io_spec& io_spec, int k_max) {
  aut_preprocessors::standard::make (twa, io_spec.inputs,
    io_spec.outputs, k_max) ();
}

void preprocess_aut_surely_losing (spot::twa_graph_ptr twa, io_spec& io_spec, int k_max) {
  aut_preprocessors::surely_losing::make (twa, io_spec.inputs,
      io_spec.outputs, k_max) ();
}

void set_bool_thresh_no_bool_states (spot::twa_graph_ptr twa, int k_max) {
  posets::vectors::bool_threshold = boolean_states::no_boolean_states::make (twa, k_max)();
}

void set_bool_thresh_forward_saturation (spot::twa_graph_ptr twa, int k_max) {
  posets::vectors::bool_threshold = boolean_states::forward_saturation::make (twa, k_max)();
}

bool solve_game (spot::twa_graph_ptr twa, io_spec& io_spec, int k_max, int k_min, int k_inc) {
  bool res = solve_game (twa, k_max, k_min, k_inc, io_spec.inputs, io_spec.outputs);
  return res;
}
