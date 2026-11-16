#pragma once

#include <algorithm>
#include <limits>
#include <optional>

namespace acacia::k_schedule {
  // I05: for kmin <= k < kmax and kinc > 0, return min(kmax, k+kinc)
  // without overflow; otherwise there is no next ordinary bound. The final
  // delta can be smaller than kinc. Equivariant loops instead keep full steps
  // (see detail::checked_next_k in equivariant_k_bounded_safety_aut.hh).
  inline constexpr std::optional<long long>
  next (long long k, long long kmin, long long kmax, long long kinc) {
    if (k < kmin || k >= kmax || kinc <= 0)
      return std::nullopt;
    return k > std::numeric_limits<long long>::max () - kinc
               ? kmax : std::min (kmax, k + kinc);
  }
}  // namespace acacia::k_schedule
