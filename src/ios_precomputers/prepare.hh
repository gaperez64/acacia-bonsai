#pragma once

namespace ios_precomputers {
  // Keep streaming precomputers lazy until the actioner consumes them. Other
  // precomputers retain their existing range interface.
  template <typename Precomputer>
  auto prepare (Precomputer precomputer) {
    if constexpr (requires { precomputer.for_each_input ([] (auto&&...) {}); })
      return precomputer;
    else
      return precomputer ();
  }
}
