#include "solver/encoded_family.hh"
#include "solver/symmetric_count.hh"
#include "tlsf_frontend.hh"

#include <algorithm>
#include <iostream>

int main (int argc, char** argv) {
  using namespace acacia;
  if (argc != 2)
    return 3;
  auto captured = tlsf_frontend::load_with_value_origins (argv[1]);
  if (not symmetry::certify_encoded_family (
          captured, 1u << 22, std::chrono::steady_clock::now () + std::chrono::seconds (15))) {
    std::cerr << "missing encoded certificate\n";
    return 3;
  }
  const auto& spec = captured.objective;
  const auto& cert = *spec.metadata.tlsf_declared_families.front ().logical->certificate;
  const auto solve = [&] (const symcount::limits& bound) {
    return symcount::solve_encoded (spec.formula, spec.inputs, spec.outputs, cert, bound, true);
  };
  using symcount::verdict;
  for (const auto& [bound, reason] : {
           std::pair {symcount::limits {.max_vertices = 1}, "input split limit"},
           std::pair {symcount::limits {.max_edges = 1}, "raw alphabet limit"},
           std::pair {symcount::limits {.deadline_mono_ns = 1}, "deadline"}}) {
    const auto r = solve (bound);
    if (r.outcome != verdict::declined or r.reason != reason) {
      std::cerr << "expected " << reason << ", got " << r.reason << '\n';
      return 3;
    }
  }
  const auto mismatched = symcount::solve_encoded ("true", spec.inputs, spec.outputs, cert);
  auto reordered = spec.inputs;
  std::reverse (reordered.begin (), reordered.end ());
  const auto wrong_order = symcount::solve_encoded (spec.formula, reordered, spec.outputs, cert);
  if (mismatched.outcome != verdict::declined or wrong_order.outcome != verdict::declined) {
    std::cerr << "accepted a different converted objective or partition\n";
    return 3;
  }
  const auto r = solve ({});
  if (not r.lifted or r.outcome == verdict::declined) {
    std::cerr << "audit declined: " << r.reason << '\n';
    return 2;
  }
  std::cout << "checked raw transitions and lifted strategy through the converted objective\n";
  return r.outcome == verdict::realizable ? 0 : 1;
}
