#pragma once

#include "actioners/direction.hh"
#include "configuration.hh"
#include "solver/antichain_snapshot.hh"
#include "solver/diagnostics.hh"
#include "solver/k_schedule.hh"
#include "solver/phase_observation.hh"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <fstream>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <random>
#include <spot/twa/formula2bdd.hh>
#include <spot/twa/twagraph.hh>
#include <sstream>
#include <utils/verbose.hh>

#include <posets/utils/vector_mm.hh>
#include <posets/vectors.hh>


/// \brief Wrapper class around a UcB to pass as the deterministic safety
/// automaton S^K_N, for N a given UcB.
template <class SetOfStates, class IOsPrecomputationMaker, class ActionerMaker,
          class InputPickerMaker>
class k_bounded_safety_aut_detail {
    using state = typename SetOfStates::value_type;

  public:
    k_bounded_safety_aut_detail (spot::twa_graph_ptr aut, VECTOR_ELT_T kfrom, VECTOR_ELT_T kto,
                                 VECTOR_ELT_T kinc, bdd input_support, bdd output_support,
                                 const IOsPrecomputationMaker& ios_precomputer_maker,
                                 const ActionerMaker& actioner_maker,
                                 const InputPickerMaker& input_picker_maker)
      : aut {aut},
        kfrom {kfrom},
        kto {kto},
        kinc {kinc},
        input_support {input_support},
        output_support {output_support},
        gen {0},
        ios_precomputer_maker {ios_precomputer_maker},
        actioner_maker {actioner_maker},
        input_picker_maker {input_picker_maker} {}

    [[nodiscard]] spot::formula bdd_to_formula (const bdd& f) const {
      return spot::bdd_to_formula (f, aut->get_dict ());
    }

    auto get_inputs_to_ios () {
      return (ios_precomputer_maker.make (aut, input_support, output_support)) ();
    }

    std::optional<std::pair<VECTOR_ELT_T, SetOfStates>> solve () {

#if ACACIA_ENABLE_DIAGNOSTICS
      acacia::antichain_snapshot::configure (aut);
#endif

      VECTOR_ELT_T k = kfrom;
      acacia::diagnostics::set_support_backend ("backward");
      acacia::diagnostics::set_support_graph (aut);
      acacia::diagnostics::set_support_k (static_cast<int> (k));
#if ACACIA_ENABLE_DIAGNOSTICS
      std::optional<acacia::diagnostics::scoped_timer> construction_timer;
      if (acacia::diagnostics::support_demand_enabled ()) {
        if (auto* diag = acacia::diagnostics::current ())
          construction_timer.emplace (&diag->support_action_construction_ms);
        acacia::diagnostics::snapshot ("support-before-action-construction");
      }
#endif

      // Precompute the input and output actions.
      acacia::legacy_phase preparing ("io-preparation");
      auto inputs_to_ios = get_inputs_to_ios ();
      preparing.finish ();
      // ^ ios_precomputers::detail::standard_container<shared_ptr<spot::twa_graph>,
      // vector<pair<int, int>>>
      verb_do (1, vout << "Make actions..." << std::endl);
      auto actioner = actioner_maker.make (aut, inputs_to_ios, k);
      verb_do (1, vout << "Fetching IO actions" << std::endl);
      // The actioner owns this table for the whole solve.  The picker may
      // reorder actions within each input, so keep a mutable view of it.
      auto& input_output_fwd_actions = actioner.actions ();
      acacia::diagnostics::set_support_actions (input_output_fwd_actions);
#if ACACIA_ENABLE_DIAGNOSTICS
      construction_timer.reset ();
#endif
#if ACACIA_ENABLE_DIAGNOSTICS
      acacia::antichain_snapshot::record_all_input_actions (input_output_fwd_actions);
#endif  // list<pair<bdd, list<action_vec>>>
      verb_do (1, io_stats (input_output_fwd_actions));

      // What is the initial state?
      posets::utils::vector_mm<VECTOR_ELT_T> init (aut->num_states ());
      init.assign (aut->num_states (), -1);
      init[aut->get_init_state_number ()] = 0;

      // What are the safe states?
      auto safe_vector = posets::utils::vector_mm<VECTOR_ELT_T> (aut->num_states (), k - 1);
      for (size_t i = posets::vectors::bool_threshold; i < aut->num_states (); ++i)
        safe_vector[i] = 0;
      SetOfStates f = SetOfStates (state (safe_vector));

      acacia::legacy_phase picking ("picker-preparation");
      auto input_picker = input_picker_maker.make (input_output_fwd_actions, actioner);
      picking.finish ();
      acacia::legacy_inapplicable ("verification", "inapplicable-fixed-point");
      std::optional<acacia::legacy_phase> searching;
      if (acacia::phase_records_enabled ()) {
        if (auto* w = acacia::active_worker_record ())
          w->k = k;
        searching.emplace ("search");
      }
      struct SearchObservation {
          std::optional<acacia::legacy_phase>*& slot;
          ~SearchObservation () { slot = nullptr; }
      } observation {search_observation};
      search_observation = &searching;
      int loopcount = 0;
      acacia::diagnostics::snapshot ("after-action-construction");

      do {
        loopcount++;
        acacia::diagnostics::observe_loop (f.size (), k);
#if ACACIA_ENABLE_DIAGNOSTICS
        acacia::antichain_snapshot::observe (f, k, loopcount);
#endif
        verb_do (1, vout << "Loop# " << loopcount << ", f of size " << f.size () << std::endl);


        auto input = [&] {
          acacia::diagnostics::scoped_fine_timer timer {acacia::diagnostics::fine_metric::picker};
          return input_picker (f);
        }();
        acacia::diagnostics::snapshot_loop_progress ("classic-after-picker");
        if (not input.has_value ())  // No more inputs, and we just tested that init was present
        {
          verb_do (3, vout << "Exit because of no more inputs being picked\n");
          acacia::diagnostics::set_final_reason ("fixedpoint");
#if ACACIA_ENABLE_DIAGNOSTICS
          acacia::antichain_snapshot::record_final (f, k, loopcount);
#endif
          return std::make_optional<std::pair<VECTOR_ELT_T, SetOfStates>> (
              std::make_pair (k, std::move (f)));
        }

        cpre_inplace (f, *input, actioner, k, loopcount);
        acacia::diagnostics::snapshot_loop_progress ("classic-after-cpre");

        if (not f.contains (state (init))) {
          if (not raise_bound_or_give_up (f, k, actioner))
            return std::nullopt;
          continue;
        }

        // verb_do (1, vout << "Loop# " << loopcount << ", f of size " << f.size () << std::endl);
      } while (true);

      verb_do (2, vout << "Aborting!\n");
      std::abort ();
      return std::nullopt;
    }

