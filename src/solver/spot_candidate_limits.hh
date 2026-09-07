#pragma once

#include "configuration.hh"
#include "solver/diagnostics.hh"
#include "solver/spot_guarded_forward_safety.hh"

namespace acacia {
  // Research caps, independent of the compiled dispatch gate. Zero is useful
  // for deterministic UNKNOWN/fallback attribution tests.
  inline spot_guarded::Limits spot_candidate_limits (
      std::size_t default_max_rank_nodes = spot_guarded::Limits {}.max_rank_nodes) {
    spot_guarded::Limits limits;
    limits.max_expansions = diagnostics::env_size (
        "ACACIA_SPOT_MAX_EXPANSIONS", limits.max_expansions, true);
    limits.max_rank_nodes = diagnostics::env_size (
        "ACACIA_SPOT_MAX_RANK_NODES", default_max_rank_nodes, true);
    limits.rows.max_rows = diagnostics::env_size (
        "ACACIA_SPOT_MAX_ROWS", limits.rows.max_rows, true);
    limits.queries.max_steps = diagnostics::env_size (
        "ACACIA_SPOT_MAX_QUERY_STEPS", limits.queries.max_steps, true);
    limits.verifier_rows = limits.rows;
    limits.verifier_queries = limits.queries;
    return limits;
  }

  inline spot_guarded::Limits spot_taa_candidate_limits () {
    auto limits = spot_candidate_limits (ACACIA_SPOT_TAA_MAX_RANK_NODES);
    // Preserve the common research override; a provider-specific override has
    // final precedence and never lowers the frozen guarded worker's budget.
    limits.max_rank_nodes = diagnostics::env_size (
        "ACACIA_SPOT_TAA_MAX_RANK_NODES", limits.max_rank_nodes, true);
    return limits;
  }
} // namespace acacia
