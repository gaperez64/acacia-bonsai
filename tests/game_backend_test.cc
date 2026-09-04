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

  return 0;
}
