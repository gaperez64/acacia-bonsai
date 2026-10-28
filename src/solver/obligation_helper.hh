#pragma once

#include "error_msg.hh"
#include "phase_records.hh"
#include "solver/obligation_memory.hh"

#include <atomic>
#include <optional>
#include <spot/tl/formula.hh>
#include <spot/twaalgos/aiger.hh>
#include <spot/twaalgos/mtdswa.hh>
#include <string>
#include <vector>

namespace acacia::obligation {
  enum class outcome { pending, real, unreal, inapplicable, exhausted, cancelled, error };
  enum class timing { mealy, moore };
  enum class eligibility { pending, eligible, inapplicable, error };

  enum class conversion { none, delay_outputs, undo_input_shift };

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
      spot::aig_ptr controller;
  };

  const char* name (outcome value) noexcept;
  int exit_code (outcome value) noexcept;
  bool eligible (spot::formula objective) noexcept;
  void publish_eligibility (control* channel, spot::formula objective) noexcept;
  void record (const char* event, outcome value, const char* reason, uint64_t start_ns = 0,
               int worker = -1, pid_t pid = -1) noexcept;

  // Spot 2.16 uses acc=t/f for completed REAL/UNREAL. A null or empty object is
  // an error, never UNREAL. debug must stay -1; partial debug automata are unsafe.
  outcome completed (const spot::mtdswa_ptr& answer, bool decision_only) noexcept;
  result solve (spot::formula objective, const std::vector<std::string>& inputs,
                const std::vector<std::string>& outputs, timing semantics = timing::mealy,
                bool synthesis = false, conversion convert = conversion::none);

  // The helper's fixed RSS threshold is supervised by the parent.
  bool limit_address_space () noexcept;
  uint64_t rss_share (size_t workers, memory_share policy = memory_share::equal);
  uint64_t resident_bytes (pid_t pid) noexcept;
  // Only this process's cgroup, or the supplied directory; no value if unbounded/unreadable.
  std::optional<uint64_t> scope_memory (const char* file,
                                        const char* directory = nullptr) noexcept;
  // A zero scope limit selects equal sharing; otherwise allow headroom up to the last eighth.
  bool rss_share_exceeded (uint64_t resident, uint64_t share, uint64_t scope_limit = 0,
                           const char* directory = nullptr) noexcept;
}
