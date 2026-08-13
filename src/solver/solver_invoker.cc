#include "solver/solver_invoker.hh"

#include "error_msg.hh"
#include "posets/vectors/traits.hh"
#include "solver/configured_components.hh"
#include "solver/create_automaton.hh"
#include "solver/degenerate_io.hh"
#include "solver/diagnostics.hh"
#include "solver/solve_game.hh"
#include "solver/spot_nba_fastpath.hh"
#include "solver/symmetric_blocks.hh"
#include "solver/symmetry.hh"
#include "solver/syntactic_bypass.hh"
#include "solver/translator_options.hh"
#include "utils/push_aps.hh"
#include "utils/typeinfo.hh"
#include "utils/verbose.hh"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <ranges>
#include <sstream>
#include <spot/misc/optionmap.hh>
#include <spot/tl/apcollect.hh>
#include <spot/tl/formula.hh>
#include <spot/tl/parse.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/aiger.hh>
#include <spot/twaalgos/hoa.hh>
#include <spot/twaalgos/synthesis.hh>
#include <spot/twaalgos/translate.hh>
#include <spot/twaalgos/word.hh>
#include <string>
#include <utility>
#include <vector>

namespace {
#define ACACIA_DIAG_STRINGIFY_INNER(x) #x
#define ACACIA_DIAG_STRINGIFY(x) ACACIA_DIAG_STRINGIFY_INNER (x)

  spot::formula parse_ltl_string (const std::string& input) {
    auto pf = spot::parse_infix_psl (input, spot::default_environment::instance (), false, false);

    if ((not pf.f) or (not pf.errors.empty ())) {
      pf.format_errors (std::cerr);
      error (EXIT_CODE_ERROR, "Error parsing LTL formula");
    }

    return pf.f;
  }

  using utils::push_aps;

#if ACACIA_ENABLE_DIAGNOSTICS
  size_t edge_count (const spot::const_twa_graph_ptr& aut) {
    size_t count = 0;
    if (aut == nullptr)
      return count;
    for (unsigned s = 0; s < aut->num_states (); ++s)
      for ([[maybe_unused]] const auto& e : aut->out (s))
        ++count;
    return count;
  }

  void observe_translated_automaton (const spot::const_twa_graph_ptr& aut) {
    if (auto* diag = acacia::diagnostics::current ()) {
      diag->aut_states = std::max<size_t> (diag->aut_states, aut->num_states ());
      diag->aut_edges = std::max<size_t> (diag->aut_edges, edge_count (aut));
    }
  }
#else
  [[maybe_unused]] void observe_translated_automaton (const spot::const_twa_graph_ptr&) {}
#endif

  std::string child_path (std::optional<UNREAL_X_T> check_unreal) {
    if (not check_unreal.has_value ())
      return "real";
    if (*check_unreal == UNREAL_X_FORMULA)
      return "unreal-formula";
    if (*check_unreal == UNREAL_X_AUTOMATON)
      return "unreal-automaton";
    return "unknown";
  }

  void print_no_output_aag (std::ostream& os, const std::vector<std::string>& input_aps) {
    os << "aag " << input_aps.size () << ' ' << input_aps.size () << " 0 0 0\n";
    for (size_t i = 0; i < input_aps.size (); ++i)
      os << 2 * (i + 1) << '\n';
    for (size_t i = 0; i < input_aps.size (); ++i)
      os << 'i' << i << ' ' << input_aps[i] << '\n';
  }

  bdd cube_for_state (const spot::aig_ptr& circuit, size_t state, unsigned n_latches) {
    bdd cube = bddtrue;
    for (unsigned bit = 0; bit < n_latches; ++bit) {
      bdd lit = circuit->latch_bdd (bit);
      cube &= ((state >> bit) & 1U) ? lit : !lit;
    }
    return cube;
  }

  unsigned aig_lit_for_bdd (const spot::aig_ptr& circuit, const bdd& func) {
    if (func == bddtrue)
      return spot::aig::aig_true ();
    if (func == bddfalse)
      return spot::aig::aig_false ();
    return circuit->encode_bdd (func);
  }

