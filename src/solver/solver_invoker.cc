#include "solver_invoker.hh"

#include "aut_preprocessors.hh"
#include "configuration.hh"
#include "create_automaton.hh"
#include "error_msg.hh"
#include "posets/vectors/traits.hh"
#include "solve_game.hh"
#include "utils/cache.hh"

#include <optional>
#include <spot/tl/formula.hh>
#include <spot/tl/parse.hh>
#include <spot/twa/twagraph.hh>
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

namespace {
  // Changes q -> <i', o'> -> q' with saved o to
  // q -> <i', o> -> {q' saved o}
  spot::twa_graph_ptr
  push_outputs (const spot::twa_graph_ptr aut, bdd all_inputs, bdd all_outputs) {
    auto ret = spot::make_twa_graph (aut->get_dict ());
    ret->copy_acceptance_of (aut);
    ret->copy_ap_of (aut);
    ret->prop_copy (aut, spot::twa::prop_set::all());
    ret->prop_universal (spot::trival::maybe ());

    static auto cache = utils::make_cache<unsigned> (0u, 0u);
    const auto build_aut = [&] (unsigned state, bdd saved_o,
                                const auto& recurse) {
      auto cached = cache.get (state, saved_o.id ());
      if (cached) return *cached;
      auto ret_state = ret->new_state ();
      cache (ret_state, state, saved_o.id ());
      for (auto& e : aut->out (state)) {
        auto cond = e.cond;
        // e.cond = i1 & o1 || !i1 & !o1

        while (cond != bddfalse) {
          // Pick one satisfying assignment where outputs all have values
          bdd one_sat = bdd_satoneset (cond, all_outputs, bddtrue);
          // Get the corresponding input bdd
          bdd one_input_bdd =
            bdd_exist (cond & bdd_exist (one_sat, all_inputs),
                       all_outputs);
          ret->new_edge (ret_state,
                         recurse (e.dst,
                                  bdd_exist (cond & one_input_bdd,
                                            all_inputs),
                                  recurse),
                         saved_o & one_input_bdd,
                         e.acc);
          cond -= one_input_bdd;
        }
      }
      return ret_state;
    };
    build_aut (aut->get_init_state_number (), bddtrue, build_aut);
    return ret;
  }
}

bool run_ltl (spot::translator& trans,
              std::vector<std::string> input_aps,
              std::vector<std::string> output_aps,
              spot::bdd_dict_ptr dict, unsigned opt_k,
              unsigned opt_kmin, unsigned opt_kinc,
              std::string formula,
              std::optional<unreal_x_t> check_unreal) {
  spot::formula spot_formula = parse_ltl_string (formula);

  if (not check_unreal.has_value ())
    spot_formula = spot::formula::Not (spot_formula);
  else if (*check_unreal == UNREAL_X_FORMULA) {
    // Add X at the outputs
    auto rec = [output_aps] (auto&& self, spot::formula m) {
      if (m.is (spot::op::ap) and
          (std::ranges::find (output_aps,
                              m.ap_name ()) != output_aps.end ()))
        return spot::formula::X (m);
      return m.map ([&] (spot::formula t) { return self (self, t); });
    };
    spot_formula = spot_formula.map ([&] (spot::formula t) { return rec (rec, t); });
    // Swap I and O.
    input_aps.swap (output_aps);
  }

  // Create BDDs for the input and output APs
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

  // Create the automaton for the formula we have prepared
  auto aut = create_automaton (std::move (spot_formula), trans);
  
  // If unreal but we haven't pushed outputs yet using X on formula
  if (check_unreal.has_value () and *check_unreal == UNREAL_X_AUTOMATON) {
    aut = push_outputs (aut, all_inputs, all_outputs);
    input_aps.swap (output_aps);
    std::swap (all_inputs, all_outputs);
  }


  AUT_PREPROCESSOR::make (aut, all_inputs, all_outputs, opt_k) ();

  bool res = solve_game (aut, opt_k, opt_kmin, opt_kinc, all_inputs, all_outputs);

  dict->unregister_all_my_variables (nullptr);

  return res;
}
