#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace acacia::obligation {
  // headroom (parity only): enforce the equal share when the scope is nearly full.
  enum class memory_share { equal, alone, headroom };

  constexpr uint64_t rss_share_for_limit (uint64_t limit, size_t workers,
                                         memory_share policy) noexcept {
    // Use at most half the invocation limit, capped at 8 GiB before division.
    // Only the helper may use the undivided share; ordinary worker budgets stay fixed.
    constexpr uint64_t address_cap = uint64_t {8} * 1024 * 1024 * 1024;
    const size_t divisor = policy == memory_share::alone ? 1 : std::max<size_t> (1, workers);
    return std::min (limit, address_cap) / divisor / 2;
  }
}
