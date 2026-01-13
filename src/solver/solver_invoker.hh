#pragma once

#include "aut_preprocessors.hh"
#include "configuration.hh"
#include "create_automaton.hh"
#include "error_msg.hh"
#include "posets/vectors/traits.hh"
#include "solve_game.hh"
#include "utils/cache.hh"

#include <optional>
#include <spot/misc/optionmap.hh>
#include <spot/misc/timer.hh>
#include <spot/misc/tmpfile.hh>
#include <spot/tl/formula.hh>
#include <spot/tl/parse.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/aiger.hh>
#include <spot/twaalgos/translate.hh>
#include <spot/twaalgos/synthesis.hh>
#include <string>
#include <utility>
#include <vector>

// These are the valid ways of treating unrealizability
enum UNREAL_X_T : char { UNREAL_X_FORMULA = 'f', UNREAL_X_AUTOMATON = 'a', UNREAL_X_BOTH };

spot::formula parse_ltl_string (const std::string& input) {
  auto pf = spot::parse_infix_psl (input, spot::default_environment::instance (), false, false);

  if ((not pf.f) or (not pf.errors.empty ())) {
    pf.format_errors (std::cerr);
    error (EXIT_CODE_ERROR, "Error parsing LTL formula");
  }

  return pf.f;
}

/** Changes q -> <i', o'> -> q' with saved o to
 * q -> <i', o> -> {q' saved o'}.
 * To be more precise, o stands for the atomic propositions whose conjunction
 * is to_be_pushed and i stands for those whose conjunction (a cube) is
 * all_others.
 */
spot::twa_graph_ptr push_aps (const spot::twa_graph_ptr aut, bdd to_be_pushed, bdd all_others) {
  auto ret = spot::make_twa_graph (aut->get_dict ());
  ret->copy_acceptance_of (aut);
  ret->copy_ap_of (aut);
  ret->prop_copy (aut, spot::twa::prop_set::all ());
  ret->prop_universal (spot::trival::maybe ());

  static auto cache = utils::make_cache<unsigned> (0u, 0u);
  const auto build_aut = [&] (unsigned state, bdd saved_o, const auto& recurse) {
    auto cached = cache.get (state, saved_o.id ());
    if (cached)
      return *cached;
    auto ret_state = ret->new_state ();
    cache (ret_state, state, saved_o.id ());
    for (auto& e : aut->out (state)) {
      auto cond = e.cond;
      // e.cond = i1 & o1 || !i1 & !o1

      while (cond != bddfalse) {
        // Pick one satisfying assignment where outputs all have values
        bdd one_sat = bdd_satoneset (cond, to_be_pushed, bddtrue);
        // Get the corresponding input bdd
        bdd one_input_bdd = bdd_exist (cond & bdd_exist (one_sat, all_others), to_be_pushed);
        ret->new_edge (ret_state,
                       recurse (e.dst, bdd_exist (cond & one_input_bdd, all_others), recurse),
                       saved_o & one_input_bdd, e.acc);
        cond -= one_input_bdd;
      }
    }
    return ret_state;
  };
  build_aut (aut->get_init_state_number (), bddtrue, build_aut);
  return ret;
}

/**
 * This is the function that calls our main algorithm on the given LTL
 * formula. Depending on whether we want to check realizability or
 * unrealizability (by changing between Mealy and Moore semantics via the
 * formula or the automaton), we apply different transformations on the
 * formula and resulting automaton. In addition, we make sure that the BDD
 * variables for the inputs are ordered first if checking realizability, and
 * after the variables for the outputs otherwise.
 */