  void write_no_input_strategy (const std::vector<std::string>& output_aps,
                                const std::vector<std::vector<bool>>& values,
                                size_t loop_start,
                                const std::string& synth_fname) {
    size_t capacity = 1;
    unsigned n_latches = 0;
    while (capacity < values.size ()) {
      capacity <<= 1;
      ++n_latches;
    }

    auto circuit = std::make_shared<spot::aig> (std::vector<std::string> {}, output_aps,
                                                n_latches);
    std::vector<bdd> state_cubes;
    state_cubes.reserve (values.size ());
    for (size_t state = 0; state < values.size (); ++state)
      state_cubes.push_back (cube_for_state (circuit, state, n_latches));

    for (size_t out = 0; out < output_aps.size (); ++out) {
      bdd func = bddfalse;
      for (size_t state = 0; state < values.size (); ++state) {
        if (values[state][out])
          func |= state_cubes[state];
      }
      circuit->set_output (out, aig_lit_for_bdd (circuit, func));
    }

    for (unsigned bit = 0; bit < n_latches; ++bit) {
      bdd func = bddfalse;
      for (size_t state = 0; state < values.size (); ++state) {
        size_t next = state + 1;
        if (next == values.size ())
          next = loop_start;
        if ((next >> bit) & 1U)
          func |= state_cubes[state];
      }
      circuit->set_next_latch (bit, aig_lit_for_bdd (circuit, func));
    }

    std::ofstream synthesis_file (synth_fname);
    if (synthesis_file)
      spot::print_aiger (synthesis_file, circuit);
    else
      std::cerr << "Failed to open the file to store controller!\n";
  }

  bool run_no_input_ltl (const std::vector<std::string>& output_aps,
                         spot::formula spot_formula,
                         std::optional<UNREAL_X_T> check_unreal,
                         TRANSLATION_PREF_T translation_pref,
                         const std::optional<std::string>& synth_fname) {
    spot::bdd_dict_ptr dict = spot::make_bdd_dict ();
    int owner = 0;
    struct owner_cleanup {
      spot::bdd_dict_ptr dict;
      int* owner;
      ~owner_cleanup () { dict->unregister_all_my_variables (owner); }
    } cleanup {dict, &owner};

    bdd all_outputs = bddtrue;
    std::vector<int> output_vars;
    output_vars.reserve (output_aps.size ());
    for (const std::string& ap : output_aps) {
      int v = dict->register_proposition (spot::formula::ap (ap), &owner);
      all_outputs &= bdd_ithvar (v);
      output_vars.push_back (v);
    }

    spot::option_map extra_options = acacia::translation::make_options ();

    spot::translator trans (dict, &extra_options);
    acacia::translation::validate_options (extra_options);
    spot::twa_graph_ptr aut;
    {
#if ACACIA_ENABLE_DIAGNOSTICS
      auto* diag = acacia::diagnostics::current ();
      acacia::diagnostics::scoped_timer timer (diag ? &diag->translation_ms : nullptr);
#endif
      aut = create_automaton (spot_formula, trans, translation_pref);
    }
    observe_translated_automaton (aut);
    acacia::diagnostics::snapshot ("no-input-after-translation");

    spot::twa_word_ptr word = nullptr;
    if (aut->num_states () != 0)
      word = aut->accepting_word ();
    const bool satisfiable = static_cast<bool> (word);
    if (check_unreal.has_value ())
      return not satisfiable;
    if (not satisfiable)
      return false;

    if (synth_fname.has_value ()) {
      word->simplify ();
      word->use_all_aps (all_outputs);

      std::vector<std::vector<bool>> values;
      auto add_letter = [&] (const bdd& letter) {
        bdd cube = bdd_satoneset (letter, all_outputs, bddtrue);
        std::vector<bool> row;
        row.reserve (output_vars.size ());
        for (int var : output_vars)
          row.push_back ((cube & bdd_ithvar (var)) != bddfalse);
        values.push_back (std::move (row));
      };
      for (const bdd& letter : word->prefix)
        add_letter (letter);
      const size_t loop_start = values.size ();
      for (const bdd& letter : word->cycle)
        add_letter (letter);

      if (values.empty ())
        values.push_back (std::vector<bool> (output_aps.size (), false));
      write_no_input_strategy (output_aps, values, loop_start, *synth_fname);
    }
    return true;
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
      const TRANSLATION_PREF_T translation_pref;
      const SPOT_FAST_T spot_fast;
      spot::option_map extra_options {acacia::translation::make_options ()};
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
                   TRANSLATION_PREF_T translation_pref,
                   SPOT_FAST_T spot_fast,
                   const std::optional<std::string>& synth_fname)
        : dict {dict},
          input_aps {input_aps},
          output_aps {output_aps},
          opt_k {opt_k},
          opt_kmin {opt_kmin},
          opt_kinc {opt_kinc},
          check_unreal {check_unreal},
          translation_pref {translation_pref},
          spot_fast {spot_fast},
          synth_fname {synth_fname} {
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
        acacia::translation::validate_options (extra_options);
        spot::twa_graph_ptr aut;
        {
#if ACACIA_ENABLE_DIAGNOSTICS
          auto* diag = acacia::diagnostics::current ();
          acacia::diagnostics::scoped_timer timer (diag ? &diag->translation_ms : nullptr);
#endif
          aut = create_automaton (spot_formula, trans, translation_pref);
        }
        observe_translated_automaton (aut);
        acacia::diagnostics::snapshot ("synthesis-check-after-translation");
        assert (not aut->intersects (mealy_aig->as_automaton (false)));

#endif
      }

