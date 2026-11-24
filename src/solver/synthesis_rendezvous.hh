#pragma once

#include "error_msg.hh"

#include <atomic>
#include <cstdint>
#include <functional>
#include <spot/twa/fwd.hh>
#include <stdexcept>
#include <time.h>
#include <utility>

namespace acacia::synthesis {
  struct rendezvous_slot {
      std::atomic<int> verdict {-1};
      std::atomic<uint64_t> order {0};
      std::atomic<bool> proceed {false};
      std::atomic<bool> controller_complete {false};
      std::atomic<bool> suspend_ready {false};
  };
  static_assert (std::atomic<int>::is_always_lock_free);
  static_assert (std::atomic<uint64_t>::is_always_lock_free);
  static_assert (std::atomic<bool>::is_always_lock_free);

  using controller_builder = std::function<spot::twa_graph_ptr ()>;
  inline controller_builder* deferred_controller = nullptr;

  class controller_capture {
      controller_builder* previous;
    public:
      explicit controller_capture (controller_builder* target)
        : previous (std::exchange (deferred_controller, target)) {}
      ~controller_capture () { deferred_controller = previous; }
      controller_capture (const controller_capture&) = delete;
      controller_capture& operator= (const controller_capture&) = delete;
  };

  inline rendezvous_slot* current_slot = nullptr;
  inline std::atomic<uint64_t>* verdict_sequence = nullptr;
#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
  void test_worker_started (bool obligation, bool parity = false);
  void test_before_worker_protection ();
  void test_before_verdict (int& verdict);
  void test_after_selection ();
  void test_before_selection ();
  void test_verdict_published ();
  void test_after_extraction_cleanup ();
  void test_extraction_turn (int worker_pid, uint64_t quantum);
  void test_before_reap (int worker_pid);
  void test_before_publication ();
  bool test_extraction_grace (bool expired, uint64_t& deadline_mono_ns, int worker_pid);
#endif

  // A completed proof may race; only the selected extractor may run.
  // Internal library callers have no portfolio rendezvous.
  inline void await_selection (int verdict) {
    if (!current_slot || current_slot->proceed.load (std::memory_order_acquire))
      return;
#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
    test_before_verdict (verdict);
#endif
    current_slot->verdict.store (verdict, std::memory_order_relaxed);
    current_slot->order.store (verdict_sequence->fetch_add (1, std::memory_order_relaxed) + 1,
                               std::memory_order_release);
#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
    test_verdict_published ();
#endif
    while (!current_slot->proceed.load (std::memory_order_acquire)) {
      timespec pause {0, 1000000};
      nanosleep (&pause, nullptr);
    }
#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
    test_after_selection ();
#endif
  }

  inline void controller_closed (bool success) {
    if (!success)
      throw std::runtime_error ("failed to write controller");
    if (current_slot)
      current_slot->controller_complete.store (true, std::memory_order_release);
  }
}
