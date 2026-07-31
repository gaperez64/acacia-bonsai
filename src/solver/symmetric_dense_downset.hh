#pragma once

// Dense/SIMD-assisted adapter for symmetric_downset::count_vector.
//
// The live quotient solver still exposes the sparse, map-backed count-vector
// type because that keeps the integration conservative. This header provides a
// dense internal representation over a per-operation "type universe":
//
//   - client type tuples are interned to compact integer type IDs;
//   - counts and shared coordinates use posets::utils::vector_mm for aligned
//     storage;
//   - shared-coordinate comparisons/mins use std::experimental::simd;
//   - type dominance and type meets are precomputed once per universe.
//
// This intentionally does NOT implement the generic posets::Downset concept for
// the main symmetry path: symmetry intersection is a bounded set of concrete
// transportation-plan meets, not a single componentwise meet.

#include "configuration.hh"
#include "solver/symmetric_downset.hh"

#include <posets/utils/simd_traits.hh>
#include <posets/utils/vector_mm.hh>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <experimental/simd>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <utility>
#include <vector>

namespace symmetric_dense_downset {

  using value_t = symmetric_downset::value_t;
  using type_t = symmetric_downset::type_t;
  using sparse_count_vector = symmetric_downset::count_vector;
  using count_t = long;
  using type_id_t = unsigned;

  inline constexpr type_id_t invalid_type_id = std::numeric_limits<type_id_t>::max ();

  namespace detail {

    template <typename T>
    inline bool all_geq_simd (const posets::utils::vector_mm<T>& a,
                              const posets::utils::vector_mm<T>& b) {
      if (a.size () != b.size ())
        return false;
      using simd_t = typename posets::utils::simd_traits<T>::fssimd;
      constexpr size_t lanes = posets::utils::simd_traits<T>::simd_size;
      size_t i = 0;
      for (; i + lanes <= a.size (); i += lanes) {
        simd_t va, vb;
        va.copy_from (a.data () + i, std::experimental::element_aligned);
        vb.copy_from (b.data () + i, std::experimental::element_aligned);
        if (not std::experimental::all_of (va >= vb))
          return false;
      }
      for (; i < a.size (); ++i)
        if (a[i] < b[i])
          return false;
      return true;
    }

    template <typename T>
    inline posets::utils::vector_mm<T> min_simd (const posets::utils::vector_mm<T>& a,
                                                 const posets::utils::vector_mm<T>& b) {
      assert (a.size () == b.size ());
      posets::utils::vector_mm<T> out (a.size ());
      using simd_t = typename posets::utils::simd_traits<T>::fssimd;
      constexpr size_t lanes = posets::utils::simd_traits<T>::simd_size;
      size_t i = 0;
      for (; i + lanes <= a.size (); i += lanes) {
        simd_t va, vb, vc;
        va.copy_from (a.data () + i, std::experimental::element_aligned);
        vb.copy_from (b.data () + i, std::experimental::element_aligned);
        vc = std::experimental::min (va, vb);
        vc.copy_to (out.data () + i, std::experimental::element_aligned);
      }
      for (; i < a.size (); ++i)
        out[i] = std::min (a[i], b[i]);
      return out;
    }

    inline bool type_dominates (const type_t& a, const type_t& b) {
      if (a.size () != b.size ())
        return false;
      for (size_t i = 0; i < a.size (); ++i)
        if (a[i] < b[i])
          return false;
      return true;
    }

    inline type_t type_meet (const type_t& a, const type_t& b) {
      assert (a.size () == b.size ());
      type_t out (a.size ());
      for (size_t i = 0; i < a.size (); ++i)
        out[i] = std::min (a[i], b[i]);
      return out;
    }

    inline long type_sum (const type_t& t) {
      long s = 0;
      for (value_t v : t)
        s += v;
      return s;
    }

  }  // namespace detail

  struct type_universe {
      size_t dimensions = 0;
      size_t shared_dimensions = 0;
      std::vector<type_t> types;
      std::map<type_t, type_id_t> ids;
      std::vector<std::vector<unsigned char>> dominates_type;
      std::vector<std::vector<type_id_t>> meet_id;

      type_id_t intern (const type_t& t) {
        if (types.empty ())
          dimensions = t.size ();
        if (t.size () != dimensions)
          return invalid_type_id;
        auto [it, inserted] = ids.emplace (t, (type_id_t) types.size ());
        if (inserted)
          types.push_back (t);
        return it->second;
      }

      type_id_t find (const type_t& t) const {
        auto it = ids.find (t);
        return it == ids.end () ? invalid_type_id : it->second;
      }