      bool operator() (spot::formula spot_formula) {
        // NOTE: realizability-preserving simplification (spot::realizability_
        // simplifier) is applied ONCE up front in run_ltl, on the original
        // spec in the standard (Mealy) frame, before the input/output swap --
        // so it is already baked into spot_formula here for every orientation.
        // It must NOT be re-applied on the swapped/X-shifted formula below:
        // the unreal paths feed the un-negated formula into an avoid/dual
        // game, so simplifying it in this frame answers the wrong question
        // and is unsound (it flipped realizable instances to UNREALIZABLE).
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
        acacia::translation::validate_options (extra_options);
        spot::twa_graph_ptr aut;
        {
#if ACACIA_ENABLE_DIAGNOSTICS
          auto* diag = acacia::diagnostics::current ();
          acacia::diagnostics::scoped_timer timer (diag ? &diag->translation_ms : nullptr);
#endif
          aut = create_automaton (spot_formula, trans, translation_pref);
        }
        observe_translated_automaton (aut);
        acacia::diagnostics::snapshot ("after-translation");

        // If unreal but we haven't pushed inputs yet using X on formula.
        if (check_unreal.has_value () and *check_unreal == UNREAL_X_AUTOMATON) {
          verb_do (2, vout << "Pushing the inputs in the automaton\n");
          aut = push_aps (aut, all_inputs, all_outputs);
          if (aut == nullptr) {
            verb_do (1, vout << "Input-push expansion limit reached; inconclusive\n");
            return acacia::diagnostics::finish (false, "input-push-limit");
          }
          if (aut->num_states () > 0 and not aut->prop_state_acc ().is_true ()) {
            [[maybe_unused]] const auto old_states = aut->num_states ();
            aut = spot::sbacc (aut);
            verb_do (1, vout << "Converted pushed automaton to state-based acceptance: "
                             << old_states << " -> " << aut->num_states ()
                             << " states." << std::endl);
          }
          observe_translated_automaton (aut);
          acacia::diagnostics::snapshot ("after-input-push");
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
          return acacia::diagnostics::finish (not check_unreal.has_value (),
                                              "empty-translated-automaton");
        }

        const bool want_controller_strategy = synth_fname.has_value () and not check_unreal.has_value ();
        // The nondeterministic GFG path is decision-only and currently validated
        // only in the REAL-child orientation. Keep unreal children on the
        // deterministic fast path or the existing Acacia solver.
        const bool allow_gfg_decision = not check_unreal.has_value ();
        auto fast = acacia::spot_fastpath::try_spot_nba_fast_path (
            aut, all_inputs, all_outputs, want_controller_strategy, allow_gfg_decision, spot_fast);
#if ACACIA_ENABLE_DIAGNOSTICS
        if (auto* diag = acacia::diagnostics::current ()) {
          if (fast.classification_ran) {
            diag->fast_class =
                std::string (acacia::spot_fastpath::detail::class_name (fast.classification));
            diag->fast_class_ms += fast.classification_ms;
            diag->fast_solve_ms += fast.solve_ms;
            diag->fast_verdict = fast.conclusive
                ? (fast.current_output_player_wins ? "solved-winning" : "solved-losing")
                : "fallback";
          }
          else {
            diag->fast_class = "off";
            diag->fast_verdict = "fallback";
          }
        }
#endif
        if (fast.conclusive) {
          if (fast.strategy.has_value ()) {
            assert (want_controller_strategy);
            strats.push_back (*fast.strategy);
          }
          verb_do (1, vout << "Spot NBA fast path returning "
                           << fast.current_output_player_wins << "\n");
          return acacia::diagnostics::finish (fast.current_output_player_wins, "spot-fast-path");
        }
        acacia::diagnostics::snapshot ("after-spot-fast");

        {
#if ACACIA_ENABLE_DIAGNOSTICS
          auto* diag = acacia::diagnostics::current ();
          if (diag != nullptr) {
            diag->preprocessor = ACACIA_DIAG_STRINGIFY (AUT_PREPROCESSOR);
            diag->preproc_states_before = aut->num_states ();
            diag->preproc_edges_before = edge_count (aut);
          }
          acacia::diagnostics::scoped_timer timer (diag ? &diag->preproc_ms : nullptr);
#endif
          AUT_PREPROCESSOR::make (aut, all_inputs, all_outputs, opt_k) ();
#if ACACIA_ENABLE_DIAGNOSTICS
          if (diag != nullptr) {
            diag->preproc_states_after = aut->num_states ();
            diag->preproc_edges_after = edge_count (aut);
          }
#endif
        }
        acacia::diagnostics::snapshot ("after-preprocessing");

        // surely_losing can flush every reachable state and leave the
        // automaton empty after purging. Map this to inconclusive on
        // both paths rather than crashing downstream.
        if (aut->num_states () == 0) {
          verb_do (1, vout << "Automaton is empty after preprocessing; inconclusive\n");
          return acacia::diagnostics::finish (false, "empty-after-preprocessing");
        }

        posets::vectors::bool_threshold = (BOOLEAN_STATES::make (aut, opt_k)) ();
#if ACACIA_ENABLE_DIAGNOSTICS
        if (auto* diag = acacia::diagnostics::current ())
          diag->bool_threshold = posets::vectors::bool_threshold;
#endif
        acacia::diagnostics::snapshot ("before-solve");
        verb_do (1, vout << "Found " << posets::vectors::bool_threshold << " boolean states.\n");
        verb_do (4, dict->dump (utils::vout));

        // [DIAG] symmetry detection on the final game automaton. Keep this
        // behind an explicit macro so verbose timing runs do not pay for a
        // duplicate recognition pass before solve_game().
#if ACACIA_SYMMETRY_VERBOSE_DIAGNOSTICS
        {
          const auto analysis = symmetry::analyze_indexed_aps (aut, all_inputs, all_outputs);
          const auto sg = symmetry::detect (aut, analysis);
          const auto report = symmetry::describe (analysis, sg, aut->num_states ());
          acacia::diagnostics::set_symmetry_structure (report);
          acacia::diagnostics::snapshot ("after-symmetry-diagnostics");

          verb_do (1, {
            vout << "[symmetry] generators=" << sg.size ()
                 << " full_symmetric=" << sg.full_symmetric
                 << " indices=" << report.indices
                 << " subsets=" << report.subsets
                 << " selected=" << report.selected << std::endl;
            if (report.blocks != "-")
              vout << "[symmetry] block_layout: blocks=" << report.blocks
                   << " shared=" << report.shared << std::endl;
            else
              vout << "[symmetry] block_layout: none (declined)\n";
          });
        }
#endif

        assert (not synth_fname.has_value () or not check_unreal.has_value ());
        std::optional<spot::twa_graph_ptr> maybe_strat;
        {
#if ACACIA_ENABLE_DIAGNOSTICS
          auto* diag = acacia::diagnostics::current ();
          acacia::diagnostics::scoped_timer timer (diag ? &diag->solve_ms : nullptr);
#endif
          maybe_strat =
              solve_game (aut, opt_k, opt_kmin, opt_kinc,
                          // we obtain the subset of inputs by projecting out the set of all
                          // outputs from the cube of all atomic propositions
                          bdd_exist (aut->ap_vars (), all_outputs),
                          // same for the outputs
                          bdd_exist (aut->ap_vars (), all_inputs), synth_fname.has_value ());
        }
#if ACACIA_ENABLE_DIAGNOSTICS
        if (auto* diag = acacia::diagnostics::current ())
          diag->bitset_threshold = posets::vectors::bitset_threshold;
#endif

        if (maybe_strat.has_value ()) {
          if (synth_fname.has_value ())
            strats.push_back (*maybe_strat);
          return acacia::diagnostics::finish (true, "solve-game");
        }
        else {
          return acacia::diagnostics::finish (false, "solve-game-inconclusive");
        }
      }
  };
}  // namespace

