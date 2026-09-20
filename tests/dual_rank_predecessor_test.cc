#include "research/dual_rank_predecessor.hh"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
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
    expect ("construct test region", static_cast<bool> (result));
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

  bool direct_input_member (const dual_rank_region& source, const rank_vector& point,
                            const std::vector<action_vec>& actions) {
    if (not source.contains (point))
      return false;
    for (const auto& action : actions)
      if (source.contains (
              apply_forward (point, action, static_cast<VECTOR_ELT_T> (source.domain ().k))))
        return true;
    return false;
  }

  void check_overflow_and_empty_actions () {
    const rank_domain domain = rank_domain::fixed_box (1, 1, 1, "k1-overflow");
    auto whole_negative = make_region (domain, frontier_form::min_excluded, {});
    action_vec accepting (1);
    accepting[0].push_back ({0, true});
    operation_budget budget {unlimited_limits ()};
    update_result preimage = try_action_preimage (whole_negative, accepting, budget);
    expect ("K=1 accepting preimage completes", static_cast<bool> (preimage));
    expect ("absent stays safe", preimage.region->contains (rank_vector {-1}));
    expect ("present overflows", not preimage.region->contains (rank_vector {0}));
    expect ("overflow frontier is {0}", preimage.region->generators ().size () == 1 and
                                            preimage.region->generators ()[0] == rank_vector {0});

    operation_budget no_actions_budget {unlimited_limits ()};
    update_result no_actions = try_input_update (whole_negative, {}, no_actions_budget);
    expect ("empty action list loses everywhere", static_cast<bool> (no_actions));
    expect ("empty action result is empty", not no_actions.region->contains (rank_vector {-1}) and
                                                not no_actions.region->contains (rank_vector {0}));

    action_vec no_transitions (1);
    operation_budget empty_action_budget {unlimited_limits ()};
    update_result one_empty = try_input_update (
        whole_negative, std::vector<action_vec> {no_transitions}, empty_action_budget);
    expect ("one transition-free action differs from no actions",
            one_empty and one_empty.region->contains (rank_vector {-1}) and
                one_empty.region->contains (rank_vector {0}));
  }

  action_vec random_action (const rank_domain& domain, std::mt19937& random) {
    action_vec result (domain.dimensions ());
    std::uniform_int_distribution<int> edges (0, 4);
    std::uniform_int_distribution<int> source (0, static_cast<int> (domain.dimensions () - 1));
    std::bernoulli_distribution increment (0.5);
    for (auto& row : result)
      for (int edge = 0; edge < edges (random); ++edge)
        row.push_back ({static_cast<unsigned> (source (random)), increment (random)});
    return result;
  }

  void check_random_differential () {
    std::mt19937 random {20260919};
    const rank_domain domain = rank_domain::fixed_box (3, 2, 2, "random-mixed");
    const auto all = points (domain);
    std::uniform_int_distribution<std::size_t> point_index (0, all.size () - 1);
    std::uniform_int_distribution<int> maxima_count (0, 4);
    std::uniform_int_distribution<int> action_count (0, 3);

    for (int trial = 0; trial < 300; ++trial) {
      std::vector<rank_vector> maxima;
      for (int i = 0; i < maxima_count (random); ++i)
        maxima.push_back (all[point_index (random)]);
      auto positive = make_region (domain, frontier_form::max_included, std::move (maxima));
      operation_budget conversion {unlimited_limits ()};
      region_result converted = try_convert (positive, frontier_form::min_excluded, conversion);
      expect ("random conversion", static_cast<bool> (converted));
      dual_rank_region negative = std::move (*converted.region);

      const action_vec action = random_action (domain, random);
      operation_budget positive_budget {unlimited_limits ()};
      update_result positive_pre = try_action_preimage (positive, action, positive_budget);
      operation_budget negative_budget {unlimited_limits ()};
      update_result negative_pre = try_action_preimage (negative, action, negative_budget);
      expect ("random positive preimage", static_cast<bool> (positive_pre));
      expect ("random negative preimage", static_cast<bool> (negative_pre));
      for (const auto& point : all) {
        const bool expected = positive.contains (
            apply_forward (point, action, static_cast<VECTOR_ELT_T> (domain.k)));
        expect ("positive action differential", positive_pre.region->contains (point) == expected);
        expect ("negative action differential", negative_pre.region->contains (point) == expected);
      }

      std::vector<action_vec> actions;
      for (int i = 0, count = action_count (random); i < count; ++i)
        actions.push_back (random_action (domain, random));
      operation_budget pos_update_budget {unlimited_limits ()};
      update_result pos_update = try_input_update (positive, actions, pos_update_budget);
      operation_budget neg_update_budget {unlimited_limits ()};
      update_result neg_update = try_input_update (negative, actions, neg_update_budget);
      expect ("random positive input update", static_cast<bool> (pos_update));
      expect ("random negative input update", static_cast<bool> (neg_update));
      for (const auto& point : all) {
        const bool expected = direct_input_member (positive, point, actions);
        expect ("positive input differential", pos_update.region->contains (point) == expected);
        expect ("negative input differential", neg_update.region->contains (point) == expected);
      }
    }
  }

  dual_rank_region solve_sweep (dual_rank_region region,
                                const std::vector<std::vector<action_vec>>& inputs) {
    for (;;) {
      bool changed = false;
      for (const auto& actions : inputs) {
        operation_budget budget {unlimited_limits ()};
        update_result update = try_input_update (region, actions, budget);
        expect ("fixed-point update", static_cast<bool> (update));
        changed = changed or update.semantic_changed;
        region = std::move (*update.region);
      }
      if (not changed)
        return region;
    }
  }

  dual_rank_region solve_with_conversion_schedule (
      dual_rank_region region, const std::vector<std::vector<action_vec>>& inputs) {
    std::size_t step = 0;
    for (;;) {
      bool changed = false;
      for (const auto& actions : inputs) {
        if ((step++ % 2) == 0) {
          const frontier_form target = region.form () == frontier_form::max_included
                                           ? frontier_form::min_excluded
                                           : frontier_form::max_included;
          operation_budget conversion_budget {unlimited_limits ()};
          region_result converted = try_convert (region, target, conversion_budget);
          expect ("scheduled exact conversion", static_cast<bool> (converted));
          region = std::move (*converted.region);
        }
        operation_budget update_budget {unlimited_limits ()};
        update_result update = try_input_update (region, actions, update_budget);
        expect ("scheduled fixed-point update", static_cast<bool> (update));
        changed = changed or update.semantic_changed;
        region = std::move (*update.region);
      }
      if (not changed)
        return region;
    }
  }

  void check_sweep_quantifiers () {
    const rank_domain domain = rank_domain::fixed_box (1, 1, 1, "sweep-order");
    action_vec reset_to_bottom (1);
    action_vec accepting (1);
    accepting[0].push_back ({0, true});
    const std::vector<std::vector<action_vec>> inputs {{reset_to_bottom}, {accepting}};

    auto positive = make_region (domain, frontier_form::max_included, {domain.top ()});
    auto negative = make_region (domain, frontier_form::min_excluded, {});

    operation_budget first_budget {unlimited_limits ()};
    update_result first = try_input_update (positive, inputs[0], first_budget);
    expect ("one unchanged input is not a fixed point", first and not first.semantic_changed);
    operation_budget second_budget {unlimited_limits ()};
    update_result second = try_input_update (*first.region, inputs[1], second_budget);
    expect ("later input contracts region", second and second.semantic_changed);

    dual_rank_region positive_fixed = solve_sweep (std::move (positive), inputs);
    dual_rank_region negative_fixed = solve_sweep (std::move (negative), inputs);
    auto scheduled_start = make_region (domain, frontier_form::max_included, {domain.top ()});
    dual_rank_region scheduled_fixed =
        solve_with_conversion_schedule (std::move (scheduled_start), inputs);
    for (const auto& point : points (domain))
      expect ("positive/negative fixed-point membership",
              positive_fixed.contains (point) == negative_fixed.contains (point) and
                  positive_fixed.contains (point) == scheduled_fixed.contains (point));
    expect ("only absent rank survives", positive_fixed.contains (rank_vector {-1}) and
                                             not positive_fixed.contains (rank_vector {0}));
  }

  void check_predecessor_reversal_and_abort () {
    constexpr std::size_t outputs = 5;
    const rank_domain domain = rank_domain::fixed_box (3 * outputs, 1, 0, "predecessor-reversal");
    rank_vector exclusion = domain.bottom ();
    for (std::size_t destination = 0; destination < outputs; ++destination)
      exclusion[destination] = 0;
    auto negative = make_region (domain, frontier_form::min_excluded, {exclusion});
    action_vec action (domain.dimensions ());
    for (std::size_t destination = 0; destination < outputs; ++destination) {
      action[destination].push_back ({static_cast<unsigned> (outputs + 2 * destination), false});
      action[destination].push_back (
          {static_cast<unsigned> (outputs + 2 * destination + 1), false});
    }
    operation_budget budget {unlimited_limits ()};
    update_result preimage = try_action_preimage (negative, action, budget);
    expect ("predecessor reversal completes", static_cast<bool> (preimage));
    expect ("one exclusion expands to 2^m predecessor minima",
            preimage.region->generators ().size () == (1U << outputs));

    const auto source_hash = negative.semantic_hash ();
    budget_limits tiny = unlimited_limits ();
    tiny.max_work = 10;
    operation_budget aborting {tiny};
    update_result stopped = try_action_preimage (negative, action, aborting);
    expect ("predecessor abort is typed", stopped.status == completion::work_limit);
    expect ("predecessor abort leaves source unchanged", negative.semantic_hash () == source_hash);
  }
}

int main () {
  check_overflow_and_empty_actions ();
  check_random_differential ();
  check_sweep_quantifiers ();
  check_predecessor_reversal_and_abort ();
  return failures == 0 ? 0 : 1;
}
