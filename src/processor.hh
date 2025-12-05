#pragma once

#include <utility>
#include <vector>
#include <string>

#include <spot/twaalgos/translate.hh>
#include <spot/tl/formula.hh>
#include <spot/tl/parse.hh>


#include "error_msg.hh"
#include "solver_invoker/create_safety_game.hh"
#include "solver_invoker/epilogue.hh"


inline spot::parsed_formula parse_formula(const std::string& s)
{
    return spot::parse_infix_psl
      (s, spot::default_environment::instance(), false, false);
}

inline spot::formula process_ltl_string(const std::string& input)
  {
    auto pf = parse_formula(input);

    if (!pf.f || !pf.errors.empty())
    {
      error(0, "parse error:");
      pf.format_errors(std::cerr);
      exit(1);
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
  spot::formula spot_formula = process_ltl_string(formula);

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

  safety_game game = prepare_formula(spot_formula, trans, all_inputs, all_outputs, opt_K, opt_Kmin);
  int res = epilogue(game, opt_K, opt_Kmin, opt_Kinc, all_inputs, all_outputs, std::move(init_state), bddtrue);

  dict->unregister_all_my_variables (0);

  return res;
}
