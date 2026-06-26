#include "solver/solver_invoker.hh"

#include "error_msg.hh"
#include "posets/vectors/traits.hh"
#include "solver/configured_components.hh"
#include "solver/create_automaton.hh"
#include "solver/solve_game.hh"
#include "solver/spot_nba_fastpath.hh"
#include "utils/push_aps.hh"
#include "utils/typeinfo.hh"
#include "utils/verbose.hh"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <ostream>
#include <ranges>
#include <spot/misc/optionmap.hh>
#include <spot/tl/formula.hh>
#include <spot/tl/parse.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/aiger.hh>
#include <spot/twaalgos/synthesis.hh>
#include <spot/twaalgos/translate.hh>
#include <string>
#include <utility>
#include <vector>

namespace {
  spot::formula parse_ltl_string (const std::string& input) {
    auto pf = spot::parse_infix_psl (input, spot::default_environment::instance (), false, false);

    if ((not pf.f) or (not pf.errors.empty ())) {
      pf.format_errors (std::cerr);
      error (EXIT_CODE_ERROR, "Error parsing LTL formula");
    }

    return pf.f;
  }

  using utils::push_aps;

  void print_no_output_aag (std::ostream& os, const std::vector<std::string>& input_aps) {
    os << "aag " << input_aps.size () << ' ' << input_aps.size () << " 0 0 0\n";
    for (size_t i = 0; i < input_aps.size (); ++i)
      os << 2 * (i + 1) << '\n';
    for (size_t i = 0; i < input_aps.size (); ++i)
      os << 'i' << i << ' ' << input_aps[i] << '\n';
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
      const SPOT_FAST_T spot_fast;
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
                   SPOT_FAST_T spot_fast,
                   const std::optional<std::string>& synth_fname)
        : dict {dict},
          input_aps {input_aps},
          output_aps {output_aps},
          opt_k {opt_k},
          opt_kmin {opt_kmin},
          opt_kinc {opt_kinc},
          check_unreal {check_unreal},
          spot_fast {spot_fast},
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
        std::vector<spot::const_twa_graph_ptr> nonempty_strats;
        std::vector<std::vector<std::string>> nonempty_out_part;
        for (size_t i = 0; i < out_part.size (); ++i) {
          if (out_part[i].empty ())
            continue;
          nonempty_strats.push_back (strats[i]);
          nonempty_out_part.push_back (std::move (out_part[i]));
        }

        if (nonempty_out_part.empty ()) {
          std::ofstream synthesis_file (*synth_fname);
          if (synthesis_file)
            print_no_output_aag (synthesis_file, input_aps);
          else
            std::cerr << "Failed to open the file to store controller!\n";
          return;
        }

        // try both ITE and SoP encodings
        spot::aig_ptr mealy_aig = mealy_machines_to_aig (nonempty_strats, "isop",
                                                         // make sure all
                                                         // inputs and outputs
                                                         // are in the AIG
                                                         input_aps, nonempty_out_part);
        std::ofstream synthesis_file (*synth_fname);
        if (synthesis_file)
          spot::print_aiger (synthesis_file, mealy_aig);
        else
          std::cerr << "Failed to open the file to store controller!\n";
#ifndef NDEBUG
        spot::print_hoa (std::cout, mealy_aig->as_automaton (false));
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

        if (not check_unreal.has_value ())
          spot_formula = spot::formula::Not (spot_formula);

        // Create the automaton for the formula we have prepared.
        spot::translator trans (dict, &extra_options);
        auto aut = create_automaton (spot_formula, trans);

        // If unreal but we haven't pushed inputs yet using X on formula.
        if (check_unreal.has_value () and *check_unreal == UNREAL_X_AUTOMATON) {
          verb_do (2, vout << "Pushing the inputs in the automaton\n");
          aut = push_aps (aut, all_inputs, all_outputs);
        }

        // The (negated/X-modified) formula can translate to a 0-state
        // automaton when its language is empty. Downstream code calls
        // aut->get_init_state_number(), which throws on such an automaton.
        // On the realizability path the formula has been negated, so an
        // empty language means the original spec is a tautology -> REAL.
        // On the unrealizability paths we cannot soundly map an empty
        // language to UNREAL (see issue #109 for the proper fast-path
        // pre-check), so we return inconclusive there.
        if (aut->num_states () == 0) {
          verb_do (1, vout << "Automaton from translator is empty (formula unsat); "
                           << (check_unreal.has_value () ? "inconclusive on unreal path"
                                                         : "spec is valid, realizable")
                           << std::endl);
          return not check_unreal.has_value ();
        }

