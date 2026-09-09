#pragma once

#include <string_view>

#include <optional>

namespace acacia {

  // The value names the fixed-point backend that will run, not a preference
  // that another backend may pre-empt.
  enum class game_backend : unsigned char { backward, forward, spot_guarded, spot_guarded_sparse };

  inline bool is_guarded_backend (game_backend backend) {
    return backend == game_backend::spot_guarded || backend == game_backend::spot_guarded_sparse;
  }

  // Independent of the rank-game algorithm. Compiling a provider never
  // selects it for an existing arm.
  enum class automaton_provider : unsigned char { frozen_graph, spot_lazy, spot_eager };
  enum class candidate_mode : unsigned char { only, fallback };

  inline const char* automaton_provider_name (automaton_provider provider) {
    switch (provider) {
      case automaton_provider::frozen_graph: return "frozen-graph";
      case automaton_provider::spot_lazy: return "spot-lazy";
      case automaton_provider::spot_eager: return "spot-eager";
    }
    return "unknown";
  }

  inline std::optional<automaton_provider> parse_automaton_provider (std::string_view name) {
    if (name == "frozen-graph") return automaton_provider::frozen_graph;
    if (name == "spot-lazy") return automaton_provider::spot_lazy;
    if (name == "spot-eager") return automaton_provider::spot_eager;
    return std::nullopt;
  }

  inline const char* candidate_mode_name (candidate_mode mode) {
    return mode == candidate_mode::only ? "only" : "fallback";
  }

  inline std::optional<candidate_mode> parse_candidate_mode (std::string_view name) {
    if (name == "only") return candidate_mode::only;
    if (name == "fallback") return candidate_mode::fallback;
    return std::nullopt;
  }

  inline game_backend synthesis_backend (game_backend backend, bool synthesis) {
    return synthesis ? game_backend::backward : backend;
  }

  inline automaton_provider synthesis_provider (automaton_provider provider, bool synthesis) {
    return synthesis ? automaton_provider::frozen_graph : provider;
  }

  inline const char* game_backend_name (game_backend backend) {
    switch (backend) {
      case game_backend::backward: return "backward";
      case game_backend::forward: return "forward";
      case game_backend::spot_guarded: return "spot-guarded";
      case game_backend::spot_guarded_sparse: return "spot-guarded-sparse";
    }
    return "unknown";
  }

  inline std::optional<game_backend> parse_game_backend (std::string_view name) {
    if (name == "backward")
      return game_backend::backward;
    if (name == "forward")
      return game_backend::forward;
    if (name == "spot-guarded")
      return game_backend::spot_guarded;
    if (name == "spot-guarded-sparse")
      return game_backend::spot_guarded_sparse;
    return std::nullopt;
  }

}  // namespace acacia
