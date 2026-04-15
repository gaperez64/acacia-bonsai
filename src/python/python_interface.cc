
#include <utility>

#include "python_interface.hh"
#include "solver/solver_invoker.hh"
#include "utils/verbose.hh"


utils::voutstream utils::vout;
unsigned int      utils::verbose = 0;

// TODO We need to figure out some clean way to define these, instead of copying them everywhere
size_t posets::vectors::bool_threshold = 0;
size_t posets::vectors::bitset_threshold = 0;


io_spec get_io_spec(const std::vector<std::string>& input_aps, const std::vector<std::string>& output_aps) {
  return io_spec{.input_aps = input_aps, .output_aps = output_aps};
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
  // TODO: this no longer works
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

// TODO: these two functions need to be merged into one function "solve_game". This
//  will return a wrapper around a "game result". This then (a) tells you REAL/UNREAL
//  and (b) returns an iterable.
bool solve_acacia_safety_game (spot::twa_graph_ptr twa, bdd_io_spec& io_spec, int k_max, int k_min, int k_inc) {
  // TODO: this no longer works
  bool res = solve_game (twa, k_max, k_min, k_inc, io_spec.inputs, io_spec.outputs);
  return res;
}

const winreg_iterator* get_winning_region_of_game (spot::twa_graph_ptr twa, bdd_io_spec& io_spec,
                                                   int k_max, int k_min, int k_inc) {
  // TODO: this no longer works
  auto winning_region = get_winning_region (twa, k_max, k_min, k_inc, io_spec.inputs, io_spec.outputs);

  if (winning_region.has_value ()) {
    // return iterator that has access to the winning region
    return new winreg_iterator{winning_region.value ()};
  }

  return nullptr;
}