      void finalize () {
        const size_t n = types.size ();
        dominates_type.assign (n, std::vector<unsigned char> (n, 0));
        meet_id.assign (n, std::vector<type_id_t> (n, invalid_type_id));
        for (type_id_t i = 0; i < n; ++i) {
          for (type_id_t j = 0; j < n; ++j) {
            dominates_type[i][j] = detail::type_dominates (types[i], types[j]) ? 1 : 0;
            meet_id[i][j] = find (detail::type_meet (types[i], types[j]));
          }
        }
      }
  };

  struct count_vector {
      posets::utils::vector_mm<value_t> shared;
      posets::utils::vector_mm<count_t> counts;
      std::vector<type_id_t> support;

      count_vector () = default;
      count_vector (size_t shared_size, size_t type_count)
        : shared (shared_size, 0), counts (type_count, 0) {}

      void add_count (type_id_t id, count_t amount) {
        if (amount == 0)
          return;
        if (counts[id] == 0)
          support.push_back (id);
        counts[id] += amount;
      }

      void normalize_support () {
        support.erase (std::remove_if (support.begin (), support.end (),
                                       [&] (type_id_t id) { return counts[id] == 0; }),
                       support.end ());
        std::sort (support.begin (), support.end ());
        support.erase (std::unique (support.begin (), support.end ()), support.end ());
      }
  };

  inline void add_sparse_types (type_universe& U, const std::vector<sparse_count_vector>& X) {
    for (const auto& c : X) {
      U.shared_dimensions = std::max (U.shared_dimensions, c.shared.size ());
      for (const auto& [t, cnt] : c.counts)
        if (cnt > 0)
          U.intern (t);
    }
  }

  inline type_universe make_universe (const std::vector<sparse_count_vector>& A,
                                      const std::vector<sparse_count_vector>& B,
                                      bool close_pairwise_meets) {
    type_universe U;
    add_sparse_types (U, A);
    add_sparse_types (U, B);
    if (close_pairwise_meets) {
      const auto base_types = U.types;
      for (const auto& a : base_types)
        for (const auto& b : base_types)
          U.intern (detail::type_meet (a, b));
    }
    U.finalize ();
    return U;
  }

  inline count_vector to_dense (const type_universe& U, const sparse_count_vector& c) {
    count_vector out (U.shared_dimensions, U.types.size ());
    for (size_t i = 0; i < c.shared.size (); ++i)
      out.shared[i] = c.shared[i];
    for (const auto& [t, cnt] : c.counts) {
      if (cnt <= 0)
        continue;
      const type_id_t id = U.find (t);
      assert (id != invalid_type_id);
      out.add_count (id, cnt);
    }
    out.normalize_support ();
    return out;
  }

  inline sparse_count_vector to_sparse (const type_universe& U, const count_vector& c) {
    sparse_count_vector out;
    out.shared.assign (c.shared.begin (), c.shared.end ());
    for (type_id_t id : c.support)
      if (c.counts[id] > 0)
        out.counts[U.types[id]] += c.counts[id];
    return out;
  }

  inline bool transportation_feasible (const std::vector<count_t>& supply,
                                       const std::vector<count_t>& demand,
                                       const std::vector<std::vector<unsigned char>>& allowed) {
    const size_t L = supply.size (), R = demand.size ();
    count_t total_supply = 0, total_demand = 0;
    for (count_t s : supply) total_supply += s;
    for (count_t d : demand) total_demand += d;
    if (total_supply < total_demand)
      return false;
    if (total_demand == 0)
      return true;

    const size_t N = L + R + 2;
    const size_t SRC = 0, SNK = N - 1;
    std::vector<std::vector<count_t>> cap (N, std::vector<count_t> (N, 0));
    for (size_t i = 0; i < L; ++i) cap[SRC][1 + i] = supply[i];
    for (size_t j = 0; j < R; ++j) cap[1 + L + j][SNK] = demand[j];
    for (size_t i = 0; i < L; ++i)
      for (size_t j = 0; j < R; ++j)
        if (allowed[i][j])
          cap[1 + i][1 + L + j] = total_supply;

    count_t flow = 0;
    while (true) {
      std::vector<long> parent (N, -1);
      std::vector<count_t> bottleneck (N, 0);
      parent[SRC] = (long) SRC;
      bottleneck[SRC] = std::numeric_limits<count_t>::max ();
      std::queue<size_t> q;
      q.push (SRC);
      while (not q.empty ()) {
        const size_t u = q.front ();
        q.pop ();
        for (size_t v = 0; v < N; ++v) {
          if (parent[v] == -1 and cap[u][v] > 0) {
            parent[v] = (long) u;
            bottleneck[v] = std::min (bottleneck[u], cap[u][v]);
            if (v == SNK)
              break;
            q.push (v);
          }
        }
      }
      if (parent[SNK] == -1)
        break;
      const count_t add = bottleneck[SNK];
      size_t v = SNK;
      while (v != SRC) {
        const size_t u = (size_t) parent[v];
        cap[u][v] -= add;
        cap[v][u] += add;
        v = u;
      }
      flow += add;
    }
    return flow >= total_demand;
  }