bool run_ltl (std::vector<std::string> input_aps, std::vector<std::string> output_aps,
              VECTOR_ELT_T opt_k, VECTOR_ELT_T opt_kmin, VECTOR_ELT_T opt_kinc,
              std::string formula, std::optional<UNREAL_X_T> check_unreal,
              TRANSLATION_PREF_T translation_pref, SPOT_FAST_T spot_fast,
              const std::optional<std::string>& synth_fname) {
  acacia::diagnostics::scoped_child diag_scope (child_path (check_unreal));
#if ACACIA_ENABLE_DIAGNOSTICS
  if (auto* diag = acacia::diagnostics::current ())
    diag->translation_pref = translation_pref_name (translation_pref);
#endif

  spot::formula spot_formula = parse_ltl_string (formula);

  // Realizability-preserving simplification (spot::realizability_simplifier):
  // force/remove input APs whose value cannot affect the verdict (e.g.
  // single-polarity APs), shrinking the automaton before translation --
  // measured ~880x on bounded-response specs with long X-chains.  Applied
  // ONCE here, on the original spec in the standard (Mealy) frame, BEFORE the
  // input/output swap below, so every forked child (real + both unreal
  // strategies) inherits the smaller formula.  This is sound by determinacy:
  // the simplifier preserves realizability of phi, and "phi realizable" <=>
  // "phi not unrealizable", so it equally preserves the unrealizability
  // verdict the unreal children compute.  Mirrors ltlsynt's up-front use of
  // the class; the decomposition branch below also mirrors ltlsynt's
  // component-local pass for real, decision-only children.  Skipped when
  // synthesizing a controller (-s): the removed APs would need
  // patch_mealy/patch_game on the emitted strategy, which is not wired up here.
  if (ACACIA_ENABLE_REALIZABILITY_SIMPLIFIER and not synth_fname.has_value ()) {
    spot::formula before_simplification = spot_formula;
    {
#if ACACIA_ENABLE_DIAGNOSTICS
      auto* diag = acacia::diagnostics::current ();
      acacia::diagnostics::scoped_timer timer (diag ? &diag->rsimp_ms : nullptr);
#endif
      spot::realizability_simplifier rsimp (spot_formula, input_aps);
      spot_formula = rsimp.simplified_formula ();
    }
#if ACACIA_ENABLE_DIAGNOSTICS
    if (auto* diag = acacia::diagnostics::current ())
      diag->rsimp_changed = before_simplification != spot_formula;
#endif
    acacia::diagnostics::snapshot ("after-rsimp");
    verb_do (2, vout << "Simplified formula: " << spot_formula << std::endl);
  }

  // Degenerate alphabets are language questions, not games.  Keep this in the
  // original Mealy frame, after realizability simplification and before the
  // unreal workers swap I/O.  Strategy-producing no-input requests retain the
  // existing lasso-to-AIG path; this fast path is intentionally decision-only.
  if (input_aps.empty () and synth_fname.has_value ())
    return acacia::diagnostics::finish (
        run_no_input_ltl (output_aps, spot_formula, check_unreal, translation_pref, synth_fname),
        "no-input-ltl-synthesis");

  if (not synth_fname.has_value () and (input_aps.empty () or output_aps.empty ())) {
    acacia::degenerate_io::verdict direct;
    {
#if ACACIA_ENABLE_DIAGNOSTICS
      auto* diag = acacia::diagnostics::current ();
      acacia::diagnostics::scoped_timer timer (diag ? &diag->translation_ms : nullptr);
#endif
      direct = acacia::degenerate_io::try_direct (
          spot_formula, input_aps, output_aps, translation_pref);
    }
    assert (direct != acacia::degenerate_io::verdict::unknown);
    const bool child_matches = acacia::syntactic_bypass::matches_worker (
        direct, check_unreal.has_value ());
    return acacia::diagnostics::finish (
        child_matches,
        child_matches ? "degenerate-io" : "degenerate-io-opposite-verdict");
  }

#if ACACIA_ENABLE_SYNTACTIC_BYPASS
  // Spot's direct-strategy check is defined in the original Mealy frame, so
  // it must run before the unreal children swap inputs and outputs.  It
  // returns a formula verdict; map that verdict to the role of this child so
  // only the matching child reports a definitive answer to the parent.
  if (not synth_fname.has_value ()) {
    acacia::syntactic_bypass::result direct;
    {
#if ACACIA_ENABLE_DIAGNOSTICS
      auto* diag = acacia::diagnostics::current ();
      acacia::diagnostics::scoped_timer timer (
          diag ? &diag->syntactic_bypass_ms : nullptr);
#endif
      direct = acacia::syntactic_bypass::try_direct (spot_formula, output_aps);
    }
#if ACACIA_ENABLE_DIAGNOSTICS
    if (auto* diag = acacia::diagnostics::current ())
      diag->syntactic_bypass = acacia::syntactic_bypass::name (direct.value);
#endif
    if (direct.value != acacia::syntactic_bypass::verdict::unknown) {
      const bool child_matches = acacia::syntactic_bypass::matches_worker (
          direct.value, check_unreal.has_value ());
      verb_do (1, vout << "Syntactic bypass found formula "
                       << acacia::syntactic_bypass::name (direct.value) << '\n');
      return acacia::diagnostics::finish (
          child_matches,
          child_matches ? "syntactic-bypass" : "syntactic-bypass-opposite-verdict");
    }
  }
#elif ACACIA_ENABLE_DIAGNOSTICS
  if (auto* diag = acacia::diagnostics::current ())
    diag->syntactic_bypass = "off";
#endif

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
                      translation_pref, spot_fast, synth_fname);

