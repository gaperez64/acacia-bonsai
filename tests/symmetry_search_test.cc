#include "solver/symmetry.hh"

#include <cstdlib>
#include <iostream>

using symmetry::detail::iso_finder;
using symmetry::detail::out_edge;
using edges = std::vector<std::vector<out_edge>>;

namespace {

  void check (bool ok) {
    if (not ok) {
      std::cerr << "symmetry search test failed\n";
      std::abort ();
    }
  }

}  // namespace

int main () {
  // The first candidate for state 1 fails below it; state 2 must become
  // available again when the search retries the other branch.
  const edges A {{{1, 0, 1}}, {{1, 0, 2}}, {{1, 0, 0}}};
  const edges B {{{1, 0, 2}}, {{1, 0, 0}}, {{1, 0, 1}}};
  const std::vector<std::string> sig (3, "same");
  iso_finder f (3, A, B, sig, sig);
  f.assign (0, 0);
  f.assign (1, 1);
  const auto phi = f.phi, inv = f.inv;
  check (not f.search ());
  check (f.phi == phi and f.inv == inv);
  f.unassign (1, 1);
  check (f.search ());
  check (f.phi == std::vector<unsigned> ({0, 2, 1}));
  check (f.verify_full ());

  // Missing and exhausted classes must fail without changing the assignment.
  const edges empty (3);
  for (const std::vector<std::string>& other : {
           std::vector<std::string> {"root", "other", "x"},
           std::vector<std::string> {"root", "x", "other"}}) {
    const std::vector<std::string> source {"root", "x", "x"};
    iso_finder missing (3, empty, empty, source, other);
    missing.assign (0, 0);
    const auto saved_phi = missing.phi, saved_inv = missing.inv;
    check (not missing.search ());
    check (missing.phi == saved_phi and missing.inv == saved_inv);
    check (not missing.search ());
    check (missing.phi == saved_phi and missing.inv == saved_inv);
  }
  {
    const std::vector<std::string> source {"root", "missing", "x"};
    const std::vector<std::string> target {"root", "other", "x"};
    iso_finder missing (3, empty, empty, source, target);
    check (not missing.search ());
    check (missing.phi == std::vector<unsigned> (3, -1U));
    check (missing.inv == std::vector<unsigned> (3, -1U));
  }

  // Equal candidate counts keep ascending A/B order. Renumbering a directed
  // cycle has one isomorphism fixing its initial state, including after rollback.
  const edges cycle {{{1, 0, 1}}, {{1, 0, 2}}, {{1, 0, 3}}, {{1, 0, 0}}};
  const std::vector<std::string> same (4, "same");
  std::vector<unsigned> permutation {0, 1, 2, 3};
  do {
    edges renamed (4);
    for (unsigned q = 0; q < 4; ++q)
      renamed[permutation[q]].push_back ({1, 0, permutation[(q + 1) % 4]});
    iso_finder renamed_finder (4, cycle, renamed, same, same);
    renamed_finder.assign (0, permutation[0]);
    check (renamed_finder.search ());
    check (renamed_finder.phi == permutation);
    check (renamed_finder.verify_full ());
  } while (std::next_permutation (permutation.begin (), permutation.end ()));

  iso_finder ties (3, empty, empty, sig, sig);
  check (ties.search ());
  check (ties.phi == std::vector<unsigned> ({0, 1, 2}));

  // Same local signatures do not prove an isomorphism: final edge verification
  // must reject a three-cycle versus three self-loops and roll back completely.
  const edges loops {{{1, 0, 0}}, {{1, 0, 1}}, {{1, 0, 2}}};
  iso_finder nonisomorphic (3, A, loops, sig, sig);
  for (unsigned q = 0; q < 3; ++q) nonisomorphic.assign (q, q);
  check (not nonisomorphic.search ());
  for (unsigned q = 0; q < 3; ++q) nonisomorphic.unassign (q, q);
  check (not nonisomorphic.search ());
  check (nonisomorphic.phi == std::vector<unsigned> (3, -1U));
  check (nonisomorphic.inv == std::vector<unsigned> (3, -1U));

  std::cout << "symmetry search tests passed\n";
}
