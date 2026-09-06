#pragma once

#include "configuration.hh"
#include "posets/utils/vector_mm.hh"
#include "posets/vectors/traits.hh"
#include "solver/certificate_verifier.hh"
#include "solver/diagnostics.hh"
#include "solver/forward_reachable_safety.hh"
#include "solver/k_schedule.hh"
#include "utils/verbose.hh"

#include <bddx.h>

#include <chrono>
#include <iostream>
#include <optional>
#include <spot/twa/twagraph.hh>
#include <string>
#include <utility>

namespace acacia::solver_detail {

  /// Increasing-K wrapper around the exact reachable fixed-K safety solver.
  ///
  /// Each fixed-K call owns a fresh forward_search, so its graph, intern map,
  /// losing antichain, proof identifiers, successor vectors, and work queues
  /// are all discarded before K changes.  In particular, successors cannot be
  /// reused because the actioner's rank transformer depends on K.
  template <class SetOfStates, class IOsPrecomputationMaker, class ActionerMaker,
            class InputPickerMaker>
  class forward_k_bounded_safety_aut_detail {
      using state = typename SetOfStates::value_type;
      using clock = std::chrono::steady_clock;

    public:
      forward_k_bounded_safety_aut_detail (
          spot::twa_graph_ptr aut, VECTOR_ELT_T kfrom, VECTOR_ELT_T kto,
          VECTOR_ELT_T kinc, bdd input_support, bdd output_support,
          const IOsPrecomputationMaker& ios_precomputer_maker,
          const ActionerMaker& actioner_maker,
          [[maybe_unused]] const InputPickerMaker& input_picker_maker,
          forward_limits limits = {})
        : aut {std::move (aut)},
          kfrom {kfrom},
          kto {kto},
          kinc {kinc},
          input_support {input_support},
          output_support {output_support},
          ios_precomputer_maker {ios_precomputer_maker},
          actioner_maker {actioner_maker},
          limits {limits} {}