bool run_ltl (std::vector<std::string> input_aps, std::vector<std::string> output_aps,
              VECTOR_ELT_T opt_k, VECTOR_ELT_T opt_kmin, VECTOR_ELT_T opt_kinc,
              std::string formula, std::optional<UNREAL_X_T> check_unreal) {
  spot::formula spot_formula = parse_ltl_string (formula);

  auto [forms, outs] = spot::split_independent_formulas(spot_formula, output_aps);
  verb_do (2, vout << "Decomposed the input into " << forms.size () << " subformulas\n");
#ifndef NDEBUG
  for (size_t i = 0; i < forms.size (); ++i) {
    verb_do (2, vout << "Subformula " << i + 1 << ": " << forms[i] << std::endl);
    verb_do (2, vout << "with output set:");
    for (auto& sf: outs[i])
      verb_do (2, vout << sf << " ");
    verb_do (2, vout << "\n");
  }
#endif

  if (check_unreal.has_value ()) {
    assert (*check_unreal != UNREAL_X_BOTH);
    // Swap I and O.
    verb_do (2, vout << "Swapping inputs and outputs\n");
    input_aps.swap (output_aps);
    verb_do (3, vout << "Inputs: " << input_aps << std::endl);
    verb_do (3, vout << "Outputs: " << output_aps << std::endl);

    // Swapping them in the formula too
    if (*check_unreal == UNREAL_X_FORMULA) {
      verb_do (2, vout << "Mealy-to-Moore: adding X to the inputs in the formula\n");
      auto rec = [input_aps] (auto&& self, spot::formula m) {
        if (m.is (spot::op::ap) and
            (std::ranges::find (input_aps, m.ap_name ()) != input_aps.end ()))
          return spot::formula::X (m);
        return m.map ([&] (spot::formula t) { return self (self, t); });
      };
      spot_formula = spot_formula.map ([&] (spot::formula t) { return rec (rec, t); });
    }
  }
  else  // all that is needed for real is to negate the formula
    spot_formula = spot::formula::Not (spot_formula);

  // These options play a role in twaalgos.
  spot::option_map extra_options;
  extra_options.set ("simul", 0);
  extra_options.set ("ba-simul", 0);
  extra_options.set ("det-simul", 0);
  extra_options.set ("tls-impl", 1);
  extra_options.set ("wdba-minimize", 2);
#ifndef NDEBUG
  extra_options.report_unused_options ();
#endif

  // Setup the dictionary now: BuDDy's initialization
  spot::bdd_dict_ptr dict = spot::make_bdd_dict ();
  spot::translator trans (dict, &extra_options);

  // At this point we have the formula we are going to compile and the BDD
  // dictionary is set exactly as we need it. The only thing
  // missing is to:
  // 1. construct the automaton for the formula (and get some BDDs)
  // 2. push the in-/out-puts if needed due to a Mealy-Moore change, and
  // 3. solve the game

  // Create the automaton for the formula we have prepared
  auto aut = create_automaton (std::move (spot_formula), trans);

  // Create BDDs for the input and output APs
  bdd all_inputs = bddtrue;
  bdd all_outputs = bddtrue;
  for (std::string ap : input_aps) {
    const unsigned v = aut->register_ap (spot::formula::ap (ap));
    all_inputs &= bdd_ithvar (v);
  }
  for (std::string ap : output_aps) {
    const unsigned v = aut->register_ap (spot::formula::ap (ap));
    all_outputs &= bdd_ithvar (v);
  }


  // If unreal but we haven't pushed inputs yet using X on formula
  if (check_unreal.has_value () and *check_unreal == UNREAL_X_AUTOMATON) {
    verb_do (2, vout << "Pushing the inputs in the automaton\n");
    aut = push_aps (aut, all_inputs, all_outputs);
  }

  AUT_PREPROCESSOR::make (aut, all_inputs, all_outputs, opt_k) ();

  posets::vectors::bool_threshold = (BOOLEAN_STATES::make (aut, opt_k)) ();
  verb_do (1, vout << "Found " << posets::vectors::bool_threshold << " boolean states.\n");

  return solve_game (aut, opt_k, opt_kmin, opt_kinc, all_inputs, all_outputs);
}
