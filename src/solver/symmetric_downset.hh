#pragma once

// A downset of counter-vectors, represented compactly as canonical
// (orbit-representative) COUNT-VECTORS under a verified client-permutation
// group (symmetry::group with full_symmetric=true), instead of raw per-client
// vectors. See DIAGNOSIS.md, "Symmetry reduction: design + status", for the
// full derivation and soundness argument. Summary:
//
//  - A vector's coordinates split into SHARED (fixed by every generator) and
//    CLIENT-BLOCK coordinates (B blocks of size n, one client-owned
//    coordinate per block); a client's "type" is its B-tuple of block values.
//  - Two raw vectors are in the same symmetry orbit iff they have the same
//    multiset of client types, i.e. the same COUNT-VECTOR (type -> count).
//    This is the canonical representation: bounded size r <= (K+2)^B,
//    independent of n.
//  - contains(T, v): EXACT. Reduces to bipartite transportation feasibility
//    (max-flow) between the two count-vectors' type-distributions, using the
//    per-type dominance order as allowed edges. Polynomial in r.
//  - union_with: EXACT. Pairwise dominance filtering using the same test.
//  - intersect_with: NOT exact (no known polynomial characterization of the
//    *maximal achievable* merge outcomes). Implemented as a capped, SOUND
//    under-approximation: every generated candidate is an explicit, valid
//    transportation-plan merge (so never fabricates an invalid point); we
//    just may not find every maximal one. Because acacia's fixpoint is a
//    greatest-fixed-point (monotonically shrinking) computation, an
//    under-approximating intersect can only make the algorithm MORE
//    conservative (fall back to UNKNOWN more often), never produce a wrong
//    definitive REALIZABLE/UNREALIZABLE verdict.

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

#ifndef ACACIA_SYMMETRY_INTERSECT_MAX_PLANS
# define ACACIA_SYMMETRY_INTERSECT_MAX_PLANS 20
#endif

namespace symmetric_downset {

  using value_t = int;                    // matches acacia's VECTOR_ELT_T range
  using type_t = std::vector<value_t>;    // one client's B-tuple

  // A canonical count-vector: shared coordinates (plain values) + a sparse
  // type -> count map for the client blocks. Invariant: counts sum to n.
  struct count_vector {
      std::vector<value_t> shared;
      std::map<type_t, long> counts;      // only types with count > 0

      bool operator== (const count_vector&) const = default;

      long total_clients () const {
        long s = 0;
        for (auto& [t, c] : counts) s += c;
        return s;
      }
  };

  namespace detail {
    // Pointwise dominance on shared coordinates.
    inline bool shared_dominates (const std::vector<value_t>& a, const std::vector<value_t>& b) {
      if (a.size () != b.size ()) return false;
      for (size_t i = 0; i < a.size (); ++i)
        if (a[i] < b[i]) return false;
      return true;
    }

    // Pointwise dominance on a client type tuple.
    inline bool type_dominates (const type_t& a, const type_t& b) {
      if (a.size () != b.size ()) return false;
      for (size_t i = 0; i < a.size (); ++i)
        if (a[i] < b[i]) return false;
      return true;
    }

    // Bipartite transportation feasibility: does a flow of value `demand_total`
    // exist from left-supplies to right-demands using only allowed (i,j)
    // edges? Simple Edmonds-Karp style max-flow; the graphs here have at most
    // ~2r+2 nodes (r = number of distinct types), so this is fast regardless
    // of how large the individual supply/demand values (up to n) are, EXCEPT
    // the augmenting-path capacity scaling: we push the BOTTLENECK capacity
    // per augmenting path (not unit flow), so the number of augmentations is
    // bounded by the number of edges, independent of n.
    inline bool transportation_feasible (const std::vector<long>& supply,
                                         const std::vector<long>& demand,
                                         const std::vector<std::vector<bool>>& allowed) {
      const size_t L = supply.size (), R = demand.size ();
      long total_supply = 0, total_demand = 0;
      for (long s : supply) total_supply += s;
      for (long d : demand) total_demand += d;
      if (total_supply < total_demand) return false;  // can't possibly cover demand
      if (total_demand == 0) return true;

      // Node ids: 0 = source, 1..L = left, L+1..L+R = right, L+R+1 = sink.
      const size_t N = L + R + 2;
      const size_t SRC = 0, SNK = N - 1;
      std::vector<std::vector<long>> cap (N, std::vector<long> (N, 0));
      for (size_t i = 0; i < L; ++i) cap[SRC][1 + i] = supply[i];
      for (size_t j = 0; j < R; ++j) cap[1 + L + j][SNK] = demand[j];
      for (size_t i = 0; i < L; ++i)
        for (size_t j = 0; j < R; ++j)
          if (allowed[i][j])
            cap[1 + i][1 + L + j] = total_supply;  // effectively unbounded per-edge

      long flow = 0;
      while (true) {
        std::vector<long> parent (N, -1);
        std::vector<long> bottleneck (N, 0);
        parent[SRC] = (long) SRC;
        bottleneck[SRC] = std::numeric_limits<long>::max ();
        std::queue<size_t> q;
        q.push (SRC);
        while (not q.empty ()) {
          size_t u = q.front (); q.pop ();
          for (size_t v = 0; v < N; ++v) {
            if (parent[v] == -1 and cap[u][v] > 0) {
              parent[v] = (long) u;
              bottleneck[v] = std::min (bottleneck[u], cap[u][v]);
              if (v == SNK) break;
              q.push (v);
            }
          }
        }
        if (parent[SNK] == -1) break;  // no augmenting path
        long add = bottleneck[SNK];
        size_t v = SNK;
        while (v != SRC) {
          size_t u = (size_t) parent[v];
          cap[u][v] -= add;
          cap[v][u] += add;
          v = u;
        }
        flow += add;
      }
      return flow >= total_demand;
    }

