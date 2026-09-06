#pragma once

#include <string_view>

#include <optional>

namespace acacia {

  // The value names the fixed-point backend that will run, not a preference
  // that another backend may pre-empt.
  enum class game_backend : unsigned char { backward, forward };

  inline const char* game_backend_name (game_backend backend) {
    switch (backend) {
      case game_backend::backward: return "backward";
      case game_backend::forward: return "forward";
    }
    return "unknown";
  }

  inline std::optional<game_backend> parse_game_backend (std::string_view name) {
    if (name == "backward")
      return game_backend::backward;
    if (name == "forward")
      return game_backend::forward;
    return std::nullopt;
  }

}  // namespace acacia
