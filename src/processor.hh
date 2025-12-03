#pragma once

#include <utility>
#include <vector>
#include <string>

#include <spot/twaalgos/translate.hh>

#include "composition/composition_mt.hh"
#include "error_msg.hh"


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

  composition_mt composer (opt_K, opt_Kmin, opt_Kinc, dict, trans, all_inputs, all_outputs, input_aps,
                          output_aps, std::move(init_state), std::move(spot_formula));

  const int retval = composer.run_one ();

  dict->unregister_all_my_variables (0);

  return retval;
}
