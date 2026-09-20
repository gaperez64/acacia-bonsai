#pragma once

/// Exact newly-excluded boundary of one contracting rank-region update.

#include "research/dual_rank_region.hh"

#include <string>
#include <vector>

namespace acacia::research {

  struct delta_stats {
      std::size_t positive_maxima_still_generators = 0;
      std::size_t before_maxima_still_contained = 0;
  };

  struct exclusion_delta_result {
      completion status = completion::invalid_input;
      std::vector<rank_vector> generators;
      delta_stats stats;
      bool exact = false;
      std::string message;

      [[nodiscard]] explicit operator bool () const {
        return status == completion::complete and exact;
      }
  };

  /// Construct the minimal B_delta for a contracting update
  ///
  ///   after = before ∖ ↑B_delta.
  ///
  /// Both positive and complete-complement forms are supplied deliberately:
  /// callers must account conversion separately, and this operation never
  /// hides an implicit dualization.  Success certifies the equation exactly by
  /// rebuilding Min(P ∖ after) from Min(P ∖ before) ∪ B_delta.
  [[nodiscard]] exclusion_delta_result try_exclusion_delta (
      const dual_rank_region& before_positive, const dual_rank_region& after_positive,
      const dual_rank_region& before_min_excluded,
      const dual_rank_region& after_min_excluded, operation_budget& budget);

}  // namespace acacia::research
