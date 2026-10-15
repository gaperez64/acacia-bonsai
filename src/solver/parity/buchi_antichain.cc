#include "solver/parity/buchi_antichain.hh"

#include "posets/vectors/traits.hh"
#include "solver/ap_partition.hh"
#include "solver/configured_components.hh"
#include "solver/phase_observation.hh"
#include "solver/solve_game.hh"
#include "solver/synthesis_rendezvous.hh"
#include "utils/push_aps.hh"

#include <memory>
#include <new>
#include <spot/twaalgos/complete.hh>
#include <spot/twaalgos/degen.hh>
#include <spot/twaalgos/dualize.hh>
#include <spot/twaalgos/remfin.hh>
#include <spot/twaalgos/sbacc.hh>
#include <spot/twaalgos/totgba.hh>
#include <stdexcept>

namespace acacia::parity_route {
  namespace {
    struct worker_automaton {
        spot::twa_graph_ptr graph;
        bdd inputs = bddtrue, outputs = bddtrue;
    };

    worker_automaton partition_copy (const spot::twa_graph_ptr& source,
                                     const std::vector<std::string>& inputs,
                                     const std::vector<std::string>& outputs) {
      worker_automaton result {spot::make_twa_graph (spot::make_bdd_dict ())};
      std::vector<std::pair<int, int>> variables;
      const auto register_names = [&] (const auto& names, bdd& cube) {
        for (const auto& name : names) {
          const auto ap = spot::formula::ap (name);
          const auto variable = result.graph->register_ap (ap);
          cube &= bdd_ithvar (variable);
          variables.emplace_back (source->register_ap (ap), variable);
        }
      };
      // Match the ordinary worker: MONA needs inputs before outputs before
      // state variables. A polarity swap therefore needs a fresh dictionary.
      register_names (inputs, result.inputs);
      register_names (outputs, result.outputs);
      std::unique_ptr<bddPair, decltype (&bdd_freepair)> replace (bdd_newpair (), bdd_freepair);
      if (not replace)
        throw std::bad_alloc ();
      for (const auto& [old_variable, new_variable] : variables)
        bdd_setpair (replace.get (), old_variable, new_variable);
      result.graph->copy_acceptance_of (source);
      result.graph->prop_copy (source, spot::twa::prop_set::all ());
      result.graph->new_states (source->num_states ());
      result.graph->set_init_state (source->get_init_state_number ());
      for (const auto& edge : source->edges ())
        result.graph->new_edge (edge.src, edge.dst, bdd_replace (edge.cond, replace.get ()),
                                edge.acc);
      return result;
    }
  }

  buchi_solution solve_buchi_antichain (const spot::twa_graph_ptr& forbidden,
                                        const std::vector<std::string>& inputs,
                                        const std::vector<std::string>& outputs, bool moore,
                                        const antichain_options& config) {
    buchi_solution answer;
    // Sequential checks, REAL first. Each uses the existing complete K schedule;
    // exhausting a bound is inconclusive, never a proof of the other polarity.
    for (bool real : {true, false}) {
      if (auto* worker = active_worker_record ())
        worker->k = -1;
      worker_route ("antichain", "antichain", real ? "REAL" : "UNREAL");
      auto aut = forbidden;
      if (not real) {
        legacy_phase complement ("buchi-complement");
        // dualize completes even a partial DBA before complementing. The exact
        // Fin elimination guesses the last bad visit; it need not stay deterministic.
        aut = spot::degeneralize (
            spot::to_generalized_buchi (spot::remove_fin (spot::dualize (aut))));
      }
      worker_automaton worker;
      {
        legacy_phase partitioning ("antichain-partition");
        worker = partition_copy (aut, real ? inputs : outputs, real ? outputs : inputs);
        aut = std::move (worker.graph);
      }
      const auto& solver_inputs = worker.inputs;
      const auto& solver_outputs = worker.outputs;
      // Swapping players reverses move order too: push original inputs for
      // REAL Moore, and original outputs (solver inputs) for UNREAL Mealy.
      if (real ? moore : not moore) {
        legacy_phase pushing ("input-push");
        utils::push_aps_observation observation;
        aut = utils::push_aps (aut, solver_inputs, solver_outputs, utils::push_aps_max_states,
                               utils::push_aps_max_edges,
                               phase_records_enabled () ? &observation : nullptr);
        if (phase_records_enabled ())
          observation.publish ();
        pushing.finish (observation.limit_reason, aut == nullptr);
        if (not aut) {
          worker_stopped ("input_push_limit");
          continue;
        }
      }
      {
        // MONA precomputation omits an empty transition relation. A rejecting
        // sink retains those legal letters without changing the NBA language.
        legacy_phase completing ("buchi-completion");
        spot::complete_here (aut);
      }
      {
        legacy_phase lowering ("state-acceptance");
        aut = spot::sbacc (aut);
      }
      if (not aut->acc ().is_buchi ())
        throw std::runtime_error ("antichain input requires Buchi acceptance");
      {
        legacy_phase preprocessing ("preprocessing");
        AUT_PREPROCESSOR::make (aut, solver_inputs, solver_outputs, config.kmax) ();
      }
      if (not aut->num_states ()) {
        worker_stopped ("empty_after_preprocessing");
        continue;
      }
      {
        legacy_phase booleanizing ("booleanization");
        posets::vectors::bool_threshold = BOOLEAN_STATES::make (aut, config.kmax) ();
        legacy_graph (aut);
      }
      const bool synthesize = real and config.synthesis;
      synthesis::controller_capture capture (synthesize ? &answer.controller : nullptr);
      const auto partition = project_ap_partition (aut->ap_vars (), solver_inputs, solver_outputs);
      // Deliberately enter the worker's solver after the Spot shortcut. Its
      // configured backend, downset, K schedule and synthesis fallback are reused.
      // Input pushing preserves AP names; only their player roles change.
      auto hints = config.hints;
      if (not real)
        for (auto& hint : hints)
          hint.is_input = not hint.is_input;
      unsigned winning_k = 0;
      auto solved = solve_game (aut, config.kmax, config.kmin, config.kinc, partition.inputs,
                                partition.outputs, synthesize, hints,
                                real ? config.real_backend : config.unreal_backend,
                                config.candidate, config.equivariance, &winning_k);
      if (phase_records_enabled ())
        if (auto* worker = active_worker_record (); worker && worker->k >= 0)
          answer.stats.k = unsigned (worker->k);
      if (solved) {
        answer.stats.k = winning_k;
        answer.real = real;
        return answer;
      }
    }
    return answer;
  }
}
