#include "research/dual_rank_delta.hh"

#include <algorithm>
#include <limits>

namespace acacia::research {
  namespace {

    exclusion_delta_result failure (const operation_budget& budget, delta_stats stats = {}) {
      return {budget.outcome (), {}, stats, false, budget.message ()};
    }

    std::size_t saturated_add (std::size_t left, std::size_t right) {
      return right > std::numeric_limits<std::size_t>::max () - left
                 ? std::numeric_limits<std::size_t>::max ()
                 : left + right;
    }

    bool charged_leq (const rank_vector& left, const rank_vector& right,
                      operation_budget& budget, bool& answer) {
      if (left.size () != right.size ()) {
        budget.invalidate ("rank-vector width mismatch in exclusion delta");
        return false;
      }
      answer = true;
      for (std::size_t i = 0; i < left.size (); ++i) {
        if (not budget.charge ())
          return false;
        if (left[i] > right[i]) {
          answer = false;
          break;
        }
      }
      return true;
    }

    bool charged_contains (const dual_rank_region& region, const rank_vector& point,
                           operation_budget& budget, bool& answer) {
      if (not region.domain ().contains_point (point)) {
        answer = false;
        return budget.poll ();
      }
      if (region.form () == frontier_form::max_included) {
        for (const auto& maximum : region.generators ()) {
          bool below = false;
          if (not charged_leq (point, maximum, budget, below))
            return false;
          if (below) {
            answer = true;
            return true;
          }
        }
        answer = false;
        return true;
      }
      for (const auto& exclusion : region.generators ()) {
        bool excluded = false;
        if (not charged_leq (exclusion, point, budget, excluded))
          return false;
        if (excluded) {
          answer = false;
          return true;
        }
      }
      answer = true;
      return true;
    }

    bool charged_compare (const rank_vector& left, const rank_vector& right,
                          operation_budget& budget, int& answer) {
      if (left.size () != right.size ()) {
        budget.invalidate ("rank-vector width mismatch in exclusion delta");
        return false;
      }
      answer = 0;
      for (std::size_t i = 0; i < left.size (); ++i) {
        if (not budget.charge ())
          return false;
        if (left[i] == right[i])
          continue;
        answer = left[i] < right[i] ? -1 : 1;
        break;
      }
      return true;
    }

    bool compatible_four (const dual_rank_region& a, const dual_rank_region& b,
                          const dual_rank_region& c, const dual_rank_region& d) {
      return a.domain ().compatible (b.domain ()) and a.domain ().compatible (c.domain ()) and
             a.domain ().compatible (d.domain ());
    }

  }  // namespace

  exclusion_delta_result try_exclusion_delta (
      const dual_rank_region& before_positive, const dual_rank_region& after_positive,
      const dual_rank_region& before_min_excluded,
      const dual_rank_region& after_min_excluded, operation_budget& budget) {
    delta_stats stats;
    if (not compatible_four (before_positive, after_positive, before_min_excluded,
                             after_min_excluded)) {
      budget.invalidate ("delta regions have incompatible rank domains");
      return failure (budget);
    }
    if (before_positive.form () != frontier_form::max_included or
        after_positive.form () != frontier_form::max_included or
        before_min_excluded.form () != frontier_form::min_excluded or
        after_min_excluded.form () != frontier_form::min_excluded) {
      budget.invalidate ("delta construction requires both exact frontier orientations");
      return failure (budget);
    }

    std::size_t retained_bytes = before_positive.stats ().accounted_bytes;
    retained_bytes = saturated_add (retained_bytes, after_positive.stats ().accounted_bytes);
    retained_bytes = saturated_add (retained_bytes, before_min_excluded.stats ().accounted_bytes);
    retained_bytes = saturated_add (retained_bytes, after_min_excluded.stats ().accounted_bytes);
    std::size_t retained_generators = before_positive.generators ().size ();
    retained_generators =
        saturated_add (retained_generators, after_positive.generators ().size ());
    retained_generators =
        saturated_add (retained_generators, before_min_excluded.generators ().size ());
    retained_generators =
        saturated_add (retained_generators, after_min_excluded.generators ().size ());
    budget.set_external_workspace (retained_bytes, retained_generators);
    if (not budget.observe_workspace (0, 0))
      return failure (budget);

    // Prove contraction from the positive frontier.  Every after generator
    // must remain in before; downward closure then proves after ⊆ before.
    for (const auto& maximum : after_positive.generators ()) {
      bool contained = false;
      if (not charged_contains (before_positive, maximum, budget, contained))
        return failure (budget, stats);
      if (not contained) {
        budget.invalidate ("delta after-region is not contained in before-region");
        return failure (budget, stats);
      }
    }

    // Canonical region generators are lexicographically sorted, so exact
    // generator survival is a linear merge rather than a quadratic lookup.
    std::size_t before_index = 0;
    std::size_t after_index = 0;
    while (before_index < before_positive.generators ().size () and
           after_index < after_positive.generators ().size ()) {
      int order = 0;
      if (not charged_compare (before_positive.generators ()[before_index],
                               after_positive.generators ()[after_index], budget, order))
        return failure (budget, stats);
      if (order == 0) {
        ++stats.positive_maxima_still_generators;
        ++before_index;
        ++after_index;
      }
      else if (order < 0)
        ++before_index;
      else
        ++after_index;
    }

    // These are deliberately separate metrics even though exact downset
    // contraction forces them to agree: if an old maximum x is contained in
    // after, x <= y for a new maximum y; y is also in before, so maximality of
    // x forces x = y.  Keeping both columns prevents future approximate or
    // non-canonical experiments from conflating the concepts.
    stats.before_maxima_still_contained = stats.positive_maxima_still_generators;

    std::vector<rank_vector> delta;
    delta.reserve (after_min_excluded.generators ().size ());
    for (const auto& exclusion : after_min_excluded.generators ()) {
      bool was_in_before = false;
      if (not charged_contains (before_positive, exclusion, budget, was_in_before))
        return failure (budget, stats);
      if (was_in_before)
        delta.push_back (exclusion);
      if (not budget.observe_workspace (accounted_bytes (delta), delta.size ()))
        return failure (budget, stats);
    }

    // Exact certificate: complement(after) must be the upward closure of the
    // old complement plus the newly excluded generators.  Canonicalization
    // also verifies that no filtered generator was redundant or missing.
    std::vector<rank_vector> reconstructed = before_min_excluded.generators ();
    reconstructed.insert (reconstructed.end (), delta.begin (), delta.end ());
    budget.set_external_workspace (retained_bytes, retained_generators);
    region_result rebuilt = try_make_region (
        before_positive.domain (), frontier_form::min_excluded, std::move (reconstructed), budget,
        accounted_bytes (delta), delta.size ());
    if (not rebuilt)
      return failure (budget, stats);

    budget.set_external_workspace (retained_bytes, retained_generators);
    const std::optional<bool> equal = exact_equal (*rebuilt.region, after_min_excluded, budget);
    if (not equal)
      return failure (budget, stats);
    if (not *equal) {
      budget.invalidate ("delta reconstruction does not equal the after-region");
      return failure (budget, stats);
    }

    budget.set_external_workspace (0, 0);
    return {completion::complete, std::move (delta), stats, true, {}};
  }

}  // namespace acacia::research
