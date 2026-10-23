#include "solver/parity/route.hh"

#include "solver/bdd_config.hh"
#include "solver/mealy_to_moore.hh"
#include "solver/spot_nba_fastpath.hh"
#include "solver/synthesis_rendezvous.hh"
#include "solver/translator_options.hh"

#include <algorithm>
#include <chrono>
#include <set>
#include <spot/tl/apcollect.hh>
#include <spot/twaalgos/cleanacc.hh>
#include <spot/twaalgos/degen.hh>
#include <spot/twaalgos/dualize.hh>
#include <spot/twaalgos/game.hh>
#include <spot/twaalgos/isdet.hh>
#include <spot/twaalgos/parity.hh>
#include <spot/twaalgos/remfin.hh>
#include <spot/twaalgos/sbacc.hh>
#include <spot/twaalgos/strength.hh>
#include <spot/twaalgos/synthesis.hh>
#include <spot/twaalgos/toparity.hh>
#include <spot/twaalgos/totgba.hh>
#include <spot/twaalgos/translate.hh>
#include <spot/twaalgos/zlktree.hh>
#include <stdexcept>

namespace acacia::parity_route {
  namespace {
    void require_deterministic (const spot::twa_graph_ptr& aut) {
      // Parsed HOA property claims are not trusted.
      aut->prop_universal (spot::trival::maybe ());
      aut->prop_complete (spot::trival::maybe ());
      aut->prop_very_weak (spot::trival::maybe ());
      aut->prop_weak (spot::trival::maybe ());
      aut->prop_inherently_weak (spot::trival::maybe ());
      aut->prop_terminal (spot::trival::maybe ());
      if (not spot::is_deterministic (aut))
        throw std::runtime_error ("route requires a deterministic existential automaton");
    }
    spot::twa_graph_ptr plain_buchi (spot::twa_graph_ptr aut) {
      if (aut->acc ().is_buchi ())
        return aut;
      if (not aut->acc ().is_generalized_buchi ())
        aut = spot::to_generalized_buchi (aut);
      aut = spot::sbacc (spot::degeneralize (aut));
      require_deterministic (aut);
      if (not aut->acc ().is_buchi ())
        throw std::runtime_error ("deterministic conversion did not produce Buchi acceptance");
      return aut;
    }
  }

  classification classify (spot::twa_graph_ptr worker) {
    require_deterministic (worker);
    // All reductions preserve the worker's own language. No complementation here.
    worker = spot::simplify_acceptance (worker);
    if (worker->acc ().is_buchi () or worker->acc ().is_t () or worker->acc ().is_f ())
      return {1, "Buchi or constant acceptance", plain_buchi (worker)};
    if (not worker->acc ().uses_fin_acceptance ()) {
      auto gba = spot::to_generalized_buchi (worker);
      require_deterministic (gba);
      return {2, "no finite-occurrence acceptance", plain_buchi (gba)};
    }
    if (auto buchi = spot::rabin_to_buchi_if_realizable (worker))
      return {3, "Rabin to deterministic Buchi", plain_buchi (buchi)};
    if (spot::is_weak_automaton (worker)) {
      auto buchi = spot::remove_fin (worker);
      require_deterministic (buchi);
      return {3, "weak deterministic acceptance", plain_buchi (buchi)};
    }
    // ACD is exact and preserves determinism, including for general TELA. Its
    // reduced parity exposes additional Buchi-realizable cases before tier four.
    auto reduced = spot::acd_transform (worker);
    spot::reduce_parity_here (reduced);
    reduced = spot::simplify_acceptance (reduced);
    if (reduced->acc ().is_buchi () or reduced->acc ().is_t () or reduced->acc ().is_f () or
        not reduced->acc ().uses_fin_acceptance ())
      return {3, "parity reduction to deterministic Buchi", plain_buchi (reduced)};
    if (auto buchi = spot::rabin_to_buchi_if_realizable (reduced))
      return {3, "reduced parity to deterministic Buchi", plain_buchi (buchi)};
    return {4, "genuine parity acceptance", nullptr};
  }