    // Disallow copies.
    k_bounded_safety_aut_detail (k_bounded_safety_aut_detail&&) = delete;
    k_bounded_safety_aut_detail& operator= (k_bounded_safety_aut_detail&&) = delete;

  private:
    spot::twa_graph_ptr aut;
    const VECTOR_ELT_T kfrom, kto, kinc;
    bdd input_support, output_support;
    std::mt19937 gen {};
    const IOsPrecomputationMaker& ios_precomputer_maker;
    const ActionerMaker& actioner_maker;
    const InputPickerMaker& input_picker_maker;
    std::optional<acacia::legacy_phase>* search_observation = nullptr;

    // This is also sound when `f` is the pre-CPre region X.  The post-CPre
    // region Y is a subset of X, and the lift is monotone, so lifting X gives a
    // larger, still-sound warm start for the next bound.
    template <typename Actioner>
    bool raise_bound_or_give_up (SetOfStates& f, VECTOR_ELT_T& k, Actioner& actioner) {
      const auto next_k = acacia::k_schedule::next (static_cast<long long> (k), static_cast<long long> (kfrom), static_cast<long long> (kto), static_cast<long long> (kinc));
      if (not next_k.has_value ()) {
        verb_do (2, vout << "Early exit because the initial state is out\n");
        acacia::diagnostics::set_final_reason ("kmax-initial-out");
        return false;
      }

      return raise_bound_to_or_give_up (f, k, actioner, *next_k);
    }

    template <typename Actioner>
    bool raise_bound_to_or_give_up (SetOfStates& f, VECTOR_ELT_T& k, Actioner& actioner,
                                    long long next_k) {
      const long long current_k = static_cast<long long> (k);
      const long long delta = next_k - current_k;
      assert (delta > 0);
      assert (next_k >= static_cast<long long> (std::numeric_limits<VECTOR_ELT_T>::lowest ()));
      assert (next_k <= static_cast<long long> (std::numeric_limits<VECTOR_ELT_T>::max ()));
      verb_do (1, vout << "Incrementing k from " << (int) k << " to " << next_k << std::endl);
      if (search_observation && *search_observation) {
        (*search_observation)->finish ("bound-refuted");
        search_observation->reset ();
        if (auto* w = acacia::active_worker_record ())
          w->k = next_k;
        search_observation->emplace ("search");
      }
      k = static_cast<VECTOR_ELT_T> (next_k);
      actioner.setK (k);
      acacia::diagnostics::set_support_k (static_cast<int> (k));
      acacia::diagnostics::set_k_last_next (static_cast<int> (next_k));
#if ACACIA_ENABLE_DIAGNOSTICS
      acacia::antichain_snapshot::note_bound_raised ();
#endif
      verb_do (1, {
        vout << "Adding bound delta to every vector...";
        vout.flush ();
      });
      f = f.apply ([&] (const state& s) {
        auto vec = posets::utils::vector_mm<VECTOR_ELT_T> (s.size (), 0);
        for (size_t i = 0; i < posets::vectors::bool_threshold; ++i) {
          const long long widened = static_cast<long long> (s[i]) + delta;
          assert (widened >=
                  static_cast<long long> (std::numeric_limits<VECTOR_ELT_T>::lowest ()));
          assert (widened <= static_cast<long long> (std::numeric_limits<VECTOR_ELT_T>::max ()));
          vec[i] = static_cast<VECTOR_ELT_T> (widened);
        }
        // Other entries are set to 0 by initialization, since they are bool.
        return state (vec);
      });
      verb_do (1, vout << "Done" << std::endl);
      return true;
    }

