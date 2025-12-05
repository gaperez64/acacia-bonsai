#pragma once

#include <utility>
#include <vector>
#include <string>

#include <spot/twaalgos/translate.hh>
#include <spot/tl/formula.hh>
#include <spot/tl/parse.hh>


#include "error_msg.hh"
#include "solver_invoker/create_safety_game.hh"
#include "solver_invoker/solve_game.hh"


inline spot::formula parse_ltl_string(const std::string& input)
  {
  auto pf = spot::parse_infix_psl
    (input, spot::default_environment::instance(), false, false);

    if (!pf.f || !pf.errors.empty())
    {
      pf.format_errors(std::cerr);
      error(EXIT_CODE_ERROR, "Error parsing LTL formula");
    }

    return pf.f;
  }


inline int run_ltl(spot::translator &trans,
                   std::vector<std::string> input_aps,
                   std::vector<std::string> output_aps,
                   spot::bdd_dict_ptr dict,
                   unsigned opt_K,
                   unsigned opt_Kmin,
                   unsigned opt_Kinc,
                   std::vector<int> init_state,
                   std::string formula) {

  // manually register inputs/outputs
  bdd all_inputs = bddtrue;
  bdd all_outputs = bddtrue;

  for(std::string ap: input_aps) {
    unsigned const v = dict->register_proposition (spot::formula::ap (ap), 0);
    all_inputs &= bdd_ithvar (v);
  }
  for(std::string ap: output_aps) {
    unsigned const v = dict->register_proposition (spot::formula::ap (ap), 0);
    all_outputs &= bdd_ithvar (v);
  }


  spot::formula spot_formula = parse_ltl_string(formula);

  auto aut = create_automaton(std::move(spot_formula), trans);
  aut_preprocessors::standard::make (aut, all_inputs, all_outputs, opt_K) ();
  // aut_preprocessors::surely_losing::make (aut, all_inputs, all_outputs, K) ();

  posets::vectors::bool_threshold = (boolean_states::forward_saturation::make (aut, opt_K)) ();
  // posets::vectors::bool_threshold = (boolean_states::no_boolean_states::make (aut, opt_K)) ();
  safety_game game = create_game(aut, posets::vectors::bool_threshold, opt_Kmin);

  int res = solve_game (game, opt_K, opt_Kmin, opt_Kinc, all_inputs, all_outputs, std::move(init_state), bddtrue);

  dict->unregister_all_my_variables (0);

  return res;
}