  inline bool client_dominates (const type_universe& U, const count_vector& u,
                                const count_vector& v) {
    std::vector<count_t> supply, demand;
    supply.reserve (u.support.size ());
    demand.reserve (v.support.size ());
    for (type_id_t id : u.support)
      supply.push_back (u.counts[id]);
    for (type_id_t id : v.support)
      demand.push_back (v.counts[id]);

    std::vector<std::vector<unsigned char>> allowed (
        u.support.size (), std::vector<unsigned char> (v.support.size (), 0));
    for (size_t j = 0; j < v.support.size (); ++j) {
      bool covered = false;
      for (size_t i = 0; i < u.support.size (); ++i) {
        const bool ok = U.dominates_type[u.support[i]][v.support[j]] != 0;
        allowed[i][j] = ok ? 1 : 0;
        covered = covered or ok;
      }
      if (not covered and v.counts[v.support[j]] > 0)
        return false;
    }
    return transportation_feasible (supply, demand, allowed);
  }

  inline bool dominates (const type_universe& U, const count_vector& u,
                         const count_vector& v) {
    return detail::all_geq_simd (u.shared, v.shared) and client_dominates (U, u, v);
  }

  inline bool contains (const type_universe& U, const std::vector<count_vector>& T,
                        const count_vector& v) {
    for (const auto& u : T)
      if (dominates (U, u, v))
        return true;
    return false;
  }

  inline void add_maximal (const type_universe& U, std::vector<count_vector>& antichain,
                           count_vector candidate) {
    if (contains (U, antichain, candidate))
      return;
    antichain.erase (
        std::remove_if (antichain.begin (), antichain.end (),
                        [&] (const count_vector& existing) {
                          return dominates (U, candidate, existing);
                        }),
        antichain.end ());
    antichain.push_back (std::move (candidate));
  }

  inline bool sparse_less (const sparse_count_vector& a, const sparse_count_vector& b) {
    if (a.shared != b.shared)
      return a.shared < b.shared;
    return a.counts < b.counts;
  }

  inline std::vector<sparse_count_vector> to_sparse_sorted (
      const type_universe& U, const std::vector<count_vector>& dense) {
    std::vector<sparse_count_vector> out;
    out.reserve (dense.size ());
    for (const auto& d : dense)
      out.push_back (to_sparse (U, d));
    std::sort (out.begin (), out.end (), sparse_less);
    return out;
  }

  inline std::vector<sparse_count_vector> union_with (
      const std::vector<sparse_count_vector>& A, const std::vector<sparse_count_vector>& B) {
    type_universe U = make_universe (A, B, false);
    std::vector<count_vector> res;
    res.reserve (A.size () + B.size ());
    for (const auto& a : A)
      add_maximal (U, res, to_dense (U, a));
    for (const auto& b : B)
      add_maximal (U, res, to_dense (U, b));
    return to_sparse_sorted (U, res);
  }

  struct merge_plan {
      std::vector<unsigned> rank_u;
      std::vector<unsigned> rank_v;
  };

  using ordered_supports = std::vector<std::vector<type_id_t>>;

  inline std::vector<unsigned> rank_from_keys (const type_universe& U,
                                               const std::vector<long>& key) {
    std::vector<type_id_t> order (U.types.size ());
    std::iota (order.begin (), order.end (), 0);
    std::sort (order.begin (), order.end (), [&] (type_id_t a, type_id_t b) {
      if (key[a] != key[b])
        return key[a] > key[b];
      return U.types[a] < U.types[b];
    });
    std::vector<unsigned> rank (U.types.size ());
    for (unsigned i = 0; i < order.size (); ++i)
      rank[order[i]] = i;
    return rank;
  }

  inline void append_plan_orientations (std::vector<merge_plan>& plans,
                                        const std::vector<unsigned>& pos,
                                        const std::vector<unsigned>& neg) {
    if (plans.size () < ACACIA_SYMMETRY_INTERSECT_MAX_PLANS)
      plans.push_back ({pos, pos});
    if (plans.size () < ACACIA_SYMMETRY_INTERSECT_MAX_PLANS)
      plans.push_back ({neg, neg});
    if (plans.size () < ACACIA_SYMMETRY_INTERSECT_MAX_PLANS)
      plans.push_back ({pos, neg});
    if (plans.size () < ACACIA_SYMMETRY_INTERSECT_MAX_PLANS)
      plans.push_back ({neg, pos});
  }

