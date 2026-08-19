#pragma once

#include "solver/diagnostics.hh"

#include <bddx.h>
#include <limits>
#include <map>
#include <unordered_set>

namespace ios_precomputers {

  inline void record_alphabet_census (bdd relation, int first_output, int first_state) {
    auto collect_frontier = [&] (this const auto& self, bdd root, int boundary,
                                 std::unordered_set<int>& visited,
                                 std::map<int, bdd>& frontier) -> void {
      if (root == bddfalse)
        return;
      if (root == bddtrue or bdd_var (root) >= boundary) {
        frontier.emplace (root.id (), root);
        return;
      }
      if (not visited.emplace (root.id ()).second)
        return;
      self (bdd_low (root), boundary, visited, frontier);
      self (bdd_high (root), boundary, visited, frontier);
    };

    auto count_paths = [&] (this const auto& self, bdd root, int boundary,
                            std::map<int, unsigned long long>& memo)
        -> unsigned long long {
      if (root == bddfalse)
        return 0;
      if (root == bddtrue or bdd_var (root) >= boundary)
        return 1;
      if (auto found = memo.find (root.id ()); found != memo.end ())
        return found->second;
      const auto low = self (bdd_low (root), boundary, memo);
      const auto high = self (bdd_high (root), boundary, memo);
      const auto max = std::numeric_limits<unsigned long long>::max ();
      const auto total = low > max - high ? max : low + high;
      memo.emplace (root.id (), total);
      return total;
    };

    std::unordered_set<int> input_visited;
    std::map<int, bdd> input_frontier;
    collect_frontier (relation, first_output, input_visited, input_frontier);

    std::map<int, unsigned long long> input_path_memo;
    const auto input_paths = count_paths (relation, first_output, input_path_memo);
    std::map<int, unsigned long long> output_path_memo;
    const auto output_paths = count_paths (relation, first_state, output_path_memo);

    unsigned long long output_nodes = 0;
    for (const auto& [id, output_root] : input_frontier) {
      (void) id;
      std::unordered_set<int> output_visited;
      std::map<int, bdd> output_frontier;
      collect_frontier (output_root, first_state, output_visited, output_frontier);
      output_nodes += output_frontier.size ();
    }

    acacia::diagnostics::set_alphabet_census (
        input_paths, input_frontier.size (), output_paths, output_nodes,
        bdd_nodecount (relation));
    acacia::diagnostics::snapshot ("alphabet-census");
  }

}  // namespace ios_precomputers