    // This computes f = CPre(f), in the following way:
    // UPre(f) = f \cap f1i
    // f1i = \cup_{o \in O} f1io
    // f1io = PreHat (f, i, o)
    // Both backward apply sites below are preimages: m[p] is a destination
    // bound, including -1 constraints. supp(m) is not outgoing-source demand.
    // Raising K requests no transition rows.
    template <typename Action, typename Actioner>
    void cpre_inplace (SetOfStates& f, const Action& io_action, Actioner& actioner,
                       [[maybe_unused]] int k = -1, [[maybe_unused]] int loop = -1) {
      acacia::diagnostics::scoped_fine_timer cpre_timer {acacia::diagnostics::fine_metric::cpre};
      verb_do (2, vout << "Computing cpre(f) with f = " << std::endl << f);

      const auto& [input, actions] = io_action.get ();
#if ACACIA_ENABLE_DIAGNOSTICS
      // The action vectors determine this update completely, so recording them
      // with the region before and after makes it replayable offline without
      // the automaton or any BDD.
      const bool recording_cpre = [&] {
        std::ostringstream cube;
        cube << bdd_to_formula (input);
        return acacia::antichain_snapshot::record_cpre_before (f, k, loop, cube.str (), actions);
      }();
#endif
      posets::utils::vector_mm<VECTOR_ELT_T> v (aut->num_states (), -1);
      auto vv = typename SetOfStates::value_type (v);
      SetOfStates f1i (std::move (vv));
      bool first_turn = true;
      {
        for (const auto& action_vec : actions) {
          verb_do (3, vout << "one_output_letter:" << std::endl);

          acacia::diagnostics::observe_action ();
          SetOfStates f1io = [&] {
            acacia::diagnostics::scoped_downset_timer downset_timer;
            return f.apply ([this, &action_vec, &actioner] (const auto& m) {
              acacia::diagnostics::scoped_fine_timer apply_timer {
                  acacia::diagnostics::fine_metric::apply};
              auto ret = actioner.apply (m, action_vec, actioners::direction::backward);
              verb_do (3, vout << "  " << m << " -> " << ret << std::endl);
              return ret;
            });
          }();

          if (first_turn) {
            f1i = std::move (f1io);
            first_turn = false;
          }
          else {
            acacia::diagnostics::scoped_downset_timer downset_timer;
            f1i.union_with (std::move (f1io));
          }
          acacia::diagnostics::snapshot_action_progress ();
        }
      }
      // A third variant -- no unions at all, with intersect taking a list --
      // was reserved here as an #error. acacia_cpre_avoid_unions is a boolean,
      // so that arm was unreachable twice over; this comment is what it was
      // actually carrying.

      {
        acacia::diagnostics::observe_meets (f.size (), f1i.size ());
        acacia::diagnostics::snapshot_intersection_progress ();
        acacia::diagnostics::scoped_downset_timer downset_timer;
        f.intersect_with (std::move (f1i));
      }
#if ACACIA_ENABLE_DIAGNOSTICS
      if (recording_cpre)
        acacia::antichain_snapshot::record_cpre_after (f);
#endif
      // Experimentally, this is not faster:
      //   f1i.intersect_with (std::move (f));
      //   f = std::move (f1i);
      verb_do (2, vout << "f = " << std::endl << f);
    }

    ////////////////////////////////////////////////

    template <typename IToActions>
    void io_stats (const IToActions& inputs_to_actions) {
      size_t all_io = 0;
      for (const auto& [inputs, ios] : inputs_to_actions) {
        verb_do (1, vout << "INPUT: "
                         << bdd_to_formula (inputs)
                         /*   */
                         << " #ACTIONS: " << ios.size () << std::endl);
        all_io += ios.size ();
      }
      auto ins = input_support;
      size_t all_inputs_size = 1;
      while (ins != bddtrue) {
        all_inputs_size *= 2;
        ins = bdd_high (ins);
      }

      auto outs = output_support;
      size_t all_outputs_size = 1;
      while (outs != bddtrue) {
        all_outputs_size *= 2;
        outs = bdd_high (outs);
      }

      utils::vout << "INPUT GAIN: " << inputs_to_actions.size () << "/" << all_inputs_size << " = "
                  << (inputs_to_actions.size () * 100 / all_inputs_size) << "%\n"
                  << "IO GAIN: " << all_io << "/" << all_inputs_size * all_outputs_size << " = "
                  << (all_io * 100 / (all_inputs_size * all_outputs_size)) << "%" << std::endl;
    }
};