      std::optional<std::pair<VECTOR_ELT_T, SetOfStates>> solve () {
        const auto total_started = clock::now ();
        acacia::diagnostics::set_forward_backend ();

        auto finish = [&] (std::string reason, bool set_global_reason = true) {
          acacia::diagnostics::set_forward_final_reason (reason);
          if (set_global_reason)
            acacia::diagnostics::set_final_reason (reason);
          acacia::diagnostics::set_forward_total_ms (
              std::chrono::duration<double, std::milli> (
                  clock::now () - total_started)
                  .count ());
        };

        auto inputs_to_ios =
            (ios_precomputer_maker.make (aut, input_support, output_support)) ();
        auto actioner = actioner_maker.make (aut, inputs_to_ios, kfrom);
        auto& input_output_fwd_actions = actioner.actions ();

        posets::utils::vector_mm<VECTOR_ELT_T> initial_vector (
            aut->num_states (), -1);
        initial_vector[aut->get_init_state_number ()] = 0;
        const state initial {initial_vector};

        VECTOR_ELT_T k = kfrom;
        for (;;) {
          actioner.setK (k);

          auto safe_vector = posets::utils::vector_mm<VECTOR_ELT_T> (
              aut->num_states (), static_cast<VECTOR_ELT_T> (k - 1));
          for (std::size_t i = posets::vectors::bool_threshold;
               i < aut->num_states (); ++i)
            safe_vector[i] = 0;
          const state safe {safe_vector};

          // The local search object is intentionally reconstructed at every K.
          // Both knobs are runtime-configurable so a sweep can measure what
          // they are worth on real instances without a rebuild per arm.  The
          // The shipped lazy path uses the antichain but never consults the
          // minimisation threshold.  The threshold remains runtime-tunable for
          // ACACIA_FORWARD_EAGER_MINIMAL_SUCCESSORS comparison builds.
          static const bool use_antichain =
              acacia::diagnostics::env_size ("ACACIA_FORWARD_LOSING_ANTICHAIN", 1, true) != 0;
          static const std::size_t minimisation_threshold =
              acacia::diagnostics::env_size (
                  "ACACIA_FORWARD_MINIMISATION_THRESHOLD",
                  default_controller_minimisation_threshold, true);
          const auto attempt_started = clock::now ();
          auto result = solve_forward_reachable_safety<SetOfStates> (
              initial, safe, input_output_fwd_actions, actioner, limits,
              use_antichain, minimisation_threshold);
          acacia::diagnostics::set_forward_attempt (
              static_cast<int> (k), result_name (result.status),
              result.status == forward_result_status::resource_limit
                  ? resource_reason (result.resource_limit)
                  : "none",
              result.env_nodes, result.ctrl_nodes, result.env_expanded,
              result.ctrl_expanded, result.losing_antichain_size,
              result.losing_insertions, result.invalidation_scans,
              result.nodes_checked, result.nodes_invalidated,
              result.raw_actions, result.forward_actions_skipped,
              result.forward_covers_created, result.forward_covers_resolved,
              result.forward_cover_search_visits,
              result.distinct_successors, result.minimal_successors,
              result.strategy_ranks.size (), result.rank_bytes,
              result.node_bytes, result.index_bytes, result.total_bytes);

          if (result.status == forward_result_status::resource_limit) {
            const std::string reason = resource_reason (result.resource_limit);
            finish (reason);
            return std::nullopt;
          }

          if (result.status == forward_result_status::lose_k) {
            if (k >= kto) {
              finish ("forward-kmax-lose");
              return std::nullopt;
            }
            const acacia::k_schedule::loss_evidence evidence {
                std::chrono::duration_cast<std::chrono::milliseconds> (
                    clock::now () - attempt_started)
                    .count (),
                result.losing_antichain_peak,
                result.env_expanded + result.ctrl_expanded,
                not result.losing_proofs.empty (),
            };
            const auto next_k = acacia::k_schedule::next (
                ACACIA_K_SCHEDULE, static_cast<long long> (k),
                static_cast<long long> (kfrom), static_cast<long long> (kto),
                static_cast<long long> (kinc), evidence);
            if (not next_k.has_value ()) {
              finish ("forward-kmax-lose");
              return std::nullopt;
            }
            verb_do (1, vout << "Forward solver incrementing k from "
                             << static_cast<int> (k) << " to "
                             << *next_k << std::endl);
            k = static_cast<VECTOR_ELT_T> (*next_k);
            acacia::diagnostics::set_k_last_next (static_cast<int> (*next_k));
            continue;
          }

          SetOfStates candidate {std::move (result.strategy_ranks)};
          const SetOfStates envelope {safe.copy ()};
          const auto verify_started = clock::now ();
          unsigned long long verification_applications = 0;
          bool verification_budget_exhausted = false;
          // A zero budget is the verifier's explicit unbounded mode.
          const bool verified = verify_winning_certificate (
              envelope, candidate, initial, input_output_fwd_actions, actioner,
              &verification_applications, 0, &verification_budget_exhausted);
          const double verify_ms =
              std::chrono::duration<double, std::milli> (
                  clock::now () - verify_started)
                  .count ();
          acacia::diagnostics::set_forward_certificate_verify_ms (verify_ms);

          if (not verified or verification_budget_exhausted) {
            fallback_to_backward = true;
            const std::string reason =
                verification_budget_exhausted
                    ? "forward-certificate-unbounded-budget-exhausted"
                    : "forward-certificate-verification-failed";
            std::cerr << reason << " at K=" << static_cast<int> (k)
                      << "; falling back to the backward solver\n";
            finish (reason);
            return std::nullopt;
          }

          finish ("forward-verified-win");
          return std::make_optional<std::pair<VECTOR_ELT_T, SetOfStates>> (
              std::make_pair (k, std::move (candidate)));
        }
      }

      [[nodiscard]] bool should_fallback_to_backward () const {
        return fallback_to_backward;
      }

      forward_k_bounded_safety_aut_detail (
          forward_k_bounded_safety_aut_detail&&) = delete;
      forward_k_bounded_safety_aut_detail& operator= (
          forward_k_bounded_safety_aut_detail&&) = delete;

    private:
      static const char* result_name (forward_result_status status) {
        switch (status) {
          case forward_result_status::win_k: return "WIN_K";
          case forward_result_status::lose_k: return "LOSE_K";
          case forward_result_status::resource_limit: return "RESOURCE_LIMIT";
        }
        return "UNKNOWN";
      }

      static const char* resource_reason (forward_resource_limit reason) {
        switch (reason) {
          case forward_resource_limit::env_nodes:
            return "forward-resource-limit-env-nodes";
          case forward_resource_limit::ctrl_nodes:
            return "forward-resource-limit-ctrl-nodes";
          case forward_resource_limit::edges:
            return "forward-resource-limit-edges";
          case forward_resource_limit::rank_bytes:
            return "forward-resource-limit-rank-bytes";
          case forward_resource_limit::total_bytes:
            return "forward-resource-limit-total-bytes";
          case forward_resource_limit::none:
            return "forward-resource-limit-unspecified";
        }
        return "forward-resource-limit-unspecified";
      }

      spot::twa_graph_ptr aut;
      const VECTOR_ELT_T kfrom, kto, kinc;
      bdd input_support, output_support;
      const IOsPrecomputationMaker& ios_precomputer_maker;
      const ActionerMaker& actioner_maker;
      const forward_limits limits;
      bool fallback_to_backward = false;
  };

}  // namespace acacia::solver_detail
