#pragma once

#include <string_view>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <time.h>
#include <vector>

namespace acacia {
  enum class stage_kind : uint64_t { unrestricted, memory, checking };

  inline stage_kind scheduling_kind (std::string_view stage) noexcept {
    if (stage == "target_check" || stage == "independent_check" || stage == "certificate-check" ||
        stage == "verification")
      return stage_kind::checking;
    if (stage == "startup" || stage == "dispatch" || stage == "verified" ||
        stage == "verified-attempt" || stage == "proof_binding" || stage == "source_provenance" ||
        stage == "subjob-summary" || stage.starts_with ("teardown"))
      return stage_kind::unrestricted;
    // Unclassified computation is conservatively memory-heavy. This includes
    // translation, reduction, action construction, BDD and antichain search.
    return stage_kind::memory;
  }

  // The generation and grant share one lock-free word: a grant for an old
  // stage can never admit a new request. Telemetry is deliberately independent.
  struct alignas (64) stage_slot {
      std::atomic<uint64_t> request {0};
      static constexpr uint64_t granted = 4;
      static constexpr uint64_t kind_mask = 3;
  };
  static_assert (std::atomic<uint64_t>::is_always_lock_free);
  inline stage_slot* active_stage_slot = nullptr;
  inline stage_kind active_stage_kind = stage_kind::unrestricted;
  inline bool stage_scheduling_enabled () noexcept { return active_stage_slot != nullptr; }

  inline stage_kind schedule_stage (stage_kind kind) noexcept {
    const auto previous = active_stage_kind;
    if (!active_stage_slot || previous == kind)
      return previous;
    active_stage_kind = kind;
    const auto old = active_stage_slot->request.load (std::memory_order_relaxed);
    const auto request = ((old & ~uint64_t {7}) + 8) | uint64_t (kind);
    active_stage_slot->request.store (request, std::memory_order_release);
    if (kind != stage_kind::unrestricted)
      while (
          !(active_stage_slot->request.load (std::memory_order_acquire) & stage_slot::granted)) {
        timespec pause {0, 1000000};
        nanosleep (&pause, nullptr);
      }
    return previous;
  }
  inline void schedule_stage (const char* name) noexcept {
    if (active_stage_slot)
      schedule_stage (scheduling_kind (name));
  }

  class scheduling_phase {
      stage_kind previous_;
      bool active_ = true;

    public:
      explicit scheduling_phase (const char* name, bool applicable = true) noexcept
        : previous_ (active_stage_kind),
          active_ (applicable) {
        if (applicable)
          schedule_stage (name);
      }
      scheduling_phase (const scheduling_phase&) = delete;
      void finish () noexcept {
        if (active_) {
          active_ = false;
          schedule_stage (previous_);
        }
      }
      ~scheduling_phase () { finish (); }
  };

  class stage_scheduler {
      struct arm_state {
          bool live = false, admitted = false, priority = false, exclusive = false;
          stage_kind kind = stage_kind::unrestricted;
          uint64_t started = 0, waiting = 0, ticket = 0;
      };
      std::vector<arm_state> arms_;
      size_t limit_;
      uint64_t slice_, ticket_ = 0;

      void wait (arm_state& arm, uint64_t now) {
        arm.admitted = false;
        arm.exclusive = false;
        arm.waiting = now;
        arm.ticket = ++ticket_;
      }

    public:
      // A global three-second lease is a responsiveness/fairness allowance,
      // independent of input size, identities, verdicts and measured timings.
      static constexpr uint64_t slice_ns = 3000000000ULL;
      stage_scheduler (size_t arms, size_t limit, uint64_t slice = slice_ns)
        : arms_ (arms),
          limit_ (std::max (size_t {1}, limit)),
          slice_ (slice) {}

      void update (size_t index, bool live, stage_kind kind, uint64_t now) {
        auto& arm = arms_[index];
        if (!live || kind == stage_kind::unrestricted) {
          arm = {};
          arm.live = live;
          return;
        }
        if (!arm.live || arm.kind == stage_kind::unrestricted)
          wait (arm, now);
        if (arm.kind != kind) {
          arm.priority = kind == stage_kind::checking;
          if (kind != stage_kind::checking)
            arm.exclusive = false;
        }
        arm.live = live;
        arm.kind = kind;
      }

      std::vector<bool> choose (uint64_t now) {
        const size_t heavy = std::count_if (arms_.begin (), arms_.end (), [] (const auto& arm) {
          return arm.live && arm.kind != stage_kind::unrestricted;
        });
        const bool waiting = std::any_of (arms_.begin (), arms_.end (), [] (const auto& arm) {
          return arm.live && arm.kind != stage_kind::unrestricted && !arm.admitted;
        });
        for (auto& arm : arms_)
          if (arm.admitted && now - arm.started >= slice_) {
            if (waiting)
              wait (arm, now);
            else {
              arm.started = now;
              arm.exclusive = false;
            }
          }
        // Aging overrides even checking priority. A continuously waiting arm
        // is selected within live-heavy-count * slice, plus parent polling and
        // process-stop acknowledgment; light work and exits release immediately.
        const auto bound = heavy > std::numeric_limits<uint64_t>::max () / slice_
                               ? std::numeric_limits<uint64_t>::max ()
                               : heavy * slice_;
        const auto overdue = [&] (const arm_state& arm) {
          return arm.live && arm.kind != stage_kind::unrestricted && !arm.admitted &&
                 now - arm.waiting >= bound;
        };
        const bool aged = std::any_of (arms_.begin (), arms_.end (), overdue);
        size_t priority = arms_.size ();
        if (!aged) {
          for (size_t i = 0; i < arms_.size (); ++i)
            if (arms_[i].admitted && arms_[i].exclusive) {
              priority = i;
              break;
            }
          if (priority == arms_.size ())
            for (size_t i = 0; i < arms_.size (); ++i)
              if (arms_[i].live && arms_[i].kind == stage_kind::checking && arms_[i].priority &&
                  (priority == arms_.size () || arms_[i].ticket < arms_[priority].ticket))
                priority = i;
        }
        std::vector<bool> selected (arms_.size (), false);
        if (priority != arms_.size ()) {
          selected[priority] = true;
        }
        else {
          std::vector<size_t> order;
          for (size_t i = 0; i < arms_.size (); ++i)
            if (arms_[i].live && arms_[i].kind != stage_kind::unrestricted)
              order.push_back (i);
          std::stable_sort (order.begin (), order.end (), [&] (size_t a, size_t b) {
            if (overdue (arms_[a]) != overdue (arms_[b]))
              return overdue (arms_[a]);
            if (arms_[a].admitted != arms_[b].admitted)
              return arms_[a].admitted;
            return arms_[a].ticket < arms_[b].ticket;
          });
          for (size_t i = 0; i < std::min (limit_, order.size ()); ++i)
            selected[order[i]] = true;
        }
        for (size_t i = 0; i < arms_.size (); ++i) {
          auto& arm = arms_[i];
          if (!arm.live || arm.kind == stage_kind::unrestricted) {
            selected[i] = arm.live;
            continue;
          }
          if (selected[i]) {
            if (!arm.admitted || (i == priority && !arm.exclusive))
              arm.started = now;
            arm.admitted = true;
            arm.priority = false;
            arm.exclusive = i == priority;
          }
          else if (arm.admitted)
            wait (arm, now);
        }
        return selected;
      }
  };
}