  inline std::vector<merge_plan> intersect_merge_plans (const type_universe& U) {
    std::vector<merge_plan> plans;
    plans.reserve (ACACIA_SYMMETRY_INTERSECT_MAX_PLANS);

    std::vector<long> pos_sum (U.types.size (), 0);
    std::vector<long> neg_sum (U.types.size (), 0);
    for (type_id_t id = 0; id < U.types.size (); ++id) {
      pos_sum[id] = detail::type_sum (U.types[id]);
      neg_sum[id] = -pos_sum[id];
    }
    append_plan_orientations (plans, rank_from_keys (U, pos_sum), rank_from_keys (U, neg_sum));

    for (size_t b = 0; b < U.dimensions and
                       plans.size () < ACACIA_SYMMETRY_INTERSECT_MAX_PLANS;
         ++b) {
      std::vector<long> pos_coord (U.types.size (), 0);
      std::vector<long> neg_coord (U.types.size (), 0);
      for (type_id_t id = 0; id < U.types.size (); ++id) {
        pos_coord[id] = b < U.types[id].size () ? U.types[id][b] : 0;
        neg_coord[id] = -pos_coord[id];
      }
      append_plan_orientations (plans, rank_from_keys (U, pos_coord),
                                rank_from_keys (U, neg_coord));
    }
    return plans;
  }

  inline ordered_supports order_supports_by_plan (const count_vector& c,
                                                  const std::vector<merge_plan>& plans,
                                                  bool left_side) {
    ordered_supports ordered;
    ordered.reserve (plans.size ());
    for (const auto& plan : plans) {
      std::vector<type_id_t> support = c.support;
      const auto& rank = left_side ? plan.rank_u : plan.rank_v;
      std::sort (support.begin (), support.end (),
                 [&] (type_id_t a, type_id_t b) { return rank[a] < rank[b]; });
      ordered.push_back (std::move (support));
    }
    return ordered;
  }

  inline count_vector nw_corner_merge (const type_universe& U, const count_vector& u,
                                       const count_vector& v,
                                       const std::vector<type_id_t>& us,
                                       const std::vector<type_id_t>& vs,
                                       const posets::utils::vector_mm<value_t>& shared_meet) {
    count_vector out (shared_meet.size (), U.types.size ());
    out.shared = shared_meet;

    size_t i = 0, j = 0;
    count_t rem_u = i < us.size () ? u.counts[us[i]] : 0;
    count_t rem_v = j < vs.size () ? v.counts[vs[j]] : 0;
    while (i < us.size () and j < vs.size ()) {
      const count_t take = std::min (rem_u, rem_v);
      if (take > 0) {
        const type_id_t mid = U.meet_id[us[i]][vs[j]];
        assert (mid != invalid_type_id);
        out.add_count (mid, take);
      }
      rem_u -= take;
      rem_v -= take;
      if (rem_u == 0) {
        ++i;
        if (i < us.size ())
          rem_u = u.counts[us[i]];
      }
      if (rem_v == 0) {
        ++j;
        if (j < vs.size ())
          rem_v = v.counts[vs[j]];
      }
    }
    out.normalize_support ();
    return out;
  }

  inline std::vector<sparse_count_vector> intersect_with (
      const std::vector<sparse_count_vector>& A, const std::vector<sparse_count_vector>& B) {
    if (A.empty () or B.empty ())
      return {};

    type_universe U = make_universe (A, B, true);
    const auto plans = intersect_merge_plans (U);

    std::vector<count_vector> dense_A, dense_B;
    dense_A.reserve (A.size ());
    dense_B.reserve (B.size ());
    for (const auto& a : A)
      dense_A.push_back (to_dense (U, a));
    for (const auto& b : B)
      dense_B.push_back (to_dense (U, b));

    std::vector<ordered_supports> ordered_A, ordered_B;
    ordered_A.reserve (dense_A.size ());
    ordered_B.reserve (dense_B.size ());
    for (const auto& u : dense_A)
      ordered_A.push_back (order_supports_by_plan (u, plans, true));
    for (const auto& v : dense_B)
      ordered_B.push_back (order_supports_by_plan (v, plans, false));

    std::vector<count_vector> candidates;
    for (size_t ai = 0; ai < dense_A.size (); ++ai) {
      const auto& u = dense_A[ai];
      for (size_t bi = 0; bi < dense_B.size (); ++bi) {
        const auto& v = dense_B[bi];
        if (contains (U, candidates, u) or contains (U, candidates, v))
          continue;
        if (dominates (U, u, v)) {
          add_maximal (U, candidates, v);
          continue;
        }
        if (dominates (U, v, u)) {
          add_maximal (U, candidates, u);
          continue;
        }
        const auto shared_meet = detail::min_simd (u.shared, v.shared);
        for (size_t pi = 0; pi < plans.size (); ++pi)
          add_maximal (U, candidates,
                       nw_corner_merge (U, u, v, ordered_A[ai][pi], ordered_B[bi][pi],
                                        shared_meet));
      }
    }
    return to_sparse_sorted (U, candidates);
  }

}  // namespace symmetric_dense_downset