  result run (request input, options config, observer observe) {
    result output;
    if (config.verify and not config.synthesis)
      throw std::runtime_error ("controller verification requires synthesis");
    if (config.verify and not input.specification)
      throw std::runtime_error ("the original specification is required for verification");
    if (not input.automaton and not input.formula)
      throw std::runtime_error ("a formula or deterministic HOA is required");
    if (input.source == source_language::original and input.frame != timing_frame::original)
      throw std::runtime_error ("original specifications require the original timing frame");
    std::set<std::string> names;
    for (const auto& partition : {input.inputs, input.outputs})
      for (const auto& name : partition)
        if (name.empty () or not names.insert (name).second)
          throw std::runtime_error ("AP names must be nonempty, distinct and disjoint");
    const auto check_aps = [&] (spot::formula f) {
      const auto aps = std::unique_ptr<spot::atomic_prop_set> {spot::atomic_prop_collect (f)};
      for (const auto& ap : *aps)
        if (not names.contains (ap.ap_name ()))
          throw std::runtime_error ("formula AP is missing from the declared partition");
    };
    if (input.specification)
      check_aps (input.specification);
    if (input.formula)
      check_aps (input.formula);
    spot::twa_graph_ptr aut = input.automaton;
    auto dictionary = aut ? aut->get_dict () : spot::make_bdd_dict ();
    if constexpr (ACACIA_BDD_MAX_INCREASE > 0)
      configure_bdd ();
    input.automaton.reset ();
    auto stage = [&] (const char* name, auto action) {
      if (observe)
        observe (name, aut, -1);
      const auto start = std::chrono::steady_clock::now ();
      action ();
      if (observe)
        observe (
            name, aut,
            std::chrono::duration<double> (std::chrono::steady_clock::now () - start).count ());
    };
    if (not aut)
      stage ("translate", [&] {
        auto opts = acacia::translation::make_options ();
        spot::translator translator (dictionary, &opts);
        acacia::translation::validate_options (opts);
        translator.set_type (spot::postprocessor::Generic);
        translator.set_pref (spot::postprocessor::Deterministic);
        translator.set_level (spot::postprocessor::High);
        aut = translator.run (input.formula);
        if (!aut)
          throw std::runtime_error ("translation did not produce an automaton");
      });
    require_deterministic (aut);
    for (const auto& ap : aut->ap ())
      if (not names.contains (ap.ap_name ()))
        throw std::runtime_error ("HOA AP is missing from the declared partition");
    // Register eliminated/unused APs on the arena as well, retaining the complete
    // partition for splitting, strategy extraction and AIG verification.
    bdd outputs = bddtrue;
    for (const auto& name : input.inputs)
      aut->register_ap (spot::formula::ap (name));
    for (const auto& name : input.outputs)
      outputs &= bdd_ithvar (aut->register_ap (spot::formula::ap (name)));
    spot::twa_graph_ptr worker;
    stage ("orient", [&] {
      worker = input.source == source_language::original ? spot::dualize (aut) : aut;
    });
    classification tier;
    stage ("classify", [&] { tier = classify (worker); });
    worker.reset ();
    output.tier = tier.tier;
    output.tier_reason = tier.reason;
    if (config.on_classified)
      config.on_classified (output.tier, output.tier_reason);
    const bool native_moore = input.moore and input.frame == timing_frame::original;
    spot::synthesis_info synthesis;
    synthesis.moore = native_moore;
    synthesis.minimize_lvl = 0;
    synthesis.sp = spot::synthesis_info::splittype::AUTO;
    spot::twa_graph_ptr arena;
    bool winning = false;
    acacia::synthesis::controller_builder buchi_controller;
    if (output.tier < 4) {
      output.route = "acacia";
      aut = tier.buchi;
      const bool keep_arena = config.verify or config.on_game or config.on_solution;
      // The incumbent deterministic negated-specification Buchi shortcut, with its usual
      // completion and co-Buchi game. Native Moore uses the same Spot split API
      // with output-first order; shipping callers keep the default Mealy frame.
      stage ("acacia", [&] {
        for (const auto& name : input.inputs)
          tier.buchi->register_ap (spot::formula::ap (name));
        for (const auto& name : input.outputs)
          tier.buchi->register_ap (spot::formula::ap (name));
        // Production needs only the verdict and, after selection, the controller.
        acacia::synthesis::controller_capture capture (&buchi_controller);
        auto fast = acacia::spot_fastpath::deterministic_negated_spec_fast_path (
            tier.buchi, outputs, config.synthesis and not keep_arena, keep_arena, native_moore);
        winning = fast.current_output_player_wins;
        if (keep_arena)
          arena = *fast.solved_arena;
      });
      if (keep_arena)
        stage ("explicit", [&] {
          output.game = std::make_unique<parity::arena_game> (arena);
          aut = arena;
          output.solution = output.game->extract ();
          if (config.on_game)
            config.on_game (*output.game);
        });
    }
    else {
      output.route = "spot";
      stage ("parity", [&] {
        // For captured REAL workers, complement exactly once after classification.
        // Original-formula mode reuses its positive translation for the game.
        auto objective = input.source == source_language::real_worker ? spot::dualize (aut) : aut;
        if (config.use_lar) {
          aut = spot::to_parity (objective);
          spot::reduce_parity_here (aut);
        }
        else {
          aut = spot::acd_transform (objective);
        }
      });
      stage ("split", [&] {
        arena = spot::split_2step (aut, outputs, true, synthesis.sp, native_moore);
        spot::set_synthesis_outputs (arena, outputs);
        aut = arena;
      });
      stage ("solve", [&] { winning = spot::solve_parity_game (arena, true); });
      if (config.verify or config.on_game or config.on_solution)
        stage ("explicit", [&] {
          output.game = std::make_unique<parity::arena_game> (arena);
          output.solution = output.game->extract ();
          if (config.on_game)
            config.on_game (*output.game);
        });
    }
    if (config.on_solved)
      config.on_solved ();
    if (config.on_solution)
      stage ("solution-output", [&] { config.on_solution (*output.game, output.solution); });
    if (config.verify)
      stage ("verify-game", [&] {
        std::string reason;
        if (not parity::check_strategy (output.game->explicit_game, output.solution, &reason))
          throw std::runtime_error ("game strategy check failed: " + reason);
      });
    if (config.on_verdict)
      config.on_verdict (winning);
    if (winning and config.synthesis) {
      stage ("strategy", [&] {
        if (buchi_controller)
          output.controller = buchi_controller ();
        else
          output.controller = spot::solved_game_to_separated_mealy (arena, synthesis);
      });
      stage ("encode-controller", [&] {
        if (not output.controller)
          throw std::runtime_error ("missing controller strategy");
        output.circuit =
            spot::mealy_machine_to_aig (output.controller, "isop", input.inputs, input.outputs);
        if (input.moore and input.frame == timing_frame::effective_mealy)
          output.circuit = acacia::synthesis::mealy_to_moore (output.circuit, true);
      });
      if (config.verify) {
        stage ("verify-controller", [&] {
          if (not spot::is_input_deterministic_mealy (output.controller))
            throw std::runtime_error ("controller is not input deterministic");
          if (input.moore) {
            std::vector<bool> depends (output.circuit->max_var () / 2 + 1);
            for (unsigned i = 0; i < output.circuit->num_inputs (); ++i)
              depends[output.circuit->input_var (i) / 2] = true;
            const auto& gates = output.circuit->gates ();
            for (unsigned i = 0; i < gates.size (); ++i)
              depends[output.circuit->gate_var (i) / 2] =
                  depends[gates[i].first / 2] or depends[gates[i].second / 2];
            for (unsigned literal : output.circuit->outputs ())
              if (depends[literal / 2])
                throw std::runtime_error ("Moore controller depends on a current input");
          }
          // Fresh original-specification translation, independent of the game route,
          // with its entire cost included in verification, never in solve time.
          const auto controller_automaton = output.circuit->as_automaton (false);
          spot::translator checker (controller_automaton->get_dict ());
          checker.set_type (spot::postprocessor::BA);
          auto negated_spec_aut = checker.run (spot::formula::Not (input.specification));
          if (negated_spec_aut->intersects (controller_automaton))
            throw std::runtime_error ("controller violates the original specification");
        });
        output.verification = "original-specification product emptiness";
      }
    }
    if (winning)
      output.verdict = "REALIZABLE";
    else {
      if (config.verify)
        output.verification = "environment strategy on game; no independent certificate";
      output.verdict = "UNREALIZABLE";
    }
    output.checked = config.verify;
    return output;
  }
}
