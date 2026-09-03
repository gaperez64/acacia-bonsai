#pragma once

#undef MAX_CRITICAL_INPUTS
#define MAX_CRITICAL_INPUTS 1

#include "actioners/direction.hh"
#include "configuration.hh"
#include "utils/bdd_helper.hh"
#include "utils/lambda_ptr.hh"
#include "utils/ref_ptr_cmp.hh"
#include "solver/antichain_snapshot.hh"
#include "solver/k_schedule.hh"
#include "solver/local_certificate.hh"

#include <sstream>
#include "solver/diagnostics.hh"
#include "solver/symmetry_profile.hh"
#include "utils/typeinfo.hh"

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
#include <utils/verbose.hh>

#include <posets/utils/vector_mm.hh>
#include <posets/vectors.hh>

#if ACACIA_LOCAL_CERTIFICATE
namespace acacia::solver_detail {

  struct local_probe_frontier {
      std::array<std::size_t, 6> size_marks {16, 64, 256, 1024, 4096, 16384};
      std::array<bool, 6> mark_taken {};
      bool first_loop_taken = false;

      [[nodiscard]] bool observe (std::size_t size) {
        bool wanted = false;
        if (not first_loop_taken) {
          first_loop_taken = true;
          wanted = true;
        }
        for (std::size_t i = 0; i < size_marks.size (); ++i)
          if (not mark_taken[i] and size >= size_marks[i]) {
            mark_taken[i] = true;
            wanted = true;
          }
        return wanted;
      }

      void restart () {
        mark_taken.fill (false);
        first_loop_taken = false;
      }

      void reset_marks () { mark_taken.fill (false); }
  };

  /// Both budgets are sized by measurement against the G2s panel.
  ///
  /// 32 generators: every certificate the campaign verified is smaller --
  /// robot_grid2_2 needs 6, finding_nemo_1 8, finding_nemo_2 16, lift3 26 --
  /// and a larger cap only makes a failing search more expensive, since each
  /// node rescans every generator against every input and action.
  ///
  /// 400,000 cumulative forward applications per bound, which is four probes
  /// at the 100,000 per-run cap.  Four is what the winners need, and it was
  /// measured from per-probe traces rather than inferred: every win arrives on
  /// the last probe its bound allows.  finding_nemo_2 wins on the third at
  /// region size 389 having already spent 200,000; lift3 wins on the fourth at
  /// size 3768 having spent 300,000.  A 300,000 ceiling was tried and loses
  /// lift3 outright -- it times out with eight probes skipped.
  ///
  /// This is also why the ceiling cannot be tightened to fix G2s.
  /// round_robin_arbiter4 probes four times at k=2, concludes nothing, and
  /// costs 6.23% extra cycles against a 6% per-target ceiling.  It and lift3
  /// take the same number of probes at the same per-run cap, so no cumulative
  /// budget separates them: any cap that admits lift3's fourth probe admits
  /// round_robin_arbiter4's fourth.  The probe is therefore opt-in, and
  /// acacia_local_certificate stays default-off in meson.options.
  ///
  /// The schedule cannot be pruned instead.  Each size mark is the one that
  /// pays for some winner: robot_grid2_2 wins on the first-loop probe at
  /// region size 1, finding_nemo_1 at size 29, finding_nemo_2 at 389, lift3 at
  /// 3768.  Dropping any tier drops an answer.
  inline std::size_t configured_local_certificate_generator_budget () {
    static const std::size_t value =
        acacia::diagnostics::env_size ("ACACIA_LOCAL_CERTIFICATE_BUDGET", 32, true);
    return value;
  }

  inline unsigned long long configured_local_certificate_node_budget () {
    static const unsigned long long value =
        acacia::diagnostics::env_size ("ACACIA_LOCAL_CERTIFICATE_NODES", 200000, true);
    return value;
  }

