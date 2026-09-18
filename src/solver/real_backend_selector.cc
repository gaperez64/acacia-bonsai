#include "solver/real_backend_selector.hh"

namespace acacia {
  game_backend choose_real_backend (const RealBackendFeatures& features,
                                    const RealBackendRule& rule) noexcept {
    if (!rule.enabled || !features.available || features.states == 0 ||
        features.boolean_states > features.states)
      return game_backend::forward;
    const auto percent = (features.boolean_states * 100) / features.states;
    return percent <= rule.max_boolean_percent ? game_backend::spot_guarded_sparse
                                                : game_backend::forward;
  }
} // namespace acacia