    // Build the (supply, demand, allowed) transportation instance between two
    // type-distributions and test feasibility -- the exact "is v dominated by
    // u" test (per-client pairing existence).
    inline bool client_dominates (const std::map<type_t, long>& u_counts,
                                  const std::map<type_t, long>& v_counts) {
      std::vector<type_t> ltypes, rtypes;
      std::vector<long> supply, demand;
      for (auto& [t, c] : u_counts) { ltypes.push_back (t); supply.push_back (c); }
      for (auto& [t, c] : v_counts) { rtypes.push_back (t); demand.push_back (c); }
      std::vector<std::vector<bool>> allowed (ltypes.size (),
                                              std::vector<bool> (rtypes.size (), false));
      for (size_t i = 0; i < ltypes.size (); ++i)
        for (size_t j = 0; j < rtypes.size (); ++j)
          allowed[i][j] = type_dominates (ltypes[i], rtypes[j]);
      return transportation_feasible (supply, demand, allowed);
    }
  }  // namespace detail

  // Exact: does count-vector `u` dominate count-vector `v` (shared AND per-client)?
  inline bool dominates (const count_vector& u, const count_vector& v) {
    return detail::shared_dominates (u.shared, v.shared) and
           detail::client_dominates (u.counts, v.counts);
  }

  // Exact: is `v` dominated by some element of antichain `T`?
  inline bool contains (const std::vector<count_vector>& T, const count_vector& v) {
    for (const auto& u : T)
      if (dominates (u, v))
        return true;
    return false;
  }

  inline void add_maximal (std::vector<count_vector>& antichain, count_vector candidate) {
    if (contains (antichain, candidate))
      return;
    antichain.erase (
        std::remove_if (antichain.begin (), antichain.end (),
                        [&] (const count_vector& existing) {
                          return dominates (candidate, existing);
                        }),
        antichain.end ());
    antichain.push_back (std::move (candidate));
  }

  // Exact: antichain of maximal elements of A union B (pairwise dominance filter).
  inline std::vector<count_vector> union_with (const std::vector<count_vector>& A,
                                               const std::vector<count_vector>& B) {
    std::vector<count_vector> res;
    res.reserve (A.size () + B.size ());
    for (const auto& a : A)
      add_maximal (res, a);
    for (const auto& b : B)
      add_maximal (res, b);
    return res;
  }

  namespace detail {
    using key_fn = std::function<long (const type_t&)>;
    using merge_plan = std::pair<key_fn, key_fn>;

    // Northwest-corner transportation plan between two type-multisets, using
    // the given per-side sort keys. ALWAYS a valid plan (respects supply and
    // demand exactly), size O(|u_counts|+|v_counts|), by construction -- so
    // whatever merge count-vector it induces is a genuinely achievable point.
    // Different key choices explore different (generally non-dominated w.r.t.
    // each other) corners of the transportation polytope.
    template <typename KeyU, typename KeyV>
    inline std::map<type_t, long> nw_corner_merge (const std::map<type_t, long>& u_counts,
                                                    const std::map<type_t, long>& v_counts,
                                                    KeyU key_u, KeyV key_v) {
      std::vector<std::pair<type_t, long>> U (u_counts.begin (), u_counts.end ());
      std::vector<std::pair<type_t, long>> V (v_counts.begin (), v_counts.end ());
      std::sort (U.begin (), U.end (),
                 [&] (auto& a, auto& b) { return key_u (a.first) > key_u (b.first); });
      std::sort (V.begin (), V.end (),
                 [&] (auto& a, auto& b) { return key_v (a.first) > key_v (b.first); });

      std::map<type_t, long> merged;
      size_t i = 0, j = 0;
      long rem_u = (i < U.size ()) ? U[i].second : 0;
      long rem_v = (j < V.size ()) ? V[j].second : 0;
      while (i < U.size () and j < V.size ()) {
        const long take = std::min (rem_u, rem_v);
        if (take > 0) {
          type_t m (U[i].first.size ());
          for (size_t b = 0; b < m.size (); ++b) m[b] = std::min (U[i].first[b], V[j].first[b]);
          merged[m] += take;
        }
        rem_u -= take;
        rem_v -= take;
        if (rem_u == 0) { ++i; if (i < U.size ()) rem_u = U[i].second; }
        if (rem_v == 0) { ++j; if (j < V.size ()) rem_v = V[j].second; }
      }
      return merged;
    }

