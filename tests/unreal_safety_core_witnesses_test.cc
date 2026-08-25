#include "solver/unreal_safety_core_witnesses.hh"

#include <spot/tl/parse.hh>

#include <iostream>
#include <string>

namespace {

  spot::formula make_formula (size_t obligations, bool include_safety) {
    std::string nested = include_safety ? "(!g_0 | !g_1)" : "(r_s -> Fg_0)";
    for (size_t i = 0; i < obligations; ++i)
      nested += " & (r_" + std::to_string (i) + " -> Fg_0)";
    auto parsed = spot::parse_infix_psl (
        "G(" + nested + ") & G(r_extra -> Fg_1)");
    if (not parsed.f or not parsed.errors.empty ()) {
      parsed.format_errors (std::cerr);
      return {};
    }
    return parsed.f;
  }

}  // namespace

int main () {
  const auto oversized = make_formula (64, true);
  const auto witnesses =
      acacia::unreal_witnesses::make_safety_core_witnesses (oversized);
  if (witnesses.size () != 8)
    return 1;
  for (const auto& witness : witnesses)
    if (not witness.is (spot::op::And) or witness.size () != 2)
      return 2;

  const auto at_limit = make_formula (63, true);
  if (not acacia::unreal_witnesses::make_safety_core_witnesses (at_limit).empty ())
    return 3;

  const auto no_safety = make_formula (65, false);
  if (not acacia::unreal_witnesses::make_safety_core_witnesses (no_safety).empty ())
    return 4;

  if (not acacia::unreal_witnesses::make_safety_core_witnesses (oversized, 0).empty ())
    return 5;
  return 0;
}
