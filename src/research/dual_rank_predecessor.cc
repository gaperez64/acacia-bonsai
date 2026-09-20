#include "research/dual_rank_predecessor.hh"

#include <algorithm>
#include <limits>

namespace acacia::research {
  namespace {

    update_result failure (const operation_budget& budget, predecessor_stats stats = {}) {
      return {budget.outcome (), std::nullopt, stats, false, budget.message ()};
    }

    bool charged_leq (const rank_vector& left, const rank_vector& right, operation_budget& budget,
                      bool& answer) {
      if (left.size () != right.size ()) {
        budget.invalidate ("rank-vector width mismatch in predecessor");
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

    std::size_t saturating_add (std::size_t left, std::size_t right) {
      return right > std::numeric_limits<std::size_t>::max () - left
                 ? std::numeric_limits<std::size_t>::max ()
                 : left + right;
    }

    bool observe (const dual_rank_region& source, const std::vector<rank_vector>& first,
                  operation_budget& budget, const std::vector<rank_vector>* second = nullptr) {
      std::size_t bytes = source.stats ().accounted_bytes;
      std::size_t live = source.generators ().size ();
      bytes = saturating_add (bytes, accounted_bytes (first));
      live = saturating_add (live, first.size ());
      if (second != nullptr) {
        bytes = saturating_add (bytes, accounted_bytes (*second));
        live = saturating_add (live, second->size ());
      }
      return budget.observe_workspace (bytes, live);
    }

    bool insert_minimal (const dual_rank_region& source, std::vector<rank_vector>& frontier,
                         rank_vector candidate, operation_budget& budget,
                         const std::vector<rank_vector>* other = nullptr) {
      for (const auto& stored : frontier) {
        bool subsumed = false;
        if (not charged_leq (stored, candidate, budget, subsumed))
          return false;
        if (subsumed)
          return true;
      }
      std::size_t write = 0;
      for (std::size_t read = 0; read < frontier.size (); ++read) {
        bool remove = false;
        if (not charged_leq (candidate, frontier[read], budget, remove))
          return false;
        if (remove)
          continue;
        if (write != read)
          frontier[write] = std::move (frontier[read]);
        ++write;
      }
      frontier.erase (frontier.begin () + static_cast<std::ptrdiff_t> (write), frontier.end ());
      frontier.push_back (std::move (candidate));
      return observe (source, frontier, budget, other);
    }

    bool valid_action (const rank_domain& domain, const action_vec& action,
                       operation_budget& budget) {
      if (action.size () != domain.dimensions ()) {
        budget.invalidate ("action width disagrees with rank domain");
        return false;
      }
      for (const auto& row : action)
        for (const auto& [source, increment] : row) {
          (void) increment;
          if (source >= domain.dimensions ()) {
            budget.invalidate ("action source index is outside rank domain");
            return false;
          }
        }
      return true;
    }

    /// The threshold predicate tau(x)[destination] >= threshold is a
    /// disjunction of one-source constraints.  Absence never increments into
    /// presence, hence max(0, threshold-epsilon), not max(-1, ...).
    std::optional<std::vector<rank_vector>> threshold_preimage (
        const dual_rank_region& source, const action_vec& action, std::size_t destination,
        int threshold, operation_budget& budget, predecessor_stats& stats) {
      ++stats.threshold_preimages;
      const rank_domain& domain = source.domain ();
      if (threshold <= -1)
        return std::vector<rank_vector> {domain.bottom ()};
      if (threshold > domain.k)
        return std::vector<rank_vector> {};

      std::vector<rank_vector> result;
      for (const auto& [rank_source, increment] : action[destination]) {
        if (not budget.charge ())
          return std::nullopt;
        const int required = std::max (0, threshold - (increment ? 1 : 0));
        if (required > static_cast<int> (domain.upper[rank_source]))
          continue;
        rank_vector candidate = domain.bottom ();
        candidate[rank_source] = static_cast<VECTOR_ELT_T> (required);
        ++stats.threshold_candidates;
        if (not insert_minimal (source, result, std::move (candidate), budget))
          return std::nullopt;
      }
      return result;
    }

    /// Intersect two upsets by streamed joins of their minimal generators.
    std::optional<std::vector<rank_vector>> intersect_upsets (
        const dual_rank_region& source, const std::vector<rank_vector>& left,
        const std::vector<rank_vector>& right, operation_budget& budget,
        predecessor_stats& stats) {
      std::vector<rank_vector> result;
      for (const auto& l : left)
        for (const auto& r : right) {
          rank_vector joined (l);
          for (std::size_t i = 0; i < joined.size (); ++i) {
            if (not budget.charge ())
              return std::nullopt;
            joined[i] = std::max (joined[i], r[i]);
          }
          ++stats.join_candidates;
          if (not insert_minimal (source, result, std::move (joined), budget, &left))
            return std::nullopt;
        }
      return result;
    }

    std::optional<std::vector<rank_vector>> negative_action_failure (
        const dual_rank_region& source, const action_vec& action, operation_budget& budget,
        predecessor_stats& stats) {
      const rank_domain& domain = source.domain ();
      std::vector<rank_vector> failure;

      // Unsafe preimage is mandatory even when B is empty: safe sources may
      // leave the box without crossing an explicitly stored exclusion cone.
      for (std::size_t destination = 0; destination < domain.dimensions (); ++destination) {
        auto overflow =
            threshold_preimage (source, action, destination,
                                static_cast<int> (domain.upper[destination]) + 1, budget, stats);
        if (not overflow)
          return std::nullopt;
        for (auto& generator : *overflow)
          if (not insert_minimal (source, failure, std::move (generator), budget))
            return std::nullopt;
      }

      for (const auto& exclusion : source.generators ()) {
        // A generator's preimage is the intersection of its constrained
        // destination-threshold predicates.
        std::vector<std::vector<rank_vector>> thresholds;
        for (std::size_t destination = 0; destination < domain.dimensions (); ++destination)
          if (exclusion[destination] >= 0) {
            auto threshold =
                threshold_preimage (source, action, destination,
                                    static_cast<int> (exclusion[destination]), budget, stats);
            if (not threshold)
              return std::nullopt;
            thresholds.push_back (std::move (*threshold));
          }
        std::stable_sort (
            thresholds.begin (), thresholds.end (),
            [] (const auto& left, const auto& right) { return left.size () < right.size (); });

        std::vector<rank_vector> preimage {domain.bottom ()};
        for (const auto& threshold : thresholds) {
          auto next = intersect_upsets (source, preimage, threshold, budget, stats);
          if (not next)
            return std::nullopt;
          preimage = std::move (*next);
          if (preimage.empty ())
            break;
        }
        for (auto& generator : preimage)
          if (not insert_minimal (source, failure, std::move (generator), budget))
            return std::nullopt;
      }
      return failure;
    }

    bool same_native (const dual_rank_region& left, const dual_rank_region& right,
                      operation_budget& budget) {
      const auto& lg = left.generators ();
      const auto& rg = right.generators ();
      if (left.form () != right.form () or lg.size () != rg.size ())
        return false;
      for (std::size_t i = 0; i < lg.size (); ++i) {
        if (not budget.charge (lg[i].size ()))
          return false;
        if (lg[i] != rg[i])
          return false;
      }
      return true;
    }

  }  // namespace

  update_result try_action_preimage (const dual_rank_region& source, const action_vec& action,
                                     operation_budget& budget) {
    predecessor_stats stats;
    stats.actions = 1;
    if (not valid_action (source.domain (), action, budget))
      return failure (budget, stats);

    const region_stats retained = source.stats ();
    region_result built;
    if (source.form () == frontier_form::max_included) {
      std::vector<rank_vector> maxima;
      maxima.reserve (source.generators ().size ());
      for (const auto& maximum : source.generators ()) {
        if (not budget.charge (source.domain ().dimensions ()))
          return failure (budget, stats);
        maxima.push_back (apply_backward (maximum, action,
                                          static_cast<VECTOR_ELT_T> (source.domain ().k),
                                          source.domain ().bool_threshold));
      }
      built = try_make_region (source.domain (), frontier_form::max_included, std::move (maxima),
                               budget, retained.accounted_bytes, retained.generators);
    }
    else {
      auto minima = negative_action_failure (source, action, budget, stats);
      if (not minima)
        return failure (budget, stats);
      built = try_make_region (source.domain (), frontier_form::min_excluded, std::move (*minima),
                               budget, retained.accounted_bytes, retained.generators);
    }
    if (not built)
      return {built.status, std::nullopt, stats, false, built.message};
    const bool changed = not same_native (source, *built.region, budget);
    if (not budget.ok ())
      return failure (budget, stats);
    return {completion::complete, std::move (built.region), stats, changed, {}};
  }

  update_result try_input_update (const dual_rank_region& source,
                                  const std::vector<action_vec>& actions,
                                  operation_budget& budget) {
    predecessor_stats total;
    std::optional<dual_rank_region> action_union;

    // Each action failure/preimage is computed from the immutable pre-update
    // region.  Controller choice is union of successful preimages, equivalently
    // intersection of failures in the negative orientation.
    for (const auto& action : actions) {
      if (action_union) {
        const region_stats retained = action_union->stats ();
        budget.set_external_workspace (retained.accounted_bytes, retained.generators);
      }
      update_result preimage = try_action_preimage (source, action, budget);
      total.threshold_preimages += preimage.stats.threshold_preimages;
      total.threshold_candidates += preimage.stats.threshold_candidates;
      total.join_candidates += preimage.stats.join_candidates;
      total.actions += preimage.stats.actions;
      if (not preimage)
        return {preimage.status, std::nullopt, total, false, preimage.message};
      if (not action_union) {
        budget.set_external_workspace (0, 0);
        action_union = std::move (*preimage.region);
        continue;
      }
      budget.set_external_workspace (0, 0);
      region_result united = try_union (*action_union, *preimage.region, source.form (), budget);
      if (not united)
        return {united.status, std::nullopt, total, false, united.message};
      action_union = std::move (*united.region);
    }
    budget.set_external_workspace (0, 0);

    if (not action_union) {
      // Empty controller choice: union_a Pre_a(D) is empty.  Keep this distinct
      // from one transition-free action, whose forward image is bottom.
      std::vector<rank_vector> empty_generators;
      if (source.form () == frontier_form::min_excluded)
        empty_generators.push_back (source.domain ().bottom ());
      const region_stats retained = source.stats ();
      region_result empty =
          try_make_region (source.domain (), source.form (), std::move (empty_generators), budget,
                           retained.accounted_bytes, retained.generators);
      if (not empty)
        return {empty.status, std::nullopt, total, false, empty.message};
      action_union = std::move (*empty.region);
    }

    // The input-conditioned elimination is contracting and monotone in D.
    region_result contracted = try_intersection (source, *action_union, source.form (), budget);
    if (not contracted)
      return {contracted.status, std::nullopt, total, false, contracted.message};
    const bool changed = not same_native (source, *contracted.region, budget);
    if (not budget.ok ())
      return failure (budget, total);
    return {completion::complete, std::move (contracted.region), total, changed, {}};
  }

}  // namespace acacia::research
