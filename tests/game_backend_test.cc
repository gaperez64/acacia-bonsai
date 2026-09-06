#include "portfolio_arm.hh"
#include "solver/game_backend.hh"

#include <string_view>

#include <array>
#include <iostream>
#include <utility>

int main () {
  using acacia::game_backend;

  constexpr std::array names {
      std::pair {game_backend::backward, std::string_view {"backward"}},
      std::pair {game_backend::forward, std::string_view {"forward"}},
  };
  for (const auto& [backend, name] : names) {
    const auto parsed = acacia::parse_game_backend (name);
    if (not parsed.has_value () or *parsed != backend or
        acacia::game_backend_name (backend) != name) {
      std::cerr << "backend name and value are not inverses: " << name << '\n';
      return 1;
    }
  }

  constexpr std::array<std::string_view, 5> invalid {"", "Backward", "fwd", "backward ", "local"};
  for (std::string_view name : invalid)
    if (acacia::parse_game_backend (name).has_value ()) {
      std::cerr << "invalid backend name was accepted: " << name << '\n';
      return 1;
    }

  const auto parsed = parse_portfolio_arms (
      "real:any:backward,real:small:forward,unreal:formula:forward,"
      "unreal:automaton:forward");
  const std::vector<portfolio_arm> expected {
      {false, spot::postprocessor::Any, UNREAL_X_FORMULA, game_backend::backward},
      {false, spot::postprocessor::Small, UNREAL_X_FORMULA, game_backend::forward},
      {true, ACACIA_TRANSLATION_PREF, UNREAL_X_FORMULA, game_backend::forward},
      {true, ACACIA_TRANSLATION_PREF, UNREAL_X_AUTOMATON, game_backend::forward},
  };
  if (parsed.error != portfolio_arm_parse_error::none or parsed.arms != expected) {
    std::cerr << "valid portfolio did not parse in order\n";
    return 1;
  }

  const auto backend_pair =
      parse_portfolio_arms ("real:any:backward,real:any:forward");
  if (backend_pair.error != portfolio_arm_parse_error::none or
      backend_pair.arms.size () != 2) {
    std::cerr << "same transform with different backends was rejected\n";
    return 1;
  }

  const auto duplicate =
      parse_portfolio_arms ("real:any:backward,real:any:backward");
  if (duplicate.error != portfolio_arm_parse_error::duplicate or
      duplicate.spec != "real:any:backward") {
    std::cerr << "duplicate arm was not identified\n";
    return 1;
  }

  return 0;
}