  inline unsigned long long configured_local_certificate_forward_application_budget () {
    static const unsigned long long value = acacia::diagnostics::env_size (
        "ACACIA_LOCAL_CERTIFICATE_FORWARD_APPS",
        default_local_certificate_forward_application_budget, true);
    return value;
  }

  inline unsigned long long
  configured_local_certificate_cumulative_forward_application_budget () {
    static const unsigned long long value = acacia::diagnostics::env_size (
        "ACACIA_LOCAL_CERTIFICATE_CUMULATIVE_FORWARD_APPS", 400000, true);
    return value;
  }

}  // namespace acacia::solver_detail
#endif

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
#if ACACIA_SYMMETRY_PROFILE
      struct classic_profile_reporter {
          ~classic_profile_reporter () {
            acacia::solver_detail::symmetric::profile::global ().report ();
          }
      };
      acacia::solver_detail::symmetric::profile::global ().reset ();
      classic_profile_reporter profile_reporter;
#endif
      ACACIA_SYMMETRY_PROFILE_SCOPE (classic_solve_total);

#if ACACIA_ENABLE_DIAGNOSTICS
      acacia::antichain_snapshot::configure (aut);
#endif

      VECTOR_ELT_T k = kfrom;

      // Precompute the input and output actions.
      auto inputs_to_ios = get_inputs_to_ios ();
      // ^ ios_precomputers::detail::standard_container<shared_ptr<spot::twa_graph>,
      // vector<pair<int, int>>>
      verb_do (1, vout << "Make actions..." << std::endl);
      auto actioner = actioner_maker.make (aut, inputs_to_ios, k);
      verb_do (1, vout << "Fetching IO actions" << std::endl);
      auto input_output_fwd_actions = actioner.actions ();
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

      auto input_picker = input_picker_maker.make (input_output_fwd_actions, actioner);
      int loopcount = 0;
#if ACACIA_LOCAL_CERTIFICATE
      local_probe_schedule.restart ();
      local_probe_bound_forward_applications = 0;
