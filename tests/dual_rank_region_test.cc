#include "research/dual_rank_region.hh"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <iostream>
#include <optional>
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

  void enumerate_points (const rank_domain& domain, std::size_t coordinate, rank_vector& current,
                         std::vector<rank_vector>& out) {
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

  std::vector<rank_vector> maximal_included (const std::vector<bool>& included,
                                             const std::vector<rank_vector>& all) {
    std::vector<rank_vector> result;
    for (std::size_t i = 0; i < all.size (); ++i) {
      if (not included[i])
        continue;
      bool maximal = true;
      for (std::size_t j = 0; j < all.size (); ++j)
        if (i != j and included[j] and leq (all[i], all[j])) {
          maximal = false;
          break;
        }
      if (maximal)
        result.push_back (all[i]);
    }
    return result;
  }

  std::vector<rank_vector> minimal_excluded (const std::vector<bool>& included,
                                             const std::vector<rank_vector>& all) {
    std::vector<rank_vector> result;
    for (std::size_t i = 0; i < all.size (); ++i) {
      if (included[i])
        continue;
      bool minimal = true;
      for (std::size_t j = 0; j < all.size (); ++j)
        if (i != j and not included[j] and leq (all[j], all[i])) {
          minimal = false;
          break;
        }
      if (minimal)
        result.push_back (all[i]);
    }
    return result;
  }

  struct explicit_region {
      std::vector<bool> members;
      dual_rank_region positive;
      dual_rank_region negative;
  };

  void check_exhaustive_algebra () {
    const rank_domain domain = rank_domain::fixed_box (2, 2, 1, "mixed-small");
    const auto all = points (domain);
    std::vector<explicit_region> regions;
    for (std::uint64_t mask = 0; mask < (1ULL << all.size ()); ++mask) {
      std::vector<bool> included (all.size (), false);
      for (std::size_t i = 0; i < all.size (); ++i)
        included[i] = ((mask >> i) & 1U) != 0;
      if (not is_downset (included, all))
        continue;
      auto positive =
          make_region (domain, frontier_form::max_included, maximal_included (included, all));
      auto negative =
          make_region (domain, frontier_form::min_excluded, minimal_excluded (included, all));
      for (std::size_t i = 0; i < all.size (); ++i) {
        expect ("positive explicit membership", positive.contains (all[i]) == included[i]);
        expect ("negative explicit membership", negative.contains (all[i]) == included[i]);
      }
      rank_vector outside = domain.top ();
      outside[0] = static_cast<VECTOR_ELT_T> (domain.upper[0] + 1);
      expect ("positive rejects overflow", not positive.contains (outside));
      expect ("negative rejects overflow", not negative.contains (outside));

      operation_budget to_negative {unlimited_limits ()};
      region_result converted = try_convert (positive, frontier_form::min_excluded, to_negative);
      expect ("A to B completes", static_cast<bool> (converted));
      operation_budget equal_budget {unlimited_limits ()};
      expect ("A to B exact", exact_equal (*converted.region, negative, equal_budget) == true);

      operation_budget to_positive {unlimited_limits ()};
      region_result roundtrip = try_convert (negative, frontier_form::max_included, to_positive);
      expect ("B to A completes", static_cast<bool> (roundtrip));
      operation_budget roundtrip_equal {unlimited_limits ()};
      expect ("B to A exact", exact_equal (*roundtrip.region, positive, roundtrip_equal) == true);
      regions.push_back ({std::move (included), std::move (positive), std::move (negative)});
    }

    expect ("mixed box has several downsets", regions.size () > 5);
    for (const auto& left : regions)
      for (const auto& right : regions) {
        for (const frontier_form form :
             {frontier_form::max_included, frontier_form::min_excluded}) {
          const dual_rank_region& l =
              form == frontier_form::max_included ? left.positive : left.negative;
          const dual_rank_region& r =
              form == frontier_form::max_included ? right.positive : right.negative;
          operation_budget union_budget {unlimited_limits ()};
          region_result united = try_union (l, r, form, union_budget);
          operation_budget intersection_budget {unlimited_limits ()};
          region_result intersected = try_intersection (l, r, form, intersection_budget);
          expect ("union completes", static_cast<bool> (united));
          expect ("intersection completes", static_cast<bool> (intersected));
          for (std::size_t i = 0; i < all.size (); ++i) {
            expect ("union explicit semantics",
                    united.region->contains (all[i]) == (left.members[i] or right.members[i]));
            expect ("intersection explicit semantics", intersected.region->contains (all[i]) ==
                                                           (left.members[i] and right.members[i]));
          }
        }
      }
  }

  void check_edges_and_budgets () {
    const rank_domain zero = rank_domain::fixed_box (0, 1, 0, "zero-product");
    const rank_vector point;
    auto zero_full = make_region (zero, frontier_form::max_included, {point});
    auto zero_full_negative = make_region (zero, frontier_form::min_excluded, {});
    expect ("zero-dimensional product membership",
            zero_full.contains (point) and zero_full_negative.contains (point));

    const rank_domain wide = rank_domain::fixed_box (1, 127, 1, "k127");
    auto top = make_region (wide, frontier_form::max_included,
                            {wide.top (), wide.bottom (), wide.top ()});
    expect ("duplicate and dominated maxima normalize", top.generators ().size () == 1);

    const rank_domain other = rank_domain::fixed_box (1, 127, 1, "different-automaton");
    auto other_top = make_region (other, frontier_form::max_included, {other.top ()});
    operation_budget incompatible {unlimited_limits ()};
    expect ("incompatible domain rejected",
            not exact_equal (top, other_top, incompatible).has_value () and
                incompatible.outcome () == completion::invalid_input);

    const std::uint64_t before_hash = top.semantic_hash ();
    budget_limits tiny = unlimited_limits ();
    tiny.max_work = 0;
    operation_budget stopped {tiny};
    region_result failed = try_convert (top, frontier_form::min_excluded, stopped);
    expect ("conversion abort is typed", failed.status == completion::work_limit);
    expect ("conversion abort leaves source unchanged", top.semantic_hash () == before_hash);

    budget_limits retained_limit = unlimited_limits ();
    retained_limit.max_workspace_bytes = 100;
    operation_budget retained_budget {retained_limit};
    retained_budget.set_external_workspace (80, 2);
    expect ("retained caller storage participates in workspace budget",
            not retained_budget.observe_workspace (30, 1) and
                retained_budget.outcome () == completion::memory_limit);

    const rank_domain hole_domain = rank_domain::fixed_box (2, 1, 0, "hole-check");
    auto only_bottom =
        make_region (hole_domain, frontier_form::max_included, {hole_domain.bottom ()});
    auto wrong_negative =
        make_region (hole_domain, frontier_form::min_excluded,
                     {rank_vector {0, 0}});  // Disjoint closures, but {-1,0}/{0,-1} are holes.
    operation_budget hole_budget {unlimited_limits ()};
    expect ("exact cross-form validation detects a coverage hole",
            exact_equal (only_bottom, wrong_negative, hole_budget) == false);
  }

  void check_independent_pairs () {
    constexpr std::size_t pairs = 5;
    const rank_domain domain = rank_domain::fixed_box (2 * pairs, 1, 0, "independent-pairs");
    std::vector<rank_vector> exclusions;
    for (std::size_t pair = 0; pair < pairs; ++pair) {
      rank_vector exclusion = domain.bottom ();
      exclusion[2 * pair] = 0;
      exclusion[2 * pair + 1] = 0;
      exclusions.push_back (std::move (exclusion));
    }
    auto negative = make_region (domain, frontier_form::min_excluded, std::move (exclusions));
    operation_budget conversion {unlimited_limits ()};
    region_result positive = try_convert (negative, frontier_form::max_included, conversion);
    expect ("independent-pair expansion completes", static_cast<bool> (positive));
    expect ("independent-pair expansion has 2^m maxima",
            positive.region->generators ().size () == (1U << pairs));

    budget_limits capped = unlimited_limits ();
    capped.max_frontier = 12;
    operation_budget aborting {capped};
    region_result stopped = try_convert (negative, frontier_form::max_included, aborting);
    expect ("expanding conversion respects frontier budget",
            stopped.status == completion::frontier_limit);
  }

  void check_cardinality_threshold () {
    constexpr std::size_t dimensions = 7;
    constexpr std::size_t allowed = 3;
    const rank_domain domain = rank_domain::fixed_box (dimensions, 1, 0, "cardinality-threshold");
    std::vector<rank_vector> maxima;
    std::vector<rank_vector> exclusions;
    for (std::size_t mask = 0; mask < (1U << dimensions); ++mask) {
      const unsigned present = std::popcount (static_cast<unsigned> (mask));
      if (present != allowed and present != allowed + 1)
        continue;
      rank_vector point = domain.bottom ();
      for (std::size_t coordinate = 0; coordinate < dimensions; ++coordinate)
        if (((mask >> coordinate) & 1U) != 0)
          point[coordinate] = 0;
      (present == allowed ? maxima : exclusions).push_back (std::move (point));
    }
    auto positive = make_region (domain, frontier_form::max_included, std::move (maxima));
    auto negative = make_region (domain, frontier_form::min_excluded, std::move (exclusions));
    expect ("cardinality threshold has equally large frontiers",
            positive.generators ().size () == 35 and negative.generators ().size () == 35);
    operation_budget budget {unlimited_limits ()};
    expect ("cardinality threshold frontiers are exact duals",
            exact_equal (positive, negative, budget) == true);
  }
}

int main () {
  check_exhaustive_algebra ();
  check_edges_and_budgets ();
  check_independent_pairs ();
  check_cardinality_threshold ();
  return failures == 0 ? 0 : 1;
}
