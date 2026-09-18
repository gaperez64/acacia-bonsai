#pragma once

// B2 (sprint/symbolic-rows-selective-real, §8): a small, evidence-selected
// structural rule choosing between the forward and spot-guarded-sparse
// backends for the real-forward slot only. This moves only the selection
// decision, not formula translation: the caller supplies features already
// computed from the shared, already-preprocessed frozen automaton.
//
// Evidence (benchmarking/symbolic-rows-20260917/selector.md): raw states (N)
// does NOT separate the confirmed 5/5 sparse-real coverage gains on the
// frozen P4 list (N in [25, 2577]) from the confirmed regression on
// workstation_resupply_pb_3_pe_ (N=151, squarely inside that range;
// systematic under a 51 s cap: ~16 s forward vs ~49-51 s sparse). The
// fraction of boolean states (B/N, already-cached B over already-cached N,
// no extra scan) does separate them cleanly: every confirmed gain has
// B/N <= 24.3% (robot_grid_pb_5_5_pe_) while the regression has B/N=65.6%.
// A 30% threshold sits with margin on both sides.

#include "solver/game_backend.hh"

#include <cstddef>

namespace acacia {
  // Fixed-width counters from the final, preprocessed frozen automaton.
  // `available` is false when a feature could not be computed (overflow,
  // an unsupported mode, or the caller declining to compute it); the
  // selector then always keeps the original forward backend.
  struct RealBackendFeatures {
      std::size_t states = 0;
      std::size_t edges = 0;
      std::size_t inputs = 0;
      std::size_t outputs = 0;
      std::size_t boolean_states = 0;
      std::size_t max_out_degree = 0;
      bool available = false;
  };

  // `enabled` gates the whole selector (disabled selection keeps forward).
  // `max_boolean_percent` is the frozen threshold on 100*boolean_states/
  // states; see the file comment for how it was chosen. Both come from the
  // configuration registry (acacia_real_backend_selector /
  // acacia_real_backend_selector_max_boolean_percent), never from a
  // filename, source path or expected verdict.
  struct RealBackendRule {
      bool enabled = false;
      std::size_t max_boolean_percent = 30;
  };

  // Pure and side-effect-free: no logging, no I/O, no state. A negative
  // predicate, disabled selection, unavailable features, an empty automaton
  // or inconsistent counts (boolean_states > states) always resolve to the
  // original forward backend, never a silent fallback to a third choice.
  game_backend choose_real_backend (const RealBackendFeatures& features,
                                    const RealBackendRule& rule) noexcept;
} // namespace acacia