#if DECOMPOSE_SPEC == 0
  // Just launch a monolithic runner.
  if (runner (spot_formula)) {
    if (synth_fname.has_value ()) {
      std::vector<std::vector<std::string>> out_part = {output_aps};
      runner.synthesis (spot_formula, out_part);
    }
    return acacia::diagnostics::finish (true, "monolithic");
  }
  else {
    return acacia::diagnostics::finish (false, "monolithic");
  }

#elif DECOMPOSE_SPEC == 1
  // We are up for decomposition, so first we need to split the formula.
  // NOTE: we may have flipped inputs and outputs already, so we need to
  // provide inputs to the split function in that case.
  std::vector<std::vector<std::string>> out_part;
  auto [forms, outs] = spot::split_independent_formulas (
      spot_formula, check_unreal.has_value () ? input_aps : output_aps);
  acacia::diagnostics::snapshot ("after-decomposition");
  verb_do (2, vout << "Decomposed the input into " << forms.size () << " subformulas\n");

  if (ACACIA_ENABLE_REALIZABILITY_SIMPLIFIER and forms.size () > 1 and
      not check_unreal.has_value () and not synth_fname.has_value ()) {
    bool any_component_simplified = false;
    for (auto& sub_formula : forms) {
      spot::formula before_simplification = sub_formula;
      {
#if ACACIA_ENABLE_DIAGNOSTICS
        auto* diag = acacia::diagnostics::current ();
        acacia::diagnostics::scoped_timer timer (diag ? &diag->rsimp_ms : nullptr);
#endif
        spot::realizability_simplifier rsimp (sub_formula, input_aps);
        sub_formula = rsimp.simplified_formula ();
      }
      any_component_simplified = any_component_simplified or
                                 before_simplification != sub_formula;
    }
#if ACACIA_ENABLE_DIAGNOSTICS
    if (auto* diag = acacia::diagnostics::current ())
      diag->rsimp_changed = diag->rsimp_changed or any_component_simplified;
#endif
    acacia::diagnostics::snapshot ("after-decomposition-rsimp");
    verb_do (2, vout << "Simplified decomposed subformulas: "
                     << (any_component_simplified ? "changed" : "unchanged") << std::endl);
  }

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
    return acacia::diagnostics::finish (true, "decomposition");
  }
  else {
    return acacia::diagnostics::finish (false, "decomposition");
  }

#else
  std::unreachable ();
#endif
}
