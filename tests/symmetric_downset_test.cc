// Correctness tests for src/solver/symmetric_downset.hh: the canonical
// count-vector domination algebra used by the symmetry-quotient CPre
// (DIAGNOSIS.md, "Symmetry reduction: design + status"). No acacia/spot
// dependency -- pure combinatorics, cross-checked against brute-force
// enumeration of raw client-assignment pairings.
//
//  - contains/dominates: EXACT. Cross-checked against brute force (all n!
//    permutation pairings) that the max-flow-based test agrees exactly.
//  - union_with: EXACT (pairwise dominance filtering); sanity-checked.
//  - intersect_with: a capped, SOUND under-approximation (see the header's
//    doc comment for why this is fine for acacia's greatest-fixed-point
//    iteration). Here we check the property that actually matters:
//    SOUNDNESS -- every point intersect_with returns must be
//    dominated-by-or-equal-to some point in the TRUE exhaustive intersection
//    (brute force). We do NOT require full completeness (recovering every
//    true maximal point), only report it as an informational signal.

#include "solver/symmetric_downset.hh"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <random>
#include <vector>

using namespace symmetric_downset;

namespace {

  count_vector to_count (std::vector<type_t> raw, std::vector<value_t> shared) {
    count_vector c;
    c.shared = std::move (shared);
    for (auto& t : raw)
      c.counts[t]++;
    return c;
  }

  // Does there exist a permutation of `v_raw` making u_raw[k] >= v_raw[k]
  // pointwise for every client k? (ground truth for `dominates`.)
  bool brute_force_dominates (std::vector<type_t> u_raw, std::vector<type_t> v_raw) {
    assert (u_raw.size () == v_raw.size ());
    std::sort (v_raw.begin (), v_raw.end ());
    do {
      bool ok = true;
      for (size_t k = 0; k < u_raw.size () and ok; ++k)
        for (size_t b = 0; b < u_raw[k].size (); ++b)
          if (u_raw[k][b] < v_raw[k][b]) { ok = false; break; }
      if (ok)
        return true;
    } while (std::next_permutation (v_raw.begin (), v_raw.end ()));
    return false;
  }

  // Ground truth for a single-pair intersection: all n! meets, reduced to the
  // antichain of maximal elements (via our own, independently-validated
  // `dominates`).
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
    return union_with (all, {});
  }

  type_t rand_type (std::mt19937& rng, int B, int K) {
    type_t t (B);
    std::uniform_int_distribution<int> dist (-1, K);
    for (auto& x : t)
      x = dist (rng);
    return t;
  }

}  // namespace

