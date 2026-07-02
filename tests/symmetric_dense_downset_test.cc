// Correctness tests for src/solver/symmetric_dense_downset.hh.
//
// Dense/SIMD operations are an implementation optimization for the existing
// sparse count-vector algebra. Dominance and union must be exact-equivalent to
// the sparse implementation; intersection may select different bounded
// Northwest-corner plans in tie cases, so this test checks the required
// soundness property against brute-force raw pairings.

#include "solver/symmetric_dense_downset.hh"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <random>
#include <vector>

using symmetric_downset::count_vector;
using symmetric_downset::type_t;
using symmetric_downset::value_t;

namespace {

  count_vector to_count (std::vector<type_t> raw, std::vector<value_t> shared) {
    count_vector c;
    c.shared = std::move (shared);
    for (auto& t : raw)
      c.counts[t]++;
    return c;
  }

  type_t rand_type (std::mt19937& rng, int B, int K) {
    type_t t (B);
    std::uniform_int_distribution<int> dist (-1, K);
    for (auto& x : t)
      x = dist (rng);
    return t;
  }

  bool sparse_less (const count_vector& a, const count_vector& b) {
    if (a.shared != b.shared)
      return a.shared < b.shared;
    return a.counts < b.counts;
  }

  std::vector<count_vector> normalized (std::vector<count_vector> xs) {
    std::sort (xs.begin (), xs.end (), sparse_less);
    return xs;
  }

  std::vector<count_vector> brute_force_intersect (std::vector<type_t> u_raw,
                                                    std::vector<type_t> v_raw,
                                                    std::vector<value_t> shared_meet) {
    assert (u_raw.size () == v_raw.size ());
    std::sort (v_raw.begin (), v_raw.end ());
    std::vector<count_vector> all;
    do {
      std::vector<type_t> merged (u_raw.size ());
      for (size_t k = 0; k < u_raw.size (); ++k) {
        merged[k].resize (u_raw[k].size ());
        for (size_t b = 0; b < u_raw[k].size (); ++b)
          merged[k][b] = std::min (u_raw[k][b], v_raw[k][b]);
      }
      all.push_back (to_count (merged, shared_meet));
    } while (std::next_permutation (v_raw.begin (), v_raw.end ()));
    return symmetric_downset::union_with (all, {});
  }

}  // namespace

int main () {
  int failures = 0;

  {
    std::mt19937 rng (2026);
    for (int trial = 0; trial < 1500; ++trial) {
      const int n = 1 + (int) (rng () % 6);
      const int B = 1 + (int) (rng () % 4);
      const int K = (int) (rng () % 4);
      std::vector<type_t> u_types, v_types;
      for (int i = 0; i < n; ++i) {
        u_types.push_back (rand_type (rng, B, K));
        v_types.push_back (rand_type (rng, B, K));
      }
      auto u = to_count (u_types, {(value_t) ((int) (rng () % 5) - 2)});
      auto v = to_count (v_types, {(value_t) ((int) (rng () % 5) - 2)});
      auto U = symmetric_dense_downset::make_universe ({u}, {v}, false);
      const bool dense =
          symmetric_dense_downset::dominates (U, symmetric_dense_downset::to_dense (U, u),
                                              symmetric_dense_downset::to_dense (U, v));
      const bool sparse = symmetric_downset::dominates (u, v);
      if (dense != sparse) {
        std::cerr << "dense dominates mismatch at trial " << trial << "\n";
        ++failures;
      }
    }
    std::cout << "dense dominates exact-equivalence: OK\n";
  }

  {
    std::mt19937 rng (44);
    for (int trial = 0; trial < 250; ++trial) {
      const int n = 1 + (int) (rng () % 5);
      const int B = 1 + (int) (rng () % 3);
      const int K = (int) (rng () % 4);
      std::vector<count_vector> A, Bv;
      const int szA = 1 + (int) (rng () % 5);
      const int szB = 1 + (int) (rng () % 5);
      for (int s = 0; s < szA; ++s) {
        std::vector<type_t> types;
        for (int i = 0; i < n; ++i) types.push_back (rand_type (rng, B, K));
        A.push_back (to_count (types, {(value_t) ((int) (rng () % 5) - 2)}));
      }
      for (int s = 0; s < szB; ++s) {
        std::vector<type_t> types;
        for (int i = 0; i < n; ++i) types.push_back (rand_type (rng, B, K));
        Bv.push_back (to_count (types, {(value_t) ((int) (rng () % 5) - 2)}));
      }
      const auto sparse = normalized (symmetric_downset::union_with (A, Bv));
      const auto dense = normalized (symmetric_dense_downset::union_with (A, Bv));
      if (dense != sparse) {
        std::cerr << "dense union mismatch at trial " << trial << "\n";
        ++failures;
      }
    }
    std::cout << "dense union exact-equivalence: OK\n";
  }

  {
    std::mt19937 rng (77);
    int unsound = 0;
    for (int trial = 0; trial < 300; ++trial) {
      const int n = 2 + (int) (rng () % 4);
      const int B = 1 + (int) (rng () % 3);
      const int K = (int) (rng () % 3);
      std::vector<type_t> u_types, v_types;
      for (int i = 0; i < n; ++i) {
        u_types.push_back (rand_type (rng, B, K));
        v_types.push_back (rand_type (rng, B, K));
      }
      std::vector<value_t> shared_u = {(value_t) ((int) (rng () % 5) - 2)};
      std::vector<value_t> shared_v = {(value_t) ((int) (rng () % 5) - 2)};
      std::vector<value_t> shared_meet = {std::min (shared_u[0], shared_v[0])};
      auto dense = symmetric_dense_downset::intersect_with ({to_count (u_types, shared_u)},
                                                            {to_count (v_types, shared_v)});
      auto truth = brute_force_intersect (u_types, v_types, shared_meet);
      for (const auto& d : dense) {
        bool ok = false;
        for (const auto& t : truth)
          if (symmetric_downset::dominates (t, d)) {
            ok = true;
            break;
          }
        if (not ok) {
          ++unsound;
          std::cerr << "dense intersect unsound at trial " << trial << "\n";
        }
      }
    }
    failures += unsound;
    std::cout << "dense intersect soundness: " << (300 - unsound) << "/300 passed\n";
  }

  if (failures == 0) {
    std::cout << "\nALL symmetric_dense_downset TESTS PASSED\n";
    return 0;
  }
  std::cout << "\n" << failures << " FAILURES\n";
  return 1;
}