#endif
      reset_bound_evidence (f.size ());
      acacia::diagnostics::snapshot ("after-action-construction");

      do {
        ++bound_loops;
        bound_peak_frontier = std::max (bound_peak_frontier, f.size ());
        loopcount++;
        acacia::diagnostics::observe_loop (f.size (), k);
#if ACACIA_ENABLE_DIAGNOSTICS
        acacia::antichain_snapshot::observe (f, k, loopcount);
#endif
        verb_do (1, vout << "Loop# " << loopcount << ", f of size " << f.size () << std::endl);

#if ACACIA_LOCAL_CERTIFICATE
        if (local_probe_schedule.observe (f.size ())) {
          // Per bound, not per run, and not for the whole solve: a per-run
          // budget bounds one search but not how many times a worker re-runs a
          // search that cannot succeed.  round_robin_arbiter4 probed six times
          // at a single bound, spent the full per-run budget each time and
          // concluded nothing.  lift3 also probes six times, but across bounds,
          // and wins -- so the counter resets in raise_bound_or_give_up, where
          // a new bound makes the old evidence irrelevant.  The ceiling itself
          // is documented with the budget defaults above.
          if (local_probe_bound_forward_applications >=
              acacia::solver_detail::
                  configured_local_certificate_cumulative_forward_application_budget ()) {
            acacia::diagnostics::set_local_probe_skipped_over_budget ();
            acacia::diagnostics::trace_local_probe (
                (int) k, loopcount, f.size (), "skipped-over-budget", 0, 0);
          }
          else {
            auto local = acacia::solver_detail::find_local_certificate (
                f, init, input_output_fwd_actions, actioner, k,
                acacia::solver_detail::configured_local_certificate_generator_budget (),
                acacia::solver_detail::configured_local_certificate_node_budget (),
                acacia::solver_detail::configured_local_certificate_forward_application_budget ());
            local_probe_bound_forward_applications += local.forward_applications;
            if (local.status ==
                acacia::solver_detail::local_certificate_status::win_certificate) {
              acacia::diagnostics::trace_local_probe (
                  (int) k, loopcount, f.size (), "win", local.forward_applications, local.nodes);
              acacia::diagnostics::set_local_probe (
                  "win", local.forward_applications, local.nodes, true, false);
              acacia::diagnostics::set_final_reason ("local-win-certificate");
              return std::make_optional<std::pair<VECTOR_ELT_T, SetOfStates>> (
                  std::make_pair (k, std::move (*local.win)));
            }
            if (local.status == acacia::solver_detail::local_certificate_status::root_refuted) {
              acacia::diagnostics::trace_local_probe ((int) k, loopcount, f.size (),
                                                      "root-refuted", local.forward_applications,
                                                      local.nodes);
              const bool bound_raised = raise_bound_or_give_up (f, k, actioner);
              acacia::diagnostics::set_local_probe (
                  "root-refuted", local.forward_applications, local.nodes, true, bound_raised);
              if (not bound_raised)
                return std::nullopt;
              continue;
            }
            if (local.status ==
                acacia::solver_detail::local_certificate_status::budget_exhausted) {
              acacia::diagnostics::trace_local_probe (
                  (int) k, loopcount, f.size (), "budget-exhausted",
                  local.forward_applications, local.nodes);
              acacia::diagnostics::set_local_probe (
                  "budget-exhausted", local.forward_applications, local.nodes, false, false);
            }
            else {
              acacia::diagnostics::trace_local_probe ((int) k, loopcount, f.size (), "unknown",
                                                      local.forward_applications, local.nodes);
              acacia::diagnostics::set_local_probe (
                  "unknown", local.forward_applications, local.nodes, false, false);
            }
          }
        }
#endif

        auto input = [&] {
          acacia::diagnostics::scoped_fine_timer timer {
              acacia::diagnostics::fine_metric::picker};
          return input_picker (f);
        } ();
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
#if ACACIA_LOCAL_CERTIFICATE
    acacia::solver_detail::local_probe_frontier local_probe_schedule;
    unsigned long long local_probe_bound_forward_applications = 0;
#endif
    std::chrono::steady_clock::time_point bound_started {};
    std::size_t bound_peak_frontier = 0;
    std::size_t bound_loops = 0;

    void reset_bound_evidence (std::size_t initial_frontier) {
      bound_started = std::chrono::steady_clock::now ();
      bound_peak_frontier = initial_frontier;
      bound_loops = 0;
    }

    [[nodiscard]] acacia::k_schedule::loss_evidence bound_loss_evidence () const {
      return {
          std::chrono::duration_cast<std::chrono::milliseconds> (
              std::chrono::steady_clock::now () - bound_started)
              .count (),
          bound_peak_frontier,
          bound_loops,
          true,
      };
    }

    // This is also sound when `f` is the pre-CPre region X.  The post-CPre
    // region Y is a subset of X, and the lift is monotone, so lifting X gives a
    // larger, still-sound warm start for the next bound.
    template <typename Actioner>
    bool raise_bound_or_give_up (SetOfStates& f, VECTOR_ELT_T& k, Actioner& actioner) {
      const auto next_k = acacia::k_schedule::next (
          ACACIA_K_SCHEDULE, static_cast<long long> (k),
          static_cast<long long> (kfrom), static_cast<long long> (kto),
          static_cast<long long> (kinc), bound_loss_evidence ());
      if (not next_k.has_value ()) {
        verb_do (2, vout << "Early exit because the initial state is out\n");
        acacia::diagnostics::set_final_reason ("kmax-initial-out");
        return false;
      }

      return raise_bound_to_or_give_up (f, k, actioner, *next_k);
    }

    template <typename Actioner>
    bool raise_bound_to_or_give_up (SetOfStates& f, VECTOR_ELT_T& k,
                                    Actioner& actioner, long long next_k) {
      const long long current_k = static_cast<long long> (k);
      const long long delta = next_k - current_k;
      assert (delta > 0);
      assert (next_k >= static_cast<long long> (std::numeric_limits<VECTOR_ELT_T>::lowest ()));
      assert (next_k <= static_cast<long long> (std::numeric_limits<VECTOR_ELT_T>::max ()));
      verb_do (1, vout << "Incrementing k from " << (int) k << " to " << next_k
                       << std::endl);
      k = static_cast<VECTOR_ELT_T> (next_k);
      actioner.setK (k);
      acacia::diagnostics::set_k_last_next (static_cast<int> (next_k));
#if ACACIA_LOCAL_CERTIFICATE
      local_probe_schedule.reset_marks ();
      local_probe_bound_forward_applications = 0;
#endif
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
          assert (widened
                  >= static_cast<long long> (std::numeric_limits<VECTOR_ELT_T>::lowest ()));
          assert (widened
                  <= static_cast<long long> (std::numeric_limits<VECTOR_ELT_T>::max ()));
          vec[i] = static_cast<VECTOR_ELT_T> (widened);
        }
        // Other entries are set to 0 by initialization, since they are bool.
        return state (vec);
      });
      verb_do (1, vout << "Done" << std::endl);
      reset_bound_evidence (f.size ());
      return true;
    }

    // This computes f = CPre(f), in the following way:
    // UPre(f) = f \cap f1i
    // f1i = \cup_{o \in O} f1io
    // f1io = PreHat (f, i, o)
    template <typename Action, typename Actioner>
    void cpre_inplace (SetOfStates& f, const Action& io_action, Actioner& actioner,
                       [[maybe_unused]] int k = -1, [[maybe_unused]] int loop = -1) {
      acacia::diagnostics::scoped_fine_timer cpre_timer {
          acacia::diagnostics::fine_metric::cpre};
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
      } ();
