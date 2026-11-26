#include "solver/symmetric_count.hh"

#include <iostream>

int main () {
  using namespace acacia::symcount;
  using spot::formula;
  const auto check = [] (bool ok, const char* message) {
    if (not ok)
      std::cerr << message << '\n';
    return ok;
  };
  bool ok = true;

  // Test the count width without asking TLSF to expand a huge temporal formula.
  symmetry::indexed_family_hint family;
  for (unsigned i = 0; i < UINT16_MAX; ++i)
    family.members.push_back ("b_" + std::to_string (i));
  const auto at_limit = solve (formula::tt (), {}, family.members, {&family, 1});
  ok &= check (at_limit.outcome == verdict::realizable and at_limit.n == UINT16_MAX,
               "UINT16_MAX clients should fit");
  family.members.push_back ("b_" + std::to_string (UINT16_MAX));
  const auto overflow = solve (formula::tt (), {}, family.members, {&family, 1});
  ok &= check (overflow.outcome == verdict::declined and
                   overflow.reason == "index count exceeds uint16_t" and overflow.vertices == 0,
               "UINT16_MAX + 1 clients should decline before building the game");

  const std::vector<std::string> outputs {"g_0", "g_1"};
  family.members = outputs;
  const auto spec = formula::G (formula::And ({formula::ap ("g_0"), formula::ap ("g_1")}));
  ok &= check (solve (spec, {}, outputs, {&family, 1}).outcome == verdict::realizable,
               "the deadline probe should be realizable without a deadline");
  limits edge_bound;
  edge_bound.max_edges = 0;
  const auto exhausted = solve (spec, {}, outputs, {&family, 1}, edge_bound);
  ok &= check (exhausted.outcome == verdict::declined,
               "an exhausted edge budget should decline");
  limits bound;
  bound.deadline_mono_ns = 1;  // Already expired on CLOCK_MONOTONIC.
  const auto expired = solve (spec, {}, outputs, {&family, 1}, bound);
  ok &= check (expired.outcome == verdict::declined and expired.reason == "deadline" and
                   expired.vertices == 0,
               "an expired deadline should decline on entry");

  // Split workspace stays bounded even when candidate weights merge.
  family.members = {"g_0", "g_1", "g_2", "g_3"};
  std::vector<formula> grants;
  for (const auto& name : family.members)
    grants.push_back (formula::ap (name));
  const auto eventual = formula::F (formula::And (grants));
  edge_bound.max_edges = 5;
  const auto merged = solve (eventual, {}, family.members, {&family, 1}, edge_bound);
  ok &= check (merged.outcome == verdict::realizable and merged.edges == 4,
               "bounded duplicate successors should merge");
  edge_bound.max_edges = 3;
  const auto too_small = solve (eventual, {}, family.members, {&family, 1}, edge_bound);
  ok &= check (too_small.outcome == verdict::declined and too_small.reason == "split limit",
               "split workspace must be bounded before allocation");
  return ok ? 0 : 1;
}
