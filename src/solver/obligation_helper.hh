#pragma once

#include "error_msg.hh"
#include "phase_records.hh"

#include <atomic>
#include <spot/tl/formula.hh>
#include <spot/twaalgos/mtdswa.hh>
#include <string>
#include <vector>

namespace acacia::obligation {
  enum class outcome { pending, real, unreal, inapplicable, exhausted, cancelled, error };
  enum class timing { mealy, moore };
  enum class eligibility { pending, eligible, inapplicable, error };

  // Fixed optional-work horizon, matching the existing 5 s reference allowance.
  // Neither this nor the per-worker RSS share is fitted to inputs or verdicts.
  inline constexpr uint64_t allowance_ns = 5000000000ULL;

  struct control {
      std::atomic<eligibility> eligible {eligibility::pending};
      std::atomic<outcome> result {outcome::pending};
      uint64_t start_ns = 0;
  };
  static_assert (std::atomic<eligibility>::is_always_lock_free);
  static_assert (std::atomic<outcome>::is_always_lock_free);

  struct result {
      outcome value = outcome::error;
      const char* reason = "error";
      bool same_library_checked = false;
  };

  const char* name (outcome value) noexcept;
  int exit_code (outcome value) noexcept;
  bool eligible (spot::formula objective) noexcept;
  void publish_eligibility (control* channel, spot::formula objective) noexcept;
  void record (const char* event, outcome value, const char* reason, uint64_t start_ns = 0,
               bool same_library_checked = false, int worker = -1, pid_t pid = -1) noexcept;

  // Spot 2.16 uses acc=t/f for completed REAL/UNREAL. A null or empty object is
  // an error, never UNREAL. debug must stay -1; partial debug automata are unsafe.
  outcome completed (const spot::mtdswa_ptr& answer, bool decision_only) noexcept;
  bool check_strategy (spot::formula objective, const spot::mtdswa_ptr& strategy);
  result solve (spot::formula objective, const std::vector<std::string>& inputs,
                const std::vector<std::string>& outputs, timing semantics = timing::mealy);

  // Same global address-space cap and cgroup-aware soft RSS share as native workers.
  bool limit_address_space () noexcept;
  uint64_t rss_share (size_t workers);
  uint64_t resident_bytes (pid_t pid) noexcept;
}
