#include "research/dual_rank_delta.hh"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
  using namespace acacia::research;

  int failures = 0;

  void expect (const std::string& name, bool condition) {
    if (condition)
      return;
    std::cerr << "FAIL: " << name << '\n';
    ++failures;
  }

  budget_limits unlimited_limits () {
    budget_limits limits;
    limits.max_work = std::numeric_limits<std::uint64_t>::max ();
    limits.max_workspace_bytes = std::numeric_limits<std::size_t>::max ();
    limits.max_frontier = std::numeric_limits<std::size_t>::max ();
    limits.deadline = std::chrono::milliseconds {0};
    return limits;
  }

  dual_rank_region make_region (const rank_domain& domain, frontier_form form,
                                std::vector<rank_vector> generators) {
    operation_budget budget {unlimited_limits ()};
    region_result result = try_make_region (domain, form, std::move (generators), budget);
    expect ("region construction", static_cast<bool> (result));
    return std::move (*result.region);
  }

  void enumerate_points (const rank_domain& domain, std::size_t coordinate,
                         rank_vector& current, std::vector<rank_vector>& out) {
    if (coordinate == domain.dimensions ()) {
      out.push_back (current);
      return;
    }
    for (int value = domain.lower[coordinate]; value <= domain.upper[coordinate]; ++value) {
      current[coordinate] = static_cast<VECTOR_ELT_T> (value);
      enumerate_points (domain, coordinate + 1, current, out);
    }
  }

  std::vector<rank_vector> points (const rank_domain& domain) {
    std::vector<rank_vector> result;
    rank_vector current (domain.dimensions (), -1);
    enumerate_points (domain, 0, current, result);
    return result;
  }

  bool is_downset (const std::vector<bool>& included, const std::vector<rank_vector>& all) {
    for (std::size_t i = 0; i < all.size (); ++i)
      if (included[i])
        for (std::size_t j = 0; j < all.size (); ++j)
          if (leq (all[j], all[i]) and not included[j])
            return false;
    return true;
  }

  std::vector<rank_vector> boundary (const std::vector<bool>& included,
                                     const std::vector<rank_vector>& all, bool maximal) {
    std::vector<rank_vector> result;
    for (std::size_t i = 0; i < all.size (); ++i) {
      if (included[i] != maximal)
        continue;
      bool extremal = true;
      for (std::size_t j = 0; j < all.size (); ++j) {
        if (i == j or included[j] != maximal)
          continue;
        const bool dominated = maximal ? leq (all[i], all[j]) : leq (all[j], all[i]);
        if (dominated) {
          extremal = false;
          break;
        }
      }
      if (extremal)
        result.push_back (all[i]);
    }
    return result;
  }

  bool excluded_by_delta (const std::vector<rank_vector>& delta, const rank_vector& point) {
    for (const auto& generator : delta)
      if (leq (generator, point))
        return true;
    return false;
  }

  struct explicit_region {
      std::vector<bool> members;
      dual_rank_region positive;
      dual_rank_region negative;
  };

  void check_every_contracting_pair () {
    // K=1 exercises the -1 absence value in both coordinates and the Boolean
    // tail at coordinate 1.  The finite four-point box permits every downset
    // pair to be checked point by point.
    const rank_domain domain = rank_domain::fixed_box (2, 1, 1, "delta-k1-mixed-tail");
    const auto all = points (domain);
    std::vector<explicit_region> regions;
    for (std::uint64_t mask = 0; mask < (1ULL << all.size ()); ++mask) {
      std::vector<bool> included (all.size (), false);
      for (std::size_t i = 0; i < all.size (); ++i)
        included[i] = ((mask >> i) & 1U) != 0;
      if (not is_downset (included, all))
        continue;
      regions.push_back ({included,
                          make_region (domain, frontier_form::max_included,
                                       boundary (included, all, true)),
                          make_region (domain, frontier_form::min_excluded,
                                       boundary (included, all, false))});
    }

    std::size_t pairs = 0;
    for (const auto& before : regions)
      for (const auto& after : regions) {
        bool subset = true;
        for (std::size_t i = 0; i < all.size (); ++i)
          subset = subset and (not after.members[i] or before.members[i]);
        if (not subset)
          continue;
        ++pairs;
        operation_budget budget {unlimited_limits ()};
        exclusion_delta_result delta =
            try_exclusion_delta (before.positive, after.positive, before.negative,
                                 after.negative, budget);
        expect ("every contracting pair completes", static_cast<bool> (delta));
        if (not delta)
          continue;
        for (std::size_t i = 0; i < all.size (); ++i)
          expect ("delta reconstructs every point",
                  after.positive.contains (all[i]) ==
                      (before.positive.contains (all[i]) and
                       not excluded_by_delta (delta.generators, all[i])));
      }
    expect ("exhaustive check covers many pairs", pairs >= 20);
  }

  void check_named_boundaries_and_failures () {
    const rank_domain domain = rank_domain::fixed_box (2, 1, 1, "delta-named");
    auto full = make_region (domain, frontier_form::max_included, {rank_vector {0, 0}});
    auto full_negative = make_region (domain, frontier_form::min_excluded, {});
    auto shaved = make_region (domain, frontier_form::max_included,
                               {rank_vector {-1, 0}, rank_vector {0, -1}});
    auto shaved_negative =
        make_region (domain, frontier_form::min_excluded, {rank_vector {0, 0}});

    operation_budget empty_budget {unlimited_limits ()};
    exclusion_delta_result empty =
        try_exclusion_delta (full, full, full_negative, full_negative, empty_budget);
    expect ("empty delta", empty and empty.generators.empty ());
    expect ("unchanged maximum survives",
            empty.stats.positive_maxima_still_generators == 1 and
                empty.stats.before_maxima_still_contained == 1);

    operation_budget shaved_budget {unlimited_limits ()};
    exclusion_delta_result one =
        try_exclusion_delta (full, shaved, full_negative, shaved_negative, shaved_budget);
    expect ("one removed maximum has one new exclusion",
            one and one.generators == std::vector<rank_vector> {{0, 0}});
    expect ("incomparable replacement maxima are not old-generator survival",
            one.stats.positive_maxima_still_generators == 0 and
                one.stats.before_maxima_still_contained == 0);

    const rank_domain line = rank_domain::fixed_box (1, 2, 1, "delta-replaced-complement");
    auto line_before =
        make_region (line, frontier_form::max_included, {rank_vector {0}});
    auto line_before_negative =
        make_region (line, frontier_form::min_excluded, {rank_vector {1}});
    auto line_after =
        make_region (line, frontier_form::max_included, {rank_vector {-1}});
    auto line_after_negative =
        make_region (line, frontier_form::min_excluded, {rank_vector {0}});
    operation_budget replaced_budget {unlimited_limits ()};
    exclusion_delta_result replaced =
        try_exclusion_delta (line_before, line_after, line_before_negative,
                             line_after_negative, replaced_budget);
    expect ("new generator replaces a redundant old complement generator",
            replaced and replaced.generators == std::vector<rank_vector> {{0}});

    operation_budget expansion_budget {unlimited_limits ()};
    exclusion_delta_result expansion =
        try_exclusion_delta (shaved, full, shaved_negative, full_negative, expansion_budget);
    expect ("non-contracting update rejected",
            not expansion and expansion.status == completion::invalid_input);

    budget_limits capped = unlimited_limits ();
    capped.max_work = 0;
    operation_budget capped_budget {capped};
    exclusion_delta_result stopped =
        try_exclusion_delta (full, shaved, full_negative, shaved_negative, capped_budget);
    expect ("delta construction is transactionally budgeted",
            not stopped and stopped.status == completion::work_limit and
                stopped.generators.empty ());
  }
}

int main () {
  check_every_contracting_pair ();
  check_named_boundaries_and_failures ();
  return failures == 0 ? 0 : 1;
}