        const bool want_controller_strategy = synth_fname.has_value () and not check_unreal.has_value ();
        // The nondeterministic GFG path is decision-only and currently validated
        // only in the REAL-child orientation. Keep unreal children on the
        // deterministic fast path or the existing Acacia solver.
        const bool allow_gfg_decision = not check_unreal.has_value ();
        auto fast = acacia::spot_fastpath::try_spot_nba_fast_path (
            aut, all_inputs, all_outputs, want_controller_strategy, allow_gfg_decision, spot_fast);
        if (fast.conclusive) {
          if (fast.strategy.has_value ()) {
            assert (want_controller_strategy);
            strats.push_back (*fast.strategy);
          }
          verb_do (1, vout << "Spot NBA fast path returning "
                           << fast.current_output_player_wins << "\n");
          return fast.current_output_player_wins;
        }

        AUT_PREPROCESSOR::make (aut, all_inputs, all_outputs, opt_k) ();

        // surely_losing can flush every reachable state and leave the
        // automaton empty after purging. Map this to inconclusive on
        // both paths rather than crashing downstream.
        if (aut->num_states () == 0) {
          verb_do (1, vout << "Automaton is empty after preprocessing; inconclusive\n");
          return false;
        }

        posets::vectors::bool_threshold = (BOOLEAN_STATES::make (aut, opt_k)) ();
        verb_do (1, vout << "Found " << posets::vectors::bool_threshold << " boolean states.\n");
        verb_do (4, dict->dump (utils::vout));

        assert (not synth_fname.has_value () or not check_unreal.has_value ());
        std::optional<spot::twa_graph_ptr> maybe_strat =
            solve_game (aut, opt_k, opt_kmin, opt_kinc,
                        // we obtain the subset of inputs by projecting out the set of all
                        // outputs from the cube of all atomic propositions
                        bdd_exist (aut->ap_vars (), all_outputs),
                        // same for the outputs
                        bdd_exist (aut->ap_vars (), all_inputs), synth_fname.has_value ());

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
}  // namespace

bool run_ltl (std::vector<std::string> input_aps, std::vector<std::string> output_aps,
              VECTOR_ELT_T opt_k, VECTOR_ELT_T opt_kmin, VECTOR_ELT_T opt_kinc,
              std::string formula, std::optional<UNREAL_X_T> check_unreal,
              SPOT_FAST_T spot_fast,
              const std::optional<std::string>& synth_fname) {
  if (check_unreal.has_value ()) {
    // We only check one thing at a time.
    assert (*check_unreal != UNREAL_X_BOTH);

    // Swap I and O.
    verb_do (2, vout << "Swapping inputs and outputs to check unrealizability\n");
    input_aps.swap (output_aps);
    verb_do (3, vout << "Inputs: " << input_aps << std::endl);
    verb_do (3, vout << "Outputs: " << output_aps << std::endl);
  }

  // We keep a bdd_dict at this level so that we can do synthesis later if
  // needed. We want BDDs for APs to be consistent through subformula/automata.
  spot::bdd_dict_ptr dict = spot::make_bdd_dict ();

  // Create BDDs for the input and output APs, and associate them with the
  // runner that we will use for the transformation and (un)real check.
  run_one_ltl runner (dict, input_aps, output_aps, opt_k, opt_kmin, opt_kinc, check_unreal,
                      spot_fast, synth_fname);

  spot::formula spot_formula = parse_ltl_string (formula);

#if DECOMPOSE_SPEC == 0
  // Just launch a monolithic runner.
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
  // We are up for decomposition, so first we need to split the formula.
  // NOTE: we may have flipped inputs and outputs already, so we need to
  // provide inputs to the split function in that case.
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
      result = std::ranges::all_of (forms.begin (), forms.end (), std::ref (runner));
    else
      result = std::ranges::any_of (forms.begin (), forms.end (), std::ref (runner));
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
