#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace acacia::unreal_witnesses {

  inline constexpr uint64_t default_attempt_ms = 250, default_total_ms = 1000;
  inline constexpr uint64_t ns_per_ms = 1000000;

  struct allowances {
      // Omitted options retain the incumbent deadline fractions. Explicit
      // absolute caps are global research controls, never input-dependent.
      std::optional<uint64_t> attempt_ms, total_ms;
  };

  inline uint64_t milliseconds_ns (uint64_t ms) {
    return std::min (ms, std::numeric_limits<uint64_t>::max () / ns_per_ms) * ns_per_ms;
  }

  struct budget {
      uint64_t until, per_attempt;
  };

  inline budget make_budget (uint64_t entry, uint64_t deadline, const allowances& options) {
    auto attempt = milliseconds_ns (options.attempt_ms.value_or (default_attempt_ms));
    auto total = milliseconds_ns (options.total_ms.value_or (default_total_ms));
    if (deadline) {
      const auto remaining = deadline > entry ? deadline - entry : 0;
      // Preserve the existing 5%/20% deadline bounds, including the original
      // solver's 80% reservation, even when research allowances are larger.
      attempt = options.attempt_ms ? std::min (attempt, remaining / 20) : remaining / 20;
      total = options.total_ms ? std::min (total, remaining / 5) : remaining / 5;
    }
    total = std::min (total, std::numeric_limits<uint64_t>::max () - entry);
    return {entry + total, attempt};
  }

}  // namespace acacia::unreal_witnesses