#endif
#if CPRE_AVOID_UNIONS == 0
      posets::utils::vector_mm<VECTOR_ELT_T> v (aut->num_states (), -1);
      auto vv = typename SetOfStates::value_type (v);
      SetOfStates f1i (std::move (vv));
      bool first_turn = true;
      {
        ACACIA_SYMMETRY_PROFILE_SCOPE (classic_pre_build);
        for (const auto& action_vec : actions) {
          verb_do (3, vout << "one_output_letter:" << std::endl);

          acacia::diagnostics::observe_action ();
          SetOfStates f1io = [&] {
            acacia::diagnostics::scoped_downset_timer downset_timer;
            return f.apply ([this, &action_vec, &actioner] (const auto& m) {
              ACACIA_SYMMETRY_PROFILE_SCOPE (classic_backward_apply);
              acacia::diagnostics::scoped_fine_timer apply_timer {
                  acacia::diagnostics::fine_metric::apply};
              auto ret = actioner.apply (m, action_vec, actioners::direction::backward);
              verb_do (3, vout << "  " << m << " -> " << ret << std::endl);
              return ret;
            });
          } ();

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
#elif CPRE_AVOID_UNIONS == 1
      // Compute downset once, before intersection

      std::vector<typename SetOfStates::value_type> f1i_vec;
      f1i_vec.reserve (actions.size () * f.size ());
      for (const auto& action_vec : actions) {
        acacia::diagnostics::observe_action ();
        verb_do (3, vout << "one_output_letter:" << std::endl);

        acacia::diagnostics::scoped_downset_timer downset_timer;
        for (const auto& m : f) {
          acacia::diagnostics::scoped_fine_timer apply_timer {
              acacia::diagnostics::fine_metric::apply};
          f1i_vec.push_back (actioner.apply (m, action_vec, actioners::direction::backward));
        }
      }

      SetOfStates f1i (std::move (f1i_vec));
#elif CPRE_AVOID_UNIONS == 2
# error Not implemented yet: Remove unions altogether and have intersect take a list
#endif

      {
        ACACIA_SYMMETRY_PROFILE_SCOPE (classic_intersect);
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