int main () {
  int failures = 0;

  // --- Basic sanity ---
  {
    count_vector a = to_count ({{4, 3}, {4, 3}, {-1, -1}}, {0});
    count_vector b = to_count ({{4, 3}, {-1, -1}, {-1, -1}}, {0});
    assert (dominates (a, b));
    assert (not dominates (b, a));
  }
  {
    // Shared coordinates must gate domination even when the client part matches.
    count_vector a = to_count ({{1}}, {5});
    count_vector b = to_count ({{1}}, {6});
    assert (not dominates (a, b));
  }
  {
    count_vector strong = to_count ({{4}, {4}}, {0});
    count_vector weak = to_count ({{3}, {3}}, {0});
    auto res = union_with ({strong}, {weak});
    assert (res.size () == 1 and res[0].counts == strong.counts);
  }
  std::cout << "basic sanity: OK\n";

  // --- contains/dominates: exact cross-check vs brute force ---
  {
    std::mt19937 rng (42);
    int checks = 0, mismatches = 0;
    for (int trial = 0; trial < 1500; ++trial) {
      const int n = 1 + (int) (rng () % 6);
      const int B = 1 + (int) (rng () % 3);
      const int K = (int) (rng () % 4);
      std::vector<type_t> u_types, v_types;
      for (int i = 0; i < n; ++i) {
        u_types.push_back (rand_type (rng, B, K));
        v_types.push_back (rand_type (rng, B, K));
      }
      count_vector cu = to_count (u_types, {});
      count_vector cv = to_count (v_types, {});
      const bool got = dominates (cu, cv);
      const bool want = brute_force_dominates (u_types, v_types);
      ++checks;
      if (got != want) {
        ++mismatches;
        std::cerr << "dominates() MISMATCH at trial " << trial << ": got=" << got
                  << " want=" << want << "\n";
      }
    }
    std::cout << "dominates() exact cross-check: " << (checks - mismatches) << "/" << checks
              << " passed\n";
    failures += mismatches;
  }

  // --- intersect_with: soundness (single-pair) ---
  {
    std::mt19937 rng (7);
    int trials = 0, unsound = 0;
    long total_true_max = 0, total_recovered = 0;
    for (int trial = 0; trial < 400; ++trial) {
      const int n = 2 + (int) (rng () % 4);
      const int B = 1 + (int) (rng () % 3);
      const int K = (int) (rng () % 3);
      std::vector<type_t> u_types, v_types;
      for (int i = 0; i < n; ++i) {
        u_types.push_back (rand_type (rng, B, K));
        v_types.push_back (rand_type (rng, B, K));
      }
      std::vector<value_t> shared_u = {(value_t) (rng () % 4) - 1};
      std::vector<value_t> shared_v = {(value_t) (rng () % 4) - 1};
      std::vector<value_t> shared_meet = {std::min (shared_u[0], shared_v[0])};

      auto ours = intersect_with ({to_count (u_types, shared_u)}, {to_count (v_types, shared_v)});
      auto truth = brute_force_intersect (u_types, v_types, shared_meet);

      ++trials;
      for (auto& o : ours) {
        bool ok = false;
        for (auto& t : truth)
          if (dominates (t, o)) { ok = true; break; }
        if (not ok) {
          ++unsound;
          std::cerr << "intersect_with() UNSOUND at trial " << trial << "\n";
        }
      }
      total_true_max += (long) truth.size ();
      for (auto& t : truth)
        for (auto& o : ours)
          if (o.counts == t.counts and o.shared == t.shared) { ++total_recovered; break; }
    }
    std::cout << "intersect_with() soundness (single-pair): " << trials << " trials, "
              << unsound << " unsound points; completeness (informational) "
              << total_recovered << "/" << total_true_max << "\n";
    failures += unsound;
  }

  // --- intersect_with: soundness (multi-element antichains, the shape cpre_inplace uses) ---
  {
    std::mt19937 rng (99);
    int trials = 0, unsound = 0;
    for (int trial = 0; trial < 80; ++trial) {
      const int n = 2 + (int) (rng () % 3);
      const int bw = 1 + (int) (rng () % 2);
      const int K = (int) (rng () % 2);
      const int szA = 1 + (int) (rng () % 3);
      const int szB = 1 + (int) (rng () % 3);

      std::vector<std::pair<std::vector<type_t>, std::vector<value_t>>> A_raw, B_raw;
      std::vector<count_vector> A, B;
      for (int s = 0; s < szA; ++s) {
        std::vector<type_t> types;
        for (int i = 0; i < n; ++i) types.push_back (rand_type (rng, bw, K));
        std::vector<value_t> shared = {(value_t) (rng () % 3) - 1};
        A_raw.push_back ({types, shared});
        A.push_back (to_count (types, shared));
      }
      for (int s = 0; s < szB; ++s) {
        std::vector<type_t> types;
        for (int i = 0; i < n; ++i) types.push_back (rand_type (rng, bw, K));
        std::vector<value_t> shared = {(value_t) (rng () % 3) - 1};
        B_raw.push_back ({types, shared});
        B.push_back (to_count (types, shared));
      }
      A = union_with (A, {});
      B = union_with (B, {});

      auto ours = intersect_with (A, B);

      // Ground truth: union over all (a,b) raw pairs of their brute-force
      // intersections, re-filtered to the overall antichain of maximal points.
      std::vector<count_vector> truth;
      for (auto& [u_types, u_shared] : A_raw) {
        for (auto& [v_types, v_shared] : B_raw) {
          std::vector<value_t> shared_meet (u_shared.size ());
          for (size_t i = 0; i < u_shared.size (); ++i)
            shared_meet[i] = std::min (u_shared[i], v_shared[i]);
          for (auto& t : brute_force_intersect (u_types, v_types, shared_meet))
            truth.push_back (t);
        }
        truth = union_with (truth, {});
      }

      ++trials;
      for (auto& o : ours) {
        bool ok = false;
        for (auto& t : truth)
          if (dominates (t, o)) { ok = true; break; }
        if (not ok) {
          ++unsound;
          std::cerr << "intersect_with() UNSOUND (multi-element) at trial " << trial << "\n";
        }
      }
    }
    std::cout << "intersect_with() soundness (multi-element antichains): " << trials
              << " trials, " << unsound << " unsound points\n";
    failures += unsound;
  }

  if (failures == 0) {
    std::cout << "\nALL symmetric_downset TESTS PASSED\n";
    return 0;
  }
  std::cout << "\n" << failures << " FAILURES\n";
  return 1;
}
