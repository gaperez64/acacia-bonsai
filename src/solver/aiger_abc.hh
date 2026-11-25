#pragma once

#include "configuration.hh"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>

namespace acacia::synthesis {
  inline constexpr size_t aiger_abc_payload_bytes = 32 * 1024 * 1024;
  inline constexpr size_t aiger_abc_payload_limit (uint64_t memory_bytes) {
    return std::min<uint64_t> (aiger_abc_payload_bytes, memory_bytes / 16);
  }
  inline constexpr uint64_t aiger_abc_deadline (uint64_t invocation_deadline, uint64_t now) {
    const auto remaining = invocation_deadline
                               ? (invocation_deadline > now ? invocation_deadline - now : 0)
                               : UINT64_MAX;
    return now + std::min<uint64_t> (5000000000, remaining / 4);
  }
  struct abc_options {
      bool enabled = false;
      uint64_t deadline = 0, worker_rss_share = UINT64_MAX;
  };
  inline abc_options aiger_abc;
#if ACACIA_HAVE_ABC
  uint64_t aiger_abc_memory (const abc_options& options);
  // Failure leaves the encoded Spot circuit intact. The callback runs only in the child.
  void shrink_aiger (std::string& text, const abc_options& options,
                     const std::function<void ()>& before = {});
#endif
}