    inline long type_sum (const type_t& t) {
      long s = 0;
      for (value_t v : t) s += v;
      return s;
    }

    inline void append_plan_orientations (std::vector<merge_plan>& plans,
                                          const key_fn& pos, const key_fn& neg) {
      if (plans.size () < ACACIA_SYMMETRY_INTERSECT_MAX_PLANS)
        plans.push_back ({pos, pos});
      if (plans.size () < ACACIA_SYMMETRY_INTERSECT_MAX_PLANS)
        plans.push_back ({neg, neg});
      if (plans.size () < ACACIA_SYMMETRY_INTERSECT_MAX_PLANS)
        plans.push_back ({pos, neg});
      if (plans.size () < ACACIA_SYMMETRY_INTERSECT_MAX_PLANS)
        plans.push_back ({neg, pos});
    }

    inline std::vector<merge_plan> intersect_merge_plans (size_t dimensions) {
      const key_fn pos_sum = [] (const type_t& t) { return type_sum (t); };
      const key_fn neg_sum = [] (const type_t& t) { return -type_sum (t); };

      std::vector<merge_plan> plans;
      plans.reserve (ACACIA_SYMMETRY_INTERSECT_MAX_PLANS);
      append_plan_orientations (plans, pos_sum, neg_sum);
      for (size_t b = 0; b < dimensions and
                         plans.size () < ACACIA_SYMMETRY_INTERSECT_MAX_PLANS;
           ++b) {
        const key_fn pos_coord = [b] (const type_t& t) {
          return b < t.size () ? (long) t[b] : 0L;
        };
        const key_fn neg_coord = [b] (const type_t& t) {
          return b < t.size () ? -(long) t[b] : 0L;
        };
        append_plan_orientations (plans, pos_coord, neg_coord);
      }
      return plans;
    }
  }  // namespace detail

  // Capped, SOUND under-approximation of downset(A) ∩ downset(B)'s canonical
  // antichain: for each pair (u,v), generate a bounded set of candidate
  // merges via Northwest-corner transportation plans under a few different
  // sort-key orientations (each one individually a valid achievable point by
  // construction), then keep only the maximal ones. May miss some maximal
  // achievable merges (no known polynomial characterization of ALL of them --
  // see DIAGNOSIS.md); never fabricates an invalid one, so this can only make
  // the result SMALLER than the true intersection, never larger -- sound for
  // acacia's greatest-fixed-point iteration (see DIAGNOSIS.md).
  inline std::vector<count_vector> intersect_with (const std::vector<count_vector>& A,
                                                    const std::vector<count_vector>& B) {
    size_t dimensions = 0;
    for (const auto& u : A)
      if (not u.counts.empty ()) {
        dimensions = u.counts.begin ()->first.size ();
        break;
      }
    if (dimensions == 0) {
      for (const auto& v : B)
        if (not v.counts.empty ()) {
          dimensions = v.counts.begin ()->first.size ();
          break;
        }
    }
    const auto plans = detail::intersect_merge_plans (dimensions);

    std::vector<count_vector> candidates;
    for (const auto& u : A) {
      for (const auto& v : B) {
        count_vector base;
        base.shared.resize (u.shared.size ());
        for (size_t i = 0; i < u.shared.size (); ++i)
          base.shared[i] = std::min (u.shared[i], v.shared[i]);

        // Four Northwest-corner orientations: (descending,descending),
        // (ascending,ascending), (descending,ascending), (ascending,descending),
        // plus a bounded number of per-block coordinate orientations.
        for (const auto& [ku, kv] : plans) {
          count_vector cand = base;
          cand.counts = detail::nw_corner_merge (u.counts, v.counts, ku, kv);
          add_maximal (candidates, std::move (cand));
        }
      }
    }
    return candidates;
  }

}  // namespace symmetric_downset
