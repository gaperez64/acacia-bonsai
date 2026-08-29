#pragma once

#include "ios_precomputers/semantic_action_census.hh"
#include "solver/diagnostics.hh"

#include <bddx.h>
#include <algorithm>
#include <limits>
#include <map>
#include <unordered_set>
#include <vector>

#if ACACIA_ENABLE_DIAGNOSTICS

namespace ios_precomputers {

  namespace census_detail {

    /// Distinct DAG nodes at or beyond `boundary`, keyed by node id.  BuDDy
    /// nodes are canonical, so distinct ids are distinct relations.
    inline void collect_frontier (bdd root, int boundary, std::unordered_set<int>& visited,
                                  std::map<int, bdd>& frontier) {
      if (root == bddfalse)
        return;
      if (root == bddtrue or bdd_var (root) >= boundary) {
        frontier.emplace (root.id (), root);
        return;
      }
      if (not visited.emplace (root.id ()).second)
        return;
      collect_frontier (bdd_low (root), boundary, visited, frontier);
      collect_frontier (bdd_high (root), boundary, visited, frontier);
    }

    /// Paths from `root` down to `boundary`.  The memo is keyed on node id and
    /// does not depend on where the descent started, so one memo can be shared
    /// across every input class.
    inline unsigned long long count_paths (bdd root, int boundary,
                                           std::map<int, unsigned long long>& memo) {
      if (root == bddfalse)
        return 0;
      if (root == bddtrue or bdd_var (root) >= boundary)
        return 1;
      if (auto found = memo.find (root.id ()); found != memo.end ())
        return found->second;
      const auto low = count_paths (bdd_low (root), boundary, memo);
      const auto high = count_paths (bdd_high (root), boundary, memo);
      const auto max = std::numeric_limits<unsigned long long>::max ();
      const auto total = low > max - high ? max : low + high;
      memo.emplace (root.id (), total);
      return total;
    }

  }  // namespace census_detail

  inline void record_alphabet_census (bdd relation, int first_output, int first_state) {
    using acacia::diagnostics::clock;
    const auto census_started = clock::now ();

    std::unordered_set<int> input_visited;
    std::map<int, bdd> input_frontier;
    census_detail::collect_frontier (relation, first_output, input_visited, input_frontier);

    std::map<int, unsigned long long> input_path_memo;
    const auto input_paths = census_detail::count_paths (relation, first_output, input_path_memo);
    std::map<int, unsigned long long> output_path_memo;
    const auto output_paths = census_detail::count_paths (relation, first_state, output_path_memo);

    // Per-input aggregation.  `output_nodes` is the SUM over input classes of
    // that class's distinct residual roots, because each input class decodes
    // its own actions: a root shared by two inputs is decoded twice today and
    // would be decoded twice under an equality quotient too.  The maxima say
    // whether the duplication is concentrated in one input or spread evenly.
    const bool want_dominance = acacia::diagnostics::semantic_dominance_census ();
    const auto budget = dominance_budget_from_env ();
    unsigned long long output_nodes = 0;
    unsigned long long max_output_paths = 0;
    unsigned long long max_output_nodes = 0;
    unsigned long long minimal_output_nodes = 0;
    unsigned long long dominance_tests = 0;
    unsigned long long dominance_declines = 0;
    unsigned long long dominance_ms = 0;

    for (const auto& [id, output_root] : input_frontier) {
      (void) id;
      std::unordered_set<int> output_visited;
      std::map<int, bdd> output_frontier;
      census_detail::collect_frontier (output_root, first_state, output_visited, output_frontier);
      output_nodes += output_frontier.size ();
      max_output_nodes = std::max<unsigned long long> (max_output_nodes, output_frontier.size ());
      max_output_paths = std::max (
          max_output_paths, census_detail::count_paths (output_root, first_state, output_path_memo));

      if (not want_dominance)
        continue;

      std::vector<bdd> roots;
      roots.reserve (output_frontier.size ());
      for (const auto& [root_id, root] : output_frontier) {
        (void) root_id;
        roots.push_back (root);
      }
      const auto dominance_started = clock::now ();
      const auto pruned = minimal_by_inclusion (roots, budget);
      dominance_ms += (unsigned long long) std::chrono::duration_cast<std::chrono::milliseconds> (
                          clock::now () - dominance_started)
                          .count ();
      minimal_output_nodes += pruned.minimal;
      dominance_tests += pruned.tests;
      dominance_declines += pruned.declined ? 1 : 0;
    }

    const auto census_ms = (unsigned long long)
        std::chrono::duration_cast<std::chrono::milliseconds> (clock::now () - census_started)
            .count ();

    acacia::diagnostics::set_alphabet_census (
        input_paths, input_frontier.size (), output_paths, output_nodes,
        bdd_nodecount (relation));
    acacia::diagnostics::set_semantic_action_census (max_output_paths, max_output_nodes,
                                                     minimal_output_nodes, dominance_tests,
                                                     dominance_declines, census_ms, dominance_ms);
    acacia::diagnostics::snapshot ("alphabet-census");
  }

}  // namespace ios_precomputers

#endif  // ACACIA_ENABLE_DIAGNOSTICS
