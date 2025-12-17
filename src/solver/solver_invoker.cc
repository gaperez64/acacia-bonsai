

#include "aut_preprocessors.hh"
#include "configuration.hh"
#include "create_automaton.hh"
#include "error_msg.hh"
#include "posets/vectors/traits.hh"
#include "solve_game.hh"

#include <spot/tl/formula.hh>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/translate.hh>
#include <string>
#include <utility>
#include <vector>

spot::formula parse_ltl_string (const std::string& input) {
  auto pf = spot::parse_infix_psl (input, spot::default_environment::instance (), false, false);

  if ((not pf.f) or (not pf.errors.empty ())) {
    pf.format_errors (std::cerr);
    error (EXIT_CODE_ERROR, "Error parsing LTL formula");
  }

  return pf.f;
}

int run_ltl (spot::translator& trans, std::vector<std::string> input_aps,
             std::vector<std::string> output_aps, spot::bdd_dict_ptr dict, unsigned opt_k,
             unsigned opt_kmin, unsigned opt_kinc, std::string formula) {
  // manually register inputs/outputs
  bdd all_inputs = bddtrue;
  bdd all_outputs = bddtrue;

  for (std::string ap : input_aps) {
    const unsigned v = dict->register_proposition (spot::formula::ap (ap), nullptr);
    all_inputs &= bdd_ithvar (v);
  }
  for (std::string ap : output_aps) {
    const unsigned v = dict->register_proposition (spot::formula::ap (ap), nullptr);
    all_outputs &= bdd_ithvar (v);
  }

  spot::formula spot_formula = parse_ltl_string (formula);

  auto aut = create_automaton (std::move (spot_formula), trans);
  AUT_PREPROCESSOR::make (aut, all_inputs, all_outputs, opt_k) ();

  const bool res = solve_game (aut, opt_k, opt_kmin, opt_kinc, all_inputs, all_outputs);

  dict->unregister_all_my_variables (nullptr);

  return res;
}
