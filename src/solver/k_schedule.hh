#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>

namespace acacia::k_schedule {
  enum class kind { linear, geometric, cheap_loss_adaptive, direct_max };

  struct loss_evidence {
      long long loss_ms;
      std::size_t peak_frontier;
      std::size_t loops;
      bool have_certificate;
  };

  // Experimental starting points from the handoff; these must not be treated
  // as tuned values.
  inline constexpr long long cheap_loss_ms_threshold = 50;
  inline constexpr std::size_t cheap_peak_frontier_threshold = 64;
  inline constexpr std::size_t cheap_loops_threshold = 8;

  inline constexpr bool is_cheap (const loss_evidence& evidence) {
    return evidence.loss_ms <= cheap_loss_ms_threshold
           || (evidence.peak_frontier <= cheap_peak_frontier_threshold
               && evidence.loops <= cheap_loops_threshold);
  }

  inline constexpr const char* name (kind value) {
    switch (value) {
      case kind::linear:
        return "linear";
      case kind::geometric:
        return "geometric";
      case kind::cheap_loss_adaptive:
        return "cheap_loss_adaptive";
      case kind::direct_max:
        return "direct_max";
    }
    return "unknown";
  }

  inline constexpr std::optional<long long>
  next (kind schedule, long long k, long long kmin, long long kmax,
        long long kinc, const loss_evidence& evidence) {
    if (k < kmin || k >= kmax)
      return std::nullopt;

    if (schedule == kind::cheap_loss_adaptive)
      schedule = is_cheap (evidence) ? kind::geometric : kind::linear;

    long long candidate = k;
    switch (schedule) {
      case kind::linear:
        if (kinc <= 0)
          return std::nullopt;
        candidate = k > std::numeric_limits<long long>::max () - kinc
                        ? kmax
                        : std::min (kmax, k + kinc);
        break;
      case kind::geometric:
        candidate = k > (std::numeric_limits<long long>::max () - 1) / 2
                        ? kmax
                        : std::min (kmax, 2 * k + 1);
        break;
      case kind::direct_max:
        candidate = kmax;
        break;
      case kind::cheap_loss_adaptive:
        // Replaced by one of the concrete schedules above.
        break;
    }

    if (candidate <= k || candidate > kmax)
      return std::nullopt;
    return candidate;
  }
}  // namespace acacia::k_schedule
