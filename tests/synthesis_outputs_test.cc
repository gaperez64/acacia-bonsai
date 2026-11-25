#include "solver/synthesis_outputs.hh"

#include <iostream>
#include <stdexcept>

int main () {
  auto dict = spot::make_bdd_dict ();
  auto first = spot::make_twa_graph (dict), second = spot::make_twa_graph (dict);
  const auto x = bdd_ithvar (first->register_ap ("x"));
  const auto y = bdd_ithvar (second->register_ap ("y"));
  const std::vector<spot::const_twa_graph_ptr> machines {first, second};
  const std::vector<std::vector<std::string>> groups {{"x", "unused"}, {"y"}};
  const auto rejected = [&] (const auto& auts, const auto& outputs) {
    try {
      acacia::synthesis::validate_output_partition (auts, outputs);
    } catch (const std::logic_error& error) {
      return std::string (error.what ()).starts_with ("internal error:");
    }
    return false;
  };
  if (!rejected (machines, groups))  // Missing properties.
    return 1;
  acacia::synthesis::set_projected_outputs (first, x & y);
  acacia::synthesis::set_projected_outputs (second, x & y);
  acacia::synthesis::validate_output_partition (machines, groups);
  if (!rejected (machines, std::vector<std::vector<std::string>> {{"x"}}))
    return 1;
  spot::set_synthesis_outputs (first, x & y);  // Global cube on a local machine.
  if (!rejected (machines, groups))
    return 1;
  first->register_ap ("y");
  if (!rejected (machines, std::vector<std::vector<std::string>> {{"x", "y"}, {"y"}}))
    return 1;
  spot::set_synthesis_outputs (first, x);
  if (!rejected (machines, std::vector<std::vector<std::string>> {{"y"}, {"x"}}))
    return 1;
  spot::set_synthesis_outputs (first, !x);
  if (!rejected (machines, groups))  // Not a positive AP cube.
    return 1;
  spot::set_synthesis_outputs (first, x);
  acacia::synthesis::validate_output_partition (machines, groups);
  std::cout << "Output partition checks passed\n";
}
