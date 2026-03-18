#pragma once

#include "aut_preprocessors.hh"
#include "configuration.hh"
#include "create_automaton.hh"
#include "error_msg.hh"
#include "posets/vectors/traits.hh"
#include "solve_game.hh"
#include "utils/cache.hh"

#include <optional>
#include <ranges>
#include <spot/misc/optionmap.hh>
#include <spot/misc/timer.hh>
#include <spot/misc/tmpfile.hh>
#include <spot/tl/formula.hh>
#include <spot/tl/parse.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/aiger.hh>
#include <spot/twaalgos/synthesis.hh>
#include <spot/twaalgos/translate.hh>
#include <string>
#include <utility>
#include <vector>

// These are the valid ways of treating unrealizability
enum UNREAL_X_T : char { UNREAL_X_FORMULA = 'f', UNREAL_X_AUTOMATON = 'a', UNREAL_X_BOTH };

namespace {
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
   * This is the functor that calls our main algorithm on the given LTL
   * formula. Depending on whether we want to check realizability or
   * unrealizability (by changing between Mealy and Moore semantics via the
   * formula or the automaton), we apply different transformations on the
   * formula and resulting automaton. In addition, we make sure that the BDD
   * variables for the inputs are ordered first if checking realizability, and
   * after the variables for the outputs otherwise.
   */
  class run_one_ltl {
    private:
      const spot::bdd_dict_ptr dict;
      const std::vector<std::string> input_aps;
      const std::vector<std::string> output_aps;
      bdd all_inputs;
      bdd all_outputs;
      const VECTOR_ELT_T opt_k;
      const VECTOR_ELT_T opt_kmin;
      const VECTOR_ELT_T opt_kinc;
      const std::optional<UNREAL_X_T> check_unreal;
      spot::option_map extra_options;
      const std::optional<std::string> synth_fname;
      std::vector<spot::const_twa_graph_ptr> strats;

    public:
      /**
       * When constructing the runner, setup the dictionary of BDDs now.
       * It's important this happens now before we create any automaton
       * so that we can ensure that the inputs are ordered before the outputs
       * (recall we already swapped them if needed!)
       */
      run_one_ltl (spot::bdd_dict_ptr dict, const std::vector<std::string>& input_aps,
                   const std::vector<std::string>& output_aps, VECTOR_ELT_T opt_k,
                   VECTOR_ELT_T opt_kmin, VECTOR_ELT_T opt_kinc,
                   std::optional<UNREAL_X_T> check_unreal,
                   const std::optional<std::string>& synth_fname)
        : dict {dict},
          input_aps {input_aps},
          output_aps {output_aps},
          opt_k {opt_k},
          opt_kmin {opt_kmin},
          opt_kinc {opt_kinc},
          check_unreal {check_unreal},
          synth_fname {synth_fname} {
        // These options play a role in twaalgos.
        extra_options.set ("simul", 0);
        extra_options.set ("ba-simul", 0);
        extra_options.set ("det-simul", 0);
        extra_options.set ("tls-impl", 1);
        extra_options.set ("wdba-minimize", 2);
#ifndef NDEBUG
        extra_options.report_unused_options ();
#endif

        // Create BDD "cubes" that represent the sets of inputs and outputs,
        // respectively. We associate them with this object when registering
        // them.
        all_inputs = bddtrue;
        all_outputs = bddtrue;
        for (std::string ap : input_aps) {
          const unsigned v = dict->register_proposition (spot::formula::ap (ap), this);
          all_inputs &= bdd_ithvar (v);
        }
        for (std::string ap : output_aps) {
          const unsigned v = dict->register_proposition (spot::formula::ap (ap), this);
          all_outputs &= bdd_ithvar (v);
        }
        verb_do (4, dict->dump (utils::vout));
      }

      ~run_one_ltl () {
        dict->unregister_all_my_variables (this);
        verb_do (4, dict->dump (utils::vout));
      }

      void synthesis (spot::formula spot_formula, std::vector<std::vector<std::string>> out_part) {
        assert (synth_fname.has_value ());
        assert (strats.size () > 0);
        assert (strats.size () == out_part.size ());
        // try both ITE and SoP encodings; use AP sets from the strats themselves
        spot::aig_ptr mealy_aig = mealy_machines_to_aig (strats, "both");
        std::ofstream synthesis_file (*synth_fname);
        if (synthesis_file)
          spot::print_aiger (synthesis_file, mealy_aig);
        else
          std::cerr << "Failed to open the file to store controller!\n";
#ifndef NDEBUG
        spot_formula = spot::formula::Not (spot_formula);
        verb_do (2, vout << "Model checking result by checking intersection with "
                         << spot_formula << std::endl);
        spot::translator trans (dict, &extra_options);
        auto aut = create_automaton (spot_formula, trans);
        assert (not aut->intersects (mealy_aig->as_automaton (false)));
#endif
      }

