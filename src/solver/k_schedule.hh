#pragma once

#include <algorithm>
#include <limits>
#include <optional>

namespace acacia::k_schedule {
  inline constexpr std::optional<long long>
  next (long long k, long long kmin, long long kmax, long long kinc) {
    if (k < kmin || k >= kmax || kinc <= 0)
      return std::nullopt;
    return k > std::numeric_limits<long long>::max () - kinc
               ? kmax : std::min (kmax, k + kinc);
  }
}  // namespace acacia::k_schedule
