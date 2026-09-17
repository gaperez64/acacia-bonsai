#include "solver/real_backend_selector.hh"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
  std::size_t assertions = 0;
  void require (bool condition, const std::string& message) {
    ++assertions;
    if (!condition)
      throw std::runtime_error (message);
  }
  using acacia::choose_real_backend;
  using acacia::game_backend;
  using acacia::RealBackendFeatures;
  using acacia::RealBackendRule;

  RealBackendFeatures features (std::size_t states, std::size_t boolean_states,
                                bool available = true) {
    RealBackendFeatures f;
    f.states = states;
    f.boolean_states = boolean_states;
    f.available = available;
    return f;
  }

  void thresholds () {
    const RealBackendRule rule {true, 30};
    // Below threshold: sparse.
    require (choose_real_backend (features (100, 20), rule) == game_backend::spot_guarded_sparse,
             "20% < 30% picks sparse");
    // At threshold, inclusive: sparse.
    require (choose_real_backend (features (100, 30), rule) == game_backend::spot_guarded_sparse,
             "exactly 30% picks sparse (threshold is inclusive)");
    // Just above threshold: forward.
    require (choose_real_backend (features (100, 31), rule) == game_backend::forward,
             "31% > 30% keeps forward");
    // Confirmed evidence points from selector.md, exact integers.
    require (choose_real_backend (features (2577, 626), rule) == game_backend::spot_guarded_sparse,
             "robot_grid_pb_5_5_pe_: 24.3% picks sparse (confirmed B1 gain)");
    require (choose_real_backend (features (151, 99), rule) == game_backend::forward,
             "workstation_resupply_pb_3_pe_: 65.6% keeps forward (confirmed B1 regression)");
    require (choose_real_backend (features (295, 2), rule) == game_backend::spot_guarded_sparse,
             "SPIPureNext: 0.7% picks sparse (confirmed B1 gain)");
  }

  void disabled_and_unavailable () {
    const RealBackendRule enabled {true, 30};
    const RealBackendRule disabled {false, 30};
    // Disabled selection always keeps forward, even for an obviously-sparse case.
    require (choose_real_backend (features (100, 0), disabled) == game_backend::forward,
             "disabled selection keeps forward regardless of features");
    // Unavailable features always keep forward, even under an enabled rule.
    require (choose_real_backend (features (100, 0, /*available=*/false), enabled) ==
                 game_backend::forward,
             "unavailable features keep forward regardless of the rule");
  }

  void overflow_and_missing_metadata () {
    const RealBackendRule rule {true, 30};
    // Zero states: no valid ratio: keep forward, never divide by zero.
    require (choose_real_backend (features (0, 0), rule) == game_backend::forward,
             "zero states keeps forward (no valid ratio)");
    // Inconsistent/invalid metadata (more boolean states than states):
    // treat as missing rather than computing a nonsensical >100% ratio.
    require (choose_real_backend (features (10, 11), rule) == game_backend::forward,
             "boolean_states > states keeps forward (invalid metadata)");
    // A rule with the maximum valid percent (100) always picks sparse for
    // any consistent, available feature set.
    const RealBackendRule always {true, 100};
    require (choose_real_backend (features (10, 10), always) == game_backend::spot_guarded_sparse,
             "100% threshold with a fully-boolean automaton still picks sparse");
    // A rule with percent 0 only picks sparse for an automaton with zero
    // boolean states.
    const RealBackendRule never_unless_zero {true, 0};
    require (choose_real_backend (features (10, 0), never_unless_zero) ==
                 game_backend::spot_guarded_sparse,
             "0% threshold picks sparse only at exactly zero boolean states");
    require (choose_real_backend (features (10, 1), never_unless_zero) == game_backend::forward,
             "0% threshold keeps forward once boolean_states > 0");
  }

  void purity () {
    // Calling repeatedly with the same inputs is deterministic (pure): this
    // stands in for "a frozen choice across K", since the caller (not this
    // function) is what decides once and reuses the result for the whole K
    // schedule; the function itself must not depend on hidden/global state.
    const RealBackendRule rule {true, 30};
    const auto f = features (151, 99);
    const auto first = choose_real_backend (f, rule);
    for (int i = 0; i < 5; ++i)
      require (choose_real_backend (f, rule) == first,
               "repeated calls with identical inputs return an identical result");
  }
}

int main () {
  try {
    thresholds ();
    disabled_and_unavailable ();
    overflow_and_missing_metadata ();
    purity ();
    std::cout << "real-backend-selector: PASS, " << assertions << " assertions\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what () << '\n';
    return 1;
  }
}
