#pragma once

/// rank_action_replay.hh — Acacia's rank semantics, offline.
///
/// One implementation, shared by every research tool that has to reproduce what
/// the solver does to a rank vector.  The forward and backward maps here are
/// transcriptions of `actioners::standard::apply`, and the domination test is
/// the same necessary rank-sum condition the shipping downset uses.  A second,
/// subtly different copy of these would make any disagreement between a tool
/// and the solver ambiguous, which is the one thing these tools exist to rule
/// out.

#include "configuration.hh"

#include <algorithm>
#include <vector>

#include <posets/utils/vector_mm.hh>

namespace acacia::research {

  using rank_vector = posets::utils::vector_mm<VECTOR_ELT_T>;

  /// `avec[i]` holds the pairs `(j, increment)` relating coordinate `i` to
  /// coordinate `j`, which is how `actioners::standard` stores an action.
  using action = std::vector<std::pair<unsigned, bool>>;
  using action_vec = std::vector<action>;

  /// The deterministic forward rank update, `tau`.
  ///
  /// `avec` is indexed by destination and holds sources, so this is: a state is
  /// present in the successor iff some predecessor was present, and its counter
  /// is the largest predecessor counter plus one on an accepting step,
  /// saturating at K.  -1 means absent and does not propagate.
  inline rank_vector apply_forward (const rank_vector& m, const action_vec& avec,
                                    VECTOR_ELT_T K) {
    rank_vector out (m.size (), -1);
    for (size_t p = 0; p < m.size (); ++p)
      for (const auto& [q, p_final] : avec[p]) {
        if (m[q] != -1)
          out[p] = std::max (out[p],
                             std::min (K, (VECTOR_ELT_T) (m[q] + (VECTOR_ELT_T) (p_final ? 1 : 0))));
        if (out[p] == K)
          break;  // max cannot grow further; the solver short-circuits here too
      }
    return out;
  }

  /// The backward upper-corner transformer, seeded from the reset the solver
  /// uses: K-1 on a counting coordinate, 0 on a Boolean one.
  inline rank_vector apply_backward (const rank_vector& m, const action_vec& avec,
                                     VECTOR_ELT_T K, size_t bool_threshold) {
    rank_vector out (m.size (), 0);
    const size_t counting = std::min (bool_threshold, m.size ());
    std::fill_n (out.begin (), counting, (VECTOR_ELT_T) (K - 1));
    std::fill (out.begin () + static_cast<std::ptrdiff_t> (counting), out.end (),
               (VECTOR_ELT_T) 0);

    for (size_t p = 0; p < m.size (); ++p)
      for (const auto& [q, p_final] : avec[p])
        if (out[q] != -1)
          out[q] = std::min (
              out[q], std::max ((VECTOR_ELT_T) -1,
                                (VECTOR_ELT_T) (m[p] - (VECTOR_ELT_T) (p_final ? 1 : 0))));
    return out;
  }

  /// The safe vector: K-1 on counting coordinates, 0 on Boolean ones.  A
  /// candidate generator introduced by a search must be below it; for a region
  /// the solver produced this holds by construction, since it starts there.
  inline rank_vector safe_vector (size_t states, VECTOR_ELT_T K, size_t bool_threshold) {
    rank_vector safe (states, (VECTOR_ELT_T) (K - 1));
    for (size_t i = std::min (bool_threshold, states); i < states; ++i)
      safe[i] = 0;
    return safe;
  }

  /// The initial rank vector: absent everywhere but the automaton's initial
  /// state, which starts at counter zero.
  inline rank_vector initial_vector (size_t states, size_t init_state) {
    rank_vector v (states, -1);
    v[init_state] = 0;
    return v;
  }

  inline bool leq (const rank_vector& a, const rank_vector& b) {
    for (size_t i = 0; i < a.size (); ++i)
      if (a[i] > b[i])
        return false;
    return true;
  }

  inline long long rank_of (const rank_vector& v) {
    long long sum = 0;
    for (size_t i = 0; i < v.size (); ++i)
      sum += v[i];
    return sum;
  }

  /// Generators kept sorted by coordinate sum, with an optional active mask.
  ///
  /// A dominator `w` of `v` has `v <= w` componentwise and therefore
  /// `rank (w) >= rank (v)`, so the scan can start at the first generator of
  /// rank `rank (v)` -- the same necessary condition `rank_bucketed_vector_backed`
  /// applies, over a subset it cannot express.
  class generator_index {
    public:
      explicit generator_index (std::vector<rank_vector> generators)
        : items (std::move (generators)) {
        order.resize (items.size ());
        for (size_t i = 0; i < items.size (); ++i)
          order[i] = i;
        ranks.reserve (items.size ());
        for (const auto& v : items)
          ranks.push_back (rank_of (v));
        std::ranges::sort (order, [this] (size_t a, size_t b) { return ranks[a] < ranks[b]; });
        active.assign (items.size (), true);
      }

      [[nodiscard]] size_t size () const { return items.size (); }
      [[nodiscard]] size_t active_count () const {
        return static_cast<size_t> (std::ranges::count (active, true));
      }
      [[nodiscard]] const rank_vector& operator[] (size_t i) const { return items[i]; }
      [[nodiscard]] bool is_active (size_t i) const { return active[i]; }
      void deactivate (size_t i) { active[i] = false; }

      /// Index of an active generator dominating `v`, or npos.  `checks` counts
      /// full partial-order comparisons, which is the cost worth reporting.
      [[nodiscard]] size_t find_dominator (const rank_vector& v, unsigned long long& checks) const {
        const long long rv = rank_of (v);
        const auto first = std::ranges::lower_bound (
            order, rv, {}, [this] (size_t i) { return ranks[i]; });
        for (auto it = first; it != order.end (); ++it) {
          if (not active[*it])
            continue;
          ++checks;
          if (leq (v, items[*it]))
            return *it;
        }
        return npos;
      }

      static constexpr size_t npos = static_cast<size_t> (-1);

    private:
      std::vector<rank_vector> items;
      std::vector<long long> ranks;
      std::vector<size_t> order;  ///< indices sorted by ascending rank
      std::vector<bool> active;
  };

}  // namespace acacia::research
