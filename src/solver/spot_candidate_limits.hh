#pragma once

#include "solver/diagnostics.hh"
#include "solver/spot_guarded_forward_safety.hh"

namespace acacia {
  // Research caps, independent of the compiled dispatch gate. Zero is useful
  // for deterministic UNKNOWN/fallback attribution tests.
  inline spot_guarded::Limits spot_candidate_limits () {
    spot_guarded::Limits limits;
    limits.max_expansions = diagnostics::env_size (
        "ACACIA_SPOT_MAX_EXPANSIONS", limits.max_expansions, true);
    limits.max_rank_nodes = diagnostics::env_size (
        "ACACIA_SPOT_MAX_RANK_NODES", limits.max_rank_nodes, true);
    limits.rows.max_rows = diagnostics::env_size (
        "ACACIA_SPOT_MAX_ROWS", limits.rows.max_rows, true);
    limits.queries.max_steps = diagnostics::env_size (
        "ACACIA_SPOT_MAX_QUERY_STEPS", limits.queries.max_steps, true);
    limits.verifier_rows = limits.rows;
    limits.verifier_queries = limits.queries;
    return limits;
  }
} // namespace acacia
