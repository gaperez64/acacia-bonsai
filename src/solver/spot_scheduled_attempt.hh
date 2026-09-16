#pragma once

#include "solver/game_backend.hh"
#include "solver/k_schedule.hh"
#include "solver/forward_game_nodes.hh"
#include "solver/spot_letter_oracle.hh"
#include <variant>

namespace acacia::spot_lazy_game {
  struct AttemptMetrics {
      double prep_ms = 0, solve_ms = 0, verify_ms = 0;
      std::size_t nodes = 0, choices = 0, proofs = 0, expansions = 0;
  };
  struct VerifiedWin { std::int32_t k; AttemptMetrics metrics; };
  struct VerifiedLoss { std::int32_t k; AttemptMetrics metrics; };
  // Scalars only: no losing region, proof dependencies or pruning state.
  struct LossHint { std::int32_t k; AttemptMetrics metrics; };
  struct UnknownAttempt {
      std::int32_t k;
      AttemptMetrics metrics;
      spot_letters::Unknown failure;
  };
  struct ResourceLimited { std::int32_t k; AttemptMetrics metrics; };
  using ScheduledAttempt = std::variant<VerifiedWin, VerifiedLoss, LossHint,
                                        UnknownAttempt, ResourceLimited>;

  inline const AttemptMetrics& attempt_metrics (const ScheduledAttempt& attempt) {
    return std::visit ([] (const auto& a) -> const AttemptMetrics& { return a.metrics; }, attempt);
  }
  inline const char* attempt_status (const ScheduledAttempt& attempt) {
    if (std::holds_alternative<VerifiedWin> (attempt)) return "WIN_K";
    if (std::holds_alternative<VerifiedLoss> (attempt)) return "LOSE_K";
    if (std::holds_alternative<LossHint> (attempt)) return "LOSS_HINT";
    if (std::holds_alternative<ResourceLimited> (attempt)) return "RESOURCE_LIMIT";
    return "UNKNOWN";
  }
  inline const char* attempt_evidence (const ScheduledAttempt& attempt) {
    if (std::holds_alternative<VerifiedWin> (attempt)) return "verified-win";
    if (std::holds_alternative<VerifiedLoss> (attempt)) return "verified-loss";
    if (std::holds_alternative<LossHint> (attempt)) return "loss-hint";
    return "none";
  }
  inline spot_letters::Unknown attempt_failure (const ScheduledAttempt& attempt) {
    if (const auto* unknown = std::get_if<UnknownAttempt> (&attempt)) return unknown->failure;
    return std::holds_alternative<ResourceLimited> (attempt)
        ? spot_letters::Unknown::resource_limit : spot_letters::Unknown::none;
  }
  // This conversion accepts only the exact engines' result, never an attempt.
  template<class ExactResult>
  ScheduledAttempt verified_attempt (const ExactResult& result, std::int32_t k) {
    const AttemptMetrics metrics {result.prep_ms, result.solve_ms, result.verify_ms,
        result.nodes.size (), result.choices_created, result.proofs.size (), result.expansions};
    switch (result.status) {
      case solver_detail::forward_result_status::win_k: return VerifiedWin {k, metrics};
      case solver_detail::forward_result_status::lose_k: return VerifiedLoss {k, metrics};
      case solver_detail::forward_result_status::resource_limit: return ResourceLimited {k, metrics};
      default: return UnknownAttempt {k, metrics, result.failure};
    }
  }
  inline k_schedule::loss_evidence scheduling_evidence (const ScheduledAttempt& attempt) {
    const auto& metrics = attempt_metrics (attempt);
    return {static_cast<long long> (metrics.solve_ms), metrics.proofs, metrics.expansions,
            std::holds_alternative<VerifiedLoss> (attempt)};
  }
  enum class SchedulingAction { win, advance, exhausted, inconclusive };
  struct SchedulingStep { SchedulingAction action; long long next_k = 0; };
  inline SchedulingStep schedule_attempt (const ScheduledAttempt& attempt, k_schedule::kind schedule,
                                          long long kmin, long long kmax, long long kinc) {
    if (std::holds_alternative<VerifiedWin> (attempt)) return {SchedulingAction::win};
    if (not std::holds_alternative<VerifiedLoss> (attempt) &&
        not std::holds_alternative<LossHint> (attempt)) return {SchedulingAction::inconclusive};
    const auto k = std::visit ([] (const auto& a) { return a.k; }, attempt);
    const auto next = k_schedule::next (schedule, k, kmin, kmax, kinc, scheduling_evidence (attempt));
    // Exhausting scheduling advice is UNKNOWN for either worker polarity.
    return next ? SchedulingStep {SchedulingAction::advance, *next}
                : SchedulingStep {SchedulingAction::exhausted};
  }
} // namespace acacia::spot_lazy_game
