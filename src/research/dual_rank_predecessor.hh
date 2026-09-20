#pragma once

#include "research/dual_rank_region.hh"

#include <optional>
#include <string>
#include <vector>

namespace acacia::research {

  struct predecessor_stats {
      std::uint64_t threshold_preimages = 0;
      std::uint64_t threshold_candidates = 0;
      std::uint64_t join_candidates = 0;
      std::uint64_t actions = 0;
  };

  struct update_result {
      completion status = completion::invalid_input;
      std::optional<dual_rank_region> region;
      predecessor_stats stats;
      bool semantic_changed = false;
      std::string message;

      [[nodiscard]] explicit operator bool () const {
        return status == completion::complete and region.has_value ();
      }
  };

  /// Exact safe-source preimage for one deterministic rank action.  The result
  /// uses the source region's orientation.  In the negative orientation its
  /// stored minima generate action failure, including every safe-box overflow.
  [[nodiscard]] update_result try_action_preimage (const dual_rank_region& source,
                                                   const action_vec& action,
                                                   operation_budget& budget);

  /// One input-conditioned elimination:
  ///   D' = D intersect union_a Pre_a(D).
  /// All action preimages are computed from the same immutable source.  An
  /// empty action list is the mathematical losing-everywhere input.
  [[nodiscard]] update_result try_input_update (const dual_rank_region& source,
                                                const std::vector<action_vec>& actions,
                                                operation_budget& budget);

}  // namespace acacia::research