      bool operator() (spot::formula spot_formula) {
        if (check_unreal.has_value () and *check_unreal == UNREAL_X_FORMULA) {
          verb_do (2, vout << "Mealy-to-Moore: adding X to the inputs in the formula\n");
          auto rec = [this] (auto&& self, spot::formula m) {
            if (m.is (spot::op::ap) and
                (std::ranges::find (input_aps, m.ap_name ()) != input_aps.end ()))
              return spot::formula::X (m);
            return m.map ([&] (spot::formula t) { return self (self, t); });
          };
          spot_formula = spot_formula.map ([&] (spot::formula t) { return rec (rec, t); });
        }

        if (not check_unreal.has_value ())  // all that is needed for real is to negate the formula
          spot_formula = spot::formula::Not (spot_formula);

        // At this point we have the formula we are going to compile and the BDD
        // dictionary is set exactly as we need it. The only thing
        // missing is to:
        // 1. construct the automaton for the formula
        // 2. solve the game

        // Create the automaton for the formula we have prepared
        spot::translator trans (dict, &extra_options);
        auto aut = create_automaton (spot_formula, trans);

        // If unreal but we haven't pushed inputs yet using X on formula
        if (check_unreal.has_value () and *check_unreal == UNREAL_X_AUTOMATON) {
          verb_do (2, vout << "Pushing the inputs in the automaton\n");
          aut = push_aps (aut, all_inputs, all_outputs);
        }

        AUT_PREPROCESSOR::make (aut, all_inputs, all_outputs, opt_k) ();

        posets::vectors::bool_threshold = (BOOLEAN_STATES::make (aut, opt_k)) ();
        verb_do (1, vout << "Found " << posets::vectors::bool_threshold << " boolean states.\n");
        verb_do (4, dict->dump (utils::vout));

        assert (not synth_fname.has_value () or not check_unreal.has_value ());
        std::optional<spot::twa_graph_ptr> maybe_strat =
            solve_game (aut, opt_k, opt_kmin, opt_kinc, all_inputs, all_outputs,
                        synth_fname.has_value ());

        if (maybe_strat.has_value ()) {
          if (synth_fname.has_value ())
            strats.push_back (*maybe_strat);
          return true;
        }
        else {
          return false;
        }
      }
  };
}

bool run_ltl (std::vector<std::string> input_aps, std::vector<std::string> output_aps,
              VECTOR_ELT_T opt_k, VECTOR_ELT_T opt_kmin, VECTOR_ELT_T opt_kinc,
              std::string formula, std::optional<UNREAL_X_T> check_unreal,
              const std::optional<std::string>& synth_fname) {
  if (check_unreal.has_value ()) {
    // We only check one thing at a time
    assert (*check_unreal != UNREAL_X_BOTH);

    // Swap I and O.
    verb_do (2, vout << "Swapping inputs and outputs to check unrealizability\n");
    input_aps.swap (output_aps);
    verb_do (3, vout << "Inputs: " << input_aps << std::endl);
    verb_do (3, vout << "Outputs: " << output_aps << std::endl);
  }

  // We keep a bdd_dict at this level so that we can do synthesis later if
  // needed, we want BDDs for APs to be consistent through subformula/automata
  spot::bdd_dict_ptr dict = spot::make_bdd_dict ();

  // Create BDDs for the input and output APs, and associate them with the
  // runner that we will use for the transformation and (un)real check
  run_one_ltl runner (dict, input_aps, output_aps, opt_k, opt_kmin, opt_kinc, check_unreal,
                      synth_fname);

  spot::formula spot_formula = parse_ltl_string (formula);

#if DECOMPOSE_SPEC == 0
  // just launch a monolothic runner
  if (runner (spot_formula)) {
    if (synth_fname.has_value ()) {
      std::vector<std::vector<std::string>> out_part = {output_aps};
      runner.synthesis (spot_formula, out_part);
    }
    return true;
  }
  else {
    return false;
  }

#elif DECOMPOSE_SPEC == 1
  // we are up for decomposition, so first we need to split the formula
  // NOTE: we may have flipped inputs and outputs already, so we need to
  // provide inputs to the split function in that case
  std::vector<std::vector<std::string>> out_part;
  auto [forms, outs] = spot::split_independent_formulas (
      spot_formula, check_unreal.has_value () ? input_aps : output_aps);
  verb_do (2, vout << "Decomposed the input into " << forms.size () << " subformulas\n");

  for (size_t i = 0; i < forms.size (); ++i) {
    verb_do (2, vout << "Subformula " << i + 1 << ": " << forms[i] << std::endl);
    verb_do (2, vout << "with output set: ");
    std::vector<std::string> temp;
    for (auto& sf : outs[i]) {
      verb_do (2, vout << sf << " ");
      temp.push_back (sf.ap_name ());
    }
    out_part.emplace_back (std::move (temp));
    verb_do (2, vout << "\n");
  }

  bool result;
  if (forms.size () <= 1) {
    result = runner (spot_formula);
  }
  else {
    // Here's the real decomposition in terms of solving. If we found more than
    // one formula, we're going to solve those instead.
    // * If we're checking realizability, all of the subgames must be
    //   realizable;
    // * conversely, for unrealizability, I just need one of them to be declared
    //   unrealizable to get a conclusive answer.
    if (not check_unreal.has_value ())
      result = std::ranges::all_of (forms.begin (), forms.end (), runner);
    else
      result = std::ranges::any_of (forms.begin (), forms.end (), runner);
    verb_do (3, vout << "Result of sub-calls to runner " << result << std::endl);
  }

  if (result) {
    if (synth_fname.has_value ())
      runner.synthesis (spot_formula, out_part);
    return true;
  }
  else {
    return false;
  }

#else
  std::unreachable ();
#endif
}
