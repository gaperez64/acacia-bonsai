#define POSETS_BBOX_STATS
#define POSETS_RANK_STATS
#define ACACIA_SYMMETRY_PROFILE 1
#define ACACIA_EQUIVARIANT_MIN_BLOCKS 0
#define ACACIA_EQUIVARIANT_MAX_SWEEP_CLIENTS 0

#ifdef NDEBUG
# error "The debug invariant test helper must compile with assertions enabled"
#endif

#include "solver/equivariant_k_bounded_safety_aut.hh"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <posets/downsets.hh>
#include <posets/downsets/bboxtree_backed.hh>
#include <posets/downsets/rank_bucketed_vector_backed.hh>

using state = posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>;
using configured_downset = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<state>;
namespace eq = acacia::solver_detail::equivariant;

bool equivariant_invariant_for_test (const configured_downset& f, const symmetry::group& G) {
  return eq::is_closed_under_generators (f, G);
}

namespace {

  bool expect (const std::string& what, bool condition) {
    if (not condition)
      std::cerr << "FAIL: " << what << "\n";
    return condition;
  }

  template <typename Downset>
  auto coordinates (const Downset& f) {
    std::vector<std::vector<VECTOR_ELT_T>> values;
    for (const auto& maximal : f) {
      values.emplace_back (maximal.size ());
      maximal.to_vector (std::span (values.back ()));
    }
    return values;
  }

  template <typename Downset>
  bool check_backend (const std::string& name, Downset& f, const symmetry::group& G,
                      bool expected_closed) {
    const auto before = coordinates (f);
    std::ostringstream rank_before, rank_after, bbox_before, bbox_after;
    posets::utils::rank_stats_print (rank_before);
    bool corners_before = false;
    if constexpr (requires { f.corners_built (); }) {
      corners_before = f.corners_built ();
      f.print_bbox_stats (bbox_before);
    }
    const bool closed = eq::is_closed_under_generators (f, G);
    posets::utils::rank_stats_print (rank_after);
    bool ok = expect (name + " invariant result", closed == expected_closed);
    ok &= expect (name + " preserves all rank counters", rank_before.str () == rank_after.str ());
    if constexpr (requires { f.corners_built (); }) {
      f.print_bbox_stats (bbox_after);
      ok &= expect (name + " preserves lazy corners", f.corners_built () == corners_before);
      ok &= expect (name + " preserves all bbox counters", bbox_before.str () == bbox_after.str ());
    }
    ok &= expect (name + " preserves maximal coordinates", coordinates (f) == before);
    return ok;
  }

  template <typename Downset>
  bool check_backend_cases (const std::string& name, const symmetry::group& G) {
    bool ok = true;
    for (bool symmetric : {true, false}) {
      std::vector<state> points;
      for (unsigned i = symmetric ? 0 : 1; i < 40; ++i) {
        posets::utils::vector_mm<VECTOR_ELT_T> values {
            static_cast<VECTOR_ELT_T> (i), static_cast<VECTOR_ELT_T> (39 - i)};
        points.emplace_back (values);
      }
      Downset f {std::move (points)};
      if constexpr (requires { f.corners_built (); })
        ok &= expect (name + " starts with lazy corners", not f.corners_built ());
      ok &= check_backend (name + " cold", f, G, symmetric);
      symmetry::group trivial;
      ok &= check_backend (name + " no generators", f, trivial, true);
      for (const auto& maximal : f) {
        const bool contains_maximal = f.contains (maximal);
        ok &= expect (name + " membership control", contains_maximal);
      }
      if constexpr (requires { f.corners_built (); })
        ok &= expect (name + " membership builds corners", f.corners_built ());
      ok &= check_backend (name + " warm", f, G, symmetric);
    }
    return ok;
  }

}  // namespace

bool check_equivariant_invariant_backends () {
  const auto old_bool_threshold = posets::vectors::bool_threshold;
  posets::vectors::bool_threshold = 2;
  symmetry::group G;
  G.gens.push_back ({1, 0});
  bool ok = check_backend_cases<posets::downsets::bboxtree_backed<state>> ("bbox", G);
  ok &= check_backend_cases<posets::downsets::rank_bucketed_vector_backed<state>> ("rank", G);
  posets::downsets::rank_bucketed_vector_backed<state> empty {std::vector<state> {}};
  ok &= check_backend ("rank empty", empty, G, true);
  posets::vectors::bool_threshold = old_bool_threshold;
  return ok;
}
