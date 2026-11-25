#pragma once

#include "phase_records.hh"
#include "solver/scope_memory.hh"

#include <new>

namespace acacia::subprocess {
  inline constexpr int resource_exit = 4, error_exit = 5, deadline_exit = 6, cancelled_exit = 7;

  inline bool limit_memory (rlim_t bytes) {
#ifndef __SANITIZE_ADDRESS__
    // ASan reserves a large shadow mapping; the supervisor still checks RSS.
    rlimit limit {};
    if (getrlimit (RLIMIT_AS, &limit) != 0)
      return false;
    limit.rlim_cur = std::min (limit.rlim_cur, bytes);
    if (setrlimit (RLIMIT_AS, &limit) != 0)
      return false;
#endif
    return true;
  }

  inline constexpr rlim_t max_optional_memory = rlim_t {512} * 1024 * 1024;

  inline uint64_t optional_deadline (uint64_t invocation_deadline) {
    const auto start = phase_clock (CLOCK_MONOTONIC);
    const auto remaining = invocation_deadline
                               ? (invocation_deadline > start ? invocation_deadline - start : 0)
                               : UINT64_MAX;
    return start + std::min<uint64_t> (200000000, remaining / 20);
  }

  inline rlim_t optional_memory (size_t payload_size, uint64_t worker_rss_share,
                                 const char* scope_directory, const char*& cause) {
    rlim_t memory_bytes = 0;
    bool no_limit = false;
    const auto limit = scope_memory ("memory.max", scope_directory, &no_limit);
    constexpr uint64_t reserve = uint64_t {64} * 1024 * 1024;
    const auto rss_kb = resource_snapshot (getpid ()).rss_kb;
    const uint64_t resident = rss_kb > 0 ? uint64_t (rss_kb) * 1024 : 0;
    cause = "scope_memory";
    if (no_limit) {
      memory_bytes = max_optional_memory;
      cause = "resource";
    }
    else if (const auto used = scope_memory ("memory.current", scope_directory);
             limit && used) {
      const auto threshold = *limit - *limit / 8;
      const uint64_t headroom = threshold > *used ? threshold - *used : 0;
      // Cover a full resident copy-on-write plus transfer; leave half the rest unused.
      const auto margin = std::max (reserve, resident) + payload_size;
      if (headroom > margin)
        memory_bytes = std::min<uint64_t> (max_optional_memory, (headroom - margin) / 2);
      cause = "scope_headroom";
    }
    if (memory_bytes) {
      // Charge both resident sets and transfer against the supervisor's worker share.
      const auto parent_bytes = resident + payload_size;
      const auto share = worker_rss_share;
      const auto available = share > parent_bytes ? share - parent_bytes : 0;
      if (available < memory_bytes) {
        memory_bytes = available;
        cause = "rss_share";
      }
      if (!resident || memory_bytes < resident + reserve)
        memory_bytes = 0;
    }
    return memory_bytes;
  }

  struct result {
      pid_t child = -1;
      int code = error_exit;
      const char* cause = "none";
  };

  // Only a reaped normal exit transfers a result; child state and limits stay private.
  template <bool check_peak_rss = false, typename Action>
  result run (uint64_t deadline, rlim_t memory_bytes, Action action) {
    const auto parent = getpid ();
    const auto child = fork ();
    if (child == 0) {
#ifdef __linux__
      if (prctl (PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid () != parent)
        _exit (error_exit);
#endif
      worker_record local;
      active_worker_record () = &local;
      phase_record_process_state ().enabled = false;
      unsetenv ("ACACIA_SPOT_CAPTURE_DIR");
      try {
        if (!limit_memory (memory_bytes))
          _exit (error_exit);
        const int code = action ();
        if (local.stopped)
          _exit (std::strcmp (local.reason, "resource") == 0    ? resource_exit
                 : std::strcmp (local.reason, "deadline") == 0  ? deadline_exit
                 : std::strcmp (local.reason, "cancelled") == 0 ? cancelled_exit
                                                                : error_exit);
        _exit (code);
      } catch (const std::bad_alloc&) {
        _exit (resource_exit);
      } catch (...) {
        _exit (error_exit);
      }
    }
    if (child < 0)
      return {child, error_exit, "error"};
    auto* worker = active_worker_record ();
    if (worker)
      worker->descendant_pid = child;
    const char* cause = "none";
    int status = 0;
    rusage usage {};
    bool reaped = false;
    while (true) {
      const auto got = wait4 (child, &status, WNOHANG, &usage);
      if (got == child) {
        reaped = true;
        break;
      }
      if (got < 0 && errno != EINTR)
        cause = "error";
      else if (phase_clock (CLOCK_MONOTONIC) >= deadline)
        cause = "deadline";
      else if (resource_snapshot (child).rss_kb > long (memory_bytes / 1024))
        cause = "resource";
      if (std::strcmp (cause, "none") != 0) {
        kill (child, SIGKILL);
        pid_t got;
        do {
          got = wait4 (child, &status, 0, &usage);
        } while (got < 0 && errno == EINTR);
        reaped = got == child;
        break;
      }
      timespec pause {0, 1000000};
      nanosleep (&pause, nullptr);
    }
    if (worker) {
      descendant_record (*worker, child, reaped ? &usage : nullptr, status, !reaped);
      worker->descendant_pid = -1;
    }
    const int code = reaped && WIFEXITED (status) ? WEXITSTATUS (status) : error_exit;
    // Restriction prepasses retain master's deadline-only check after the final poll.
    if (phase_clock (CLOCK_MONOTONIC) >= deadline)
      cause = "deadline";
#ifdef __linux__
    else if (check_peak_rss && reaped && usage.ru_maxrss > long (memory_bytes / 1024))
      cause = "resource";
#endif
    else if (std::strcmp (cause, "none") == 0) {
      if (code == resource_exit)
        cause = "resource";
      else if (code == deadline_exit)
        cause = "deadline";
      else if (code == cancelled_exit)
        cause = "cancelled";
      else if (code == error_exit)
        cause = "error";
    }
    return {child, code, cause};
  }
}
