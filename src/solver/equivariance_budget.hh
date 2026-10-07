#pragma once

#include "phase_records.hh"

#include <algorithm>
#include <optional>

namespace acacia::equivariance_budget {

  struct invocation_limits {
      std::optional<double> fraction;
      uint64_t deadline_ns = 0;
  };
  inline thread_local invocation_limits invocation;

  struct stopped {};

  class allowance {
      uint64_t outer_deadline_ns_ = 0;
      uint64_t total_ns_ = 0;
      uint64_t initial_remaining_ns_ = 0;
      mutable uint64_t consumed_ns_ = 0;
      mutable uint64_t deadline_ns_ = 0;

    public:
      allowance () = default;
      allowance (invocation_limits limits, uint64_t now) {
        if (limits.fraction) {
          outer_deadline_ns_ = limits.deadline_ns;
          initial_remaining_ns_ = limits.deadline_ns > now ? limits.deadline_ns - now : 0;
          total_ns_ = uint64_t (initial_remaining_ns_ * *limits.fraction);
          enter (now);
        }
      }
      [[nodiscard]] bool bounded () const { return outer_deadline_ns_ != 0; }
      [[nodiscard]] uint64_t deadline_ns () const { return deadline_ns_; }
      [[nodiscard]] uint64_t total_ns () const { return total_ns_; }
      [[nodiscard]] uint64_t initial_remaining_ns () const { return initial_remaining_ns_; }
      [[nodiscard]] uint64_t consumed_ns () const { return consumed_ns_; }
      void enter (uint64_t now) const {
        const uint64_t remaining = total_ns_ - consumed_ns_;
        const uint64_t outer_remaining = outer_deadline_ns_ > now ? outer_deadline_ns_ - now : 0;
        deadline_ns_ = now + std::min (remaining, outer_remaining);
      }
      void consume (uint64_t elapsed) const {
        consumed_ns_ += std::min (elapsed, total_ns_ - consumed_ns_);
      }
      void check () const {
        if (deadline_ns_ && phase_clock (CLOCK_MONOTONIC) >= deadline_ns_)
          throw stopped {};
      }
  };
  inline thread_local std::optional<allowance> invocation_allowance;
  inline thread_local const allowance* active = nullptr;
  inline void checkpoint () {
    if (active)
      active->check ();
  }

  class scope {
      const allowance* previous_ = active;
      const allowance& budget_;
      uint64_t entered_ns_;
      bool owns_consumption_;

    public:
      explicit scope (const allowance& budget)
        : budget_ {budget},
          entered_ns_ {
              budget.bounded () || phase_records_enabled () ? phase_clock (CLOCK_MONOTONIC) : 0},
          owns_consumption_ {budget.bounded () && previous_ != &budget} {
        if (owns_consumption_)
          budget.enter (entered_ns_);
        active = budget.bounded () ? &budget : nullptr;
      }
      ~scope () {
        // Charge the optional call, including exception cleanup, exactly once.
        // Ordinary backward and translation between subgames consume no allowance.
        if (owns_consumption_)
          budget_.consume (phase_clock (CLOCK_MONOTONIC) - entered_ns_);
        active = previous_;
      }
      [[nodiscard]] uint64_t entered_ns () const { return entered_ns_; }
      scope (const scope&) = delete;
      scope& operator= (const scope&) = delete;
  };

  inline void record (invocation_limits limits, uint64_t now, const allowance& budget) {
    auto* worker = active_worker_record ();
    if (!worker || !phase_records_enabled ())
      return;
    const uint64_t remaining = limits.deadline_ns > now ? limits.deadline_ns - now : 0;
    const uint64_t duration = budget.deadline_ns () > now ? budget.deadline_ns () - now : 0;
    char fraction[32] = "null";
    if (limits.fraction)
      snprintf (fraction, sizeof fraction, "%.17g", *limits.fraction);
    worker_event (*worker, "equivariance_budget", fraction);
    char line[512];
    const int n =
        snprintf (line, sizeof line,
                  "{\"phase\":\"equivariance_budget\",\"worker\":%u,\"pid\":%ld,"
                  "\"route\":\"equivariance\",\"fraction\":%s,\"remaining_ns\":%llu,"
                  "\"allowance_ns\":%llu,\"deadline_ns\":%llu,\"outer_deadline_ns\":%llu,"
                  "\"total_ns\":%llu,\"consumed_ns\":%llu,\"initial_remaining_ns\":%llu,"
                  "\"mono_ns\":%llu}\n",
                  worker->index, long (worker->pid), fraction, (unsigned long long) remaining,
                  (unsigned long long) duration, (unsigned long long) budget.deadline_ns (),
                  (unsigned long long) limits.deadline_ns, (unsigned long long) budget.total_ns (),
                  (unsigned long long) budget.consumed_ns (),
                  (unsigned long long) budget.initial_remaining_ns (), (unsigned long long) now);
    if (n > 0 && size_t (n) < sizeof line)
      phase_records_send (line, size_t (n));
    else
      phase_records_drop ();
  }

  inline void phase (const char* name) {
    worker_stage (name);
    if (auto* worker = active_worker_record ())
      worker_event (*worker, "equivariance_phase");
  }

}  // namespace acacia::equivariance_budget
