#pragma once

#include "error_msg.hh"
#include "phase_records.hh"

#include <atomic>
#include <spot/tl/formula.hh>
#include <spot/twaalgos/aiger.hh>
#include <spot/twaalgos/mtdswa.hh>
#include <string>
#include <vector>

namespace acacia::obligation {
  enum class outcome { pending, real, unreal, inapplicable, exhausted, cancelled, error };
  enum class timing { mealy, moore };
  enum class eligibility { pending, eligible, inapplicable, error };

  // Absolute synthesis budget from fork, including preparation, export and checking.
  // Decision work has only the invocation/race bound. Neither policy is input-fitted.
  inline constexpr uint64_t synthesis_allowance_ns = 5000000000ULL;
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
      bool independent_checked = false;
      std::string controller;
  };

  const char* name (outcome value) noexcept;
  int exit_code (outcome value) noexcept;
  bool eligible (spot::formula objective) noexcept;
  void publish_eligibility (control* channel, spot::formula objective) noexcept;
  void record (const char* event, outcome value, const char* reason, uint64_t start_ns = 0,
               bool independent_checked = false, int worker = -1, pid_t pid = -1) noexcept;

  // Spot 2.16 uses acc=t/f for completed REAL/UNREAL. A null or empty object is
  // an error, never UNREAL. debug must stay -1; partial debug automata are unsafe.
  outcome completed (const spot::mtdswa_ptr& answer, bool decision_only) noexcept;
  // General LTL/controller product, independent of the MTBDD synthesis algorithm.
  bool check_controller (spot::formula objective, const spot::aig_ptr& controller,
                         bool moore = false);
  result solve (spot::formula objective, const std::vector<std::string>& inputs,
                const std::vector<std::string>& outputs, timing semantics = timing::mealy,
                bool synthesis = false, conversion convert = conversion::none,
                spot::formula controller_objective = {});

  // Same global address-space cap and cgroup-aware soft RSS share as native workers.
  bool limit_address_space () noexcept;
  uint64_t rss_share (size_t workers);
  uint64_t resident_bytes (pid_t pid) noexcept;
}
