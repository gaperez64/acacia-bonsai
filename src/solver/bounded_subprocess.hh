#pragma once

#include "phase_records.hh"

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
