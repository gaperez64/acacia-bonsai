#pragma once

/// Minimal generators of an upward-closed losing set.
///
/// If `l` is losing, every `r` with `l <= r` is losing as well.  Posets ships
/// sixteen downward-closed set implementations and no upward-closed dual, so
/// this small dual antichain is deliberately hand-rolled.  Coordinate sums are
/// used only as a necessary-condition prefilter; every dominance decision
/// itself uses the Posets partial order.

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace acacia::solver_detail {

  template <typename State>
  class minimal_losing_antichain {
      using rank_type = std::int64_t;

      struct stored_generator {
          State value;
          rank_type rank;
      };

    public:
      /// Number of calls to `subsumes`, including those made by `insert`.
      mutable std::size_t queries = 0;
      /// Subsumption queries answered by a stored generator.
      mutable std::size_t hits = 0;
      /// Full partial-order comparisons avoided by the rank prefilter.
      mutable std::size_t prefilter_skips = 0;
      /// Generators newly stored after a successful insertion.
      std::size_t insertions = 0;
      /// Existing non-minimal generators removed by successful insertions.
      std::size_t removals = 0;

      /// Whether some stored generator `l` satisfies `l <= r`.
      [[nodiscard]] bool subsumes (const State& r) const {
        ++queries;
        const rank_type r_rank = rank_of (r);
        for (const auto& generator : generators) {
          if (generator.rank > r_rank) {
            ++prefilter_skips;
            continue;
          }
          if (generator.value.partial_order (r).leq ()) {
            ++hits;
            return true;
          }
        }
        return false;
      }

      /// Insert `r` if it is not already in the generated upward closure.
      bool insert (const State& r) {
        if (subsumes (r))
          return false;

        const rank_type r_rank = rank_of (r);
        std::size_t write = 0;
        for (std::size_t read = 0; read < generators.size (); ++read) {
          bool remove = false;
          if (r_rank > generators[read].rank)
            ++prefilter_skips;
          else
            remove = r.partial_order (generators[read].value).leq ();

          if (remove) {
            ++removals;
            continue;
          }
          if (write != read)
            generators[write] = std::move (generators[read]);
          ++write;
        }
        generators.erase (generators.begin () + static_cast<std::ptrdiff_t> (write),
                          generators.end ());
        generators.push_back ({copy_of (r), r_rank});
        ++insertions;
        return true;
      }

      [[nodiscard]] std::size_t size () const { return generators.size (); }

    private:
      std::vector<stored_generator> generators;

      [[nodiscard]] static rank_type rank_of (const State& value) {
        rank_type sum = 0;
        for (std::size_t i = 0; i < value.size (); ++i)
          sum += static_cast<rank_type> (value[i]);
        return sum;
      }

      [[nodiscard]] static State copy_of (const State& value) {
        if constexpr (requires { value.copy (); })
          return value.copy ();
        else
          return value;
      }
  };

}  // namespace acacia::solver_detail
