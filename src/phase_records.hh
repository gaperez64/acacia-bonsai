#pragma once

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// mallinfo2 is glibc 2.33 and later. Elsewhere (macOS, manylinux_2_28 wheels)
// records write "mallinfo2":null instead of inventing heap figures.
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
# include <malloc.h>
# define ACACIA_HAVE_MALLINFO2 1
#endif

namespace acacia {
  inline uint64_t phase_clock (clockid_t clock) noexcept {
    timespec t {};
    return clock_gettime (clock, &t) == 0
               ? uint64_t (t.tv_sec) * 1000000000ULL + uint64_t (t.tv_nsec)
               : 0;
  }

  struct phase_stamp {
      uint64_t wall = 0, cpu = 0;
  };
  struct phase_memory {
      long rss_kb, peak_rss_kb;
      size_t arena, hblkhd, uordblks, fordblks;
  };

  // One pipe packet is one atomic write, including its framing. The writer is
  // the only process that opens record paths. A full pipe only loses a sample.
  // PIPE_BUF is 4096 on Linux but 512 on macOS, so the line shrinks to fit;
  // a longer record is counted as dropped.
  struct phase_packet_header {
      pid_t pid;
      uint16_t length;
  };
  inline constexpr size_t phase_line_max = PIPE_BUF - sizeof (phase_packet_header) < 1024
                                               ? PIPE_BUF - sizeof (phase_packet_header)
                                               : 1024;
  struct phase_packet {
      pid_t pid;
      uint16_t length;
      char line[phase_line_max];
  };
  static_assert (sizeof (phase_packet) <= PIPE_BUF);
  // Single child writes; the parent reads only after waitpid has reaped it.
  // The snapshot survives SIGKILL and does not depend on pipe delivery.
  struct worker_record {
      unsigned index = 0;
      pid_t pid = -1;
      char requested_backend[32] = "unknown", effective_backend[32] = "unknown";
      char original_polarity[8] = "unknown", proof_polarity[8] = "unknown";
      char route[24] = "unknown", stage[48] = "startup", reason[64] = "none";
      unsigned long long sequence = 0, dropped = 0;
      bool terminal = false, verified = false, stopped = false;
  };
  inline worker_record*& active_worker_record () noexcept {
    static worker_record* record = nullptr;
    return record;
  }
  inline void worker_record_text (char* dst, size_t size, const char* value) noexcept {
    snprintf (dst, size, "%s", value ? value : "unknown");
  }
  template <size_t N>
  inline void worker_record_text (char (&dst)[N], const char* value) noexcept {
    worker_record_text (dst, N, value);
  }
  struct phase_record_state {
      pid_t pid = -1;
      pid_t owner = -1;
      pid_t writer = -1;
      int write_fd = -1;
      unsigned long long dropped = 0;
      bool enabled = false;
      size_t packet_bytes = sizeof (phase_packet);
  };
  inline phase_record_state& phase_record_process_state () noexcept {
    static phase_record_state state;
    return state;
  }
  inline void phase_records_init () noexcept {
    auto& state = phase_record_process_state ();
    const pid_t pid = getpid ();
    if (state.pid == pid)
      return;
    state.pid = pid;
    state.dropped = 0;
    state.enabled = state.write_fd >= 0;
  }
  inline bool phase_records_enabled () noexcept {
    phase_records_init ();
    return phase_record_process_state ().enabled;
  }
  inline void phase_records_drop () noexcept {
    auto& state = phase_record_process_state ();
    ++state.dropped;
    if (auto* worker = active_worker_record ())
      worker->dropped = state.dropped;
  }
  inline void phase_records_send (const char* line, size_t length) noexcept {
    auto& state = phase_record_process_state ();
    if (!state.enabled)
      return;
    if (length > std::min (sizeof (phase_packet::line),
                           state.packet_bytes - offsetof (phase_packet, line))) {
      phase_records_drop ();
      return;
    }
    phase_packet packet {};
    packet.pid = getpid ();
    packet.length = static_cast<uint16_t> (length);
    memcpy (packet.line, line, length);
    // The whole packet is within PIPE_BUF; O_NONBLOCK makes a full pipe an
    // immediate EAGAIN. SIGPIPE is ignored in every producer process.
    const size_t bytes = offsetof (phase_packet, line) + length;
    if (write (state.write_fd, &packet, bytes) != ssize_t (bytes))
      phase_records_drop ();
    if (auto* worker = active_worker_record ())
      worker->dropped = state.dropped;
  }
  inline void worker_event (worker_record& record, const char* event, const char* reason = "none",
                            int exit_code = -1, int signal = 0,
                            const char* telemetry = "pending") noexcept {
    if (!phase_records_enabled ())
      return;
    ++record.sequence;
    // All text is bounded metadata, never source formulas or input paths.
    // Escape it before placing it in the compact wire record.
    const auto quote = [] (const char* value, char* out, size_t size) {
      size_t used = 0;
      for (const unsigned char* p = reinterpret_cast<const unsigned char*> (value);
           *p && used + 7 < size; ++p) {
        if (*p < 32) {
          used += size_t (snprintf (out + used, size - used, "\\u%04x", *p));
        }
        else {
          if (*p == '"' || *p == '\\')
            out[used++] = '\\';
          out[used++] = char (*p);
        }
      }
      out[used] = 0;
    };
    char outcome[64] = "";
    if (exit_code >= 0 || signal > 0 || std::strcmp (event, "parent_terminal") == 0)
      snprintf (outcome, sizeof outcome, "\"exit_code\":%d,\"signal\":%d,", exit_code, signal);
    char escaped_reason[384];
    quote (reason, escaped_reason, sizeof escaped_reason);
    char line[1024];
    const int n =
        snprintf (line, sizeof line,
                  "{\"event\":\"%s\",\"worker\":%u,\"worker_pid\":%ld,\"seq\":%llu,"
                  "\"requested_backend\":\"%s\",\"effective_backend\":\"%s\","
                  "\"original_polarity\":\"%s\",\"proof_polarity\":\"%s\","
                  "\"route\":\"%s\",\"stage\":\"%s\",\"reason\":\"%s\","
                  "%s\"mono_ns\":%llu,"
                  "\"dropped_records\":%llu,\"telemetry\":\"%s\"}\n",
                  event, record.index, long (record.pid), record.sequence,
                  record.requested_backend, record.effective_backend, record.original_polarity,
                  record.proof_polarity, record.route, record.stage, escaped_reason, outcome,
                  (unsigned long long) phase_clock (CLOCK_MONOTONIC), record.dropped, telemetry);
    if (n > 0 && size_t (n) < sizeof line)
      phase_records_send (line, size_t (n));
    else
      phase_records_drop ();
  }
  inline void worker_stage (const char* stage) noexcept {
    if (auto* worker = active_worker_record ())
      worker_record_text (worker->stage, stage);
  }
  inline void worker_select_route (const char* route, const char* effective_backend = nullptr,
                                   const char* proof_polarity = nullptr) noexcept {
    if (auto* worker = active_worker_record ()) {
      worker_record_text (worker->route, route);
      worker_record_text (worker->stage, route);
      if (effective_backend)
        worker_record_text (worker->effective_backend, effective_backend);
      if (proof_polarity)
        worker_record_text (worker->proof_polarity, proof_polarity);
      worker_record_text (worker->reason, "none");
      worker->stopped = false;
      worker_event (*worker, "route_selected");
    }
  }
  inline void worker_route (const char* route, const char* effective_backend = nullptr,
                            const char* proof_polarity = nullptr) noexcept {
    worker_select_route (route, effective_backend, proof_polarity);
    if (auto* worker = active_worker_record ())
      worker_event (*worker, "route_start");
  }
  inline void worker_decline (const char* reason) noexcept {
    if (auto* worker = active_worker_record (); worker && !worker->stopped) {
      worker_record_text (worker->reason, reason);
      worker_event (*worker, "decline", worker->reason);
    }
  }
  inline void worker_stopped (const char* reason) noexcept {
    if (auto* worker = active_worker_record ()) {
      worker_record_text (worker->reason, reason);
      worker->stopped = true;
      worker_event (*worker, "route_stopped", worker->reason);
    }
  }
  inline void worker_verified (const char* original, const char* proof) noexcept {
    if (auto* worker = active_worker_record ()) {
      worker->verified = true;
      worker_record_text (worker->original_polarity, original);
      worker_record_text (worker->proof_polarity, proof);
      worker_record_text (worker->stage, "verified");
      worker_event (*worker, "verification_complete");
    }
  }
  inline void worker_terminal (int result, const char* reason = "none") noexcept {
    if (auto* worker = active_worker_record ()) {
      const char* final_reason =
          worker->stopped || std::strcmp (reason, "none") == 0 ? worker->reason : reason;
      worker_event (*worker, "terminal_result", final_reason, result);
      worker->terminal = true;
    }
  }
  inline void phase_records_summary (const char* arm) noexcept {
    if (!phase_records_enabled ())
      return;
    char line[256];
    const int n =
        snprintf (line, sizeof line,
                  "{\"arm\":\"%s\",\"phase\":\"record_summary\",\"dropped_records\":%llu}\n", arm,
                  phase_record_process_state ().dropped);
    if (n > 0 && size_t (n) < sizeof line)
      phase_records_send (line, size_t (n));
  }
  inline void phase_records_writer_loop (int read_fd, const char* dir,
                                         size_t packet_bytes) noexcept {
    struct sigaction ignore {};
    ignore.sa_handler = SIG_IGN;
    sigemptyset (&ignore.sa_mask);
    sigaction (SIGXFSZ, &ignore, nullptr);
    struct stat directory {};
    if (stat (dir, &directory) != 0 || !S_ISDIR (directory.st_mode))
      _exit (0);
    phase_packet packet {};
    size_t filled = 0;
    unsigned long long delivered = 0, failed = 0;
    while (true) {
      const size_t header_bytes = offsetof (phase_packet, line);
      if (filled >= header_bytes && (!packet.length || packet.length > sizeof packet.line ||
                                     header_bytes + packet.length > packet_bytes)) {
        ++failed;
        break;
      }
      const size_t wanted = filled < header_bytes ? header_bytes : header_bytes + packet.length;
      const ssize_t n =
          read (read_fd, reinterpret_cast<char*> (&packet) + filled, wanted - filled);
      if (n == 0)
        break;
      if (n < 0) {
        if (errno == EINTR)
          continue;
        break;
      }
      filled += size_t (n);
      if (filled < header_bytes || filled != header_bytes + packet.length)
        continue;
      filled = 0;
      if (!packet.length || packet.length > sizeof packet.line ||
          packet.line[packet.length - 1] != '\n') {
        ++failed;
        continue;
      }
      char path[4096];
      const int path_len = snprintf (path, sizeof path, "%s/%ld.jsonl", dir, long (packet.pid));
      if (path_len <= 0 || size_t (path_len) >= sizeof path) {
        ++failed;
        continue;
      }
      const int fd =
          open (path, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0644);
      if (fd < 0) {
        ++failed;
        continue;
      }
      struct stat target {};
      if (fstat (fd, &target) == 0 && S_ISREG (target.st_mode) &&
          write (fd, packet.line, packet.length) == packet.length)
        ++delivered;
      else
        ++failed;
      close (fd);
    }
    char path[4096], summary[256];
    const int path_len = snprintf (path, sizeof path, "%s/%ld.jsonl", dir, long (getpid ()));
    const int length = snprintf (summary, sizeof summary,
                                 "{\"event\":\"writer_summary\",\"delivered_records\":%llu,"
                                 "\"failed_records\":%llu,\"incomplete_packet\":%s}\n",
                                 delivered, failed, filled ? "true" : "false");
    if (path_len > 0 && size_t (path_len) < sizeof path && length > 0) {
      const int fd =
          open (path, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0644);
      struct stat target {};
      if (fd >= 0) {
        if (fstat (fd, &target) == 0 && S_ISREG (target.st_mode))
          (void) write (fd, summary, size_t (length));
        close (fd);
      }
    }
    _exit (0);
  }
  inline void phase_records_close () noexcept {
    auto& state = phase_record_process_state ();
    if (getpid () != state.owner)
      return;
    if (state.write_fd >= 0) {
      phase_records_summary ("legacy_parent");
      close (state.write_fd);
      state.write_fd = -1;
    }
    if (state.writer <= 0)
      return;
    const uint64_t end = phase_clock (CLOCK_MONOTONIC) + 100000000ULL;
    while (phase_clock (CLOCK_MONOTONIC) < end) {
      const pid_t got = waitpid (state.writer, nullptr, WNOHANG);
      if (got == state.writer || (got < 0 && errno == ECHILD)) {
        state.writer = -1;
        return;
      }
      timespec pause {0, 1000000};
      nanosleep (&pause, nullptr);
    }
    kill (state.writer, SIGKILL);
    // A writer blocked in uninterruptible filesystem I/O may not become
    // waitable even after SIGKILL. Never turn its failure into a parent hang.
    const uint64_t reap_end = phase_clock (CLOCK_MONOTONIC) + 10000000ULL;
    while (phase_clock (CLOCK_MONOTONIC) < reap_end) {
      const pid_t got = waitpid (state.writer, nullptr, WNOHANG);
      if (got == state.writer || (got < 0 && errno == ECHILD))
        break;
      timespec pause {0, 1000000};
      nanosleep (&pause, nullptr);
    }
    state.writer = -1;
  }
  inline pid_t phase_records_writer_pid () noexcept {
    return phase_record_process_state ().writer;
  }
  inline void phase_records_writer_reaped () noexcept {
    phase_record_process_state ().writer = -1;
  }
  inline void phase_records_start_writer (size_t packet_cap = sizeof (phase_packet),
                                          void (*writer_setup) () = nullptr) noexcept {
    const char* dir = getenv ("ACACIA_PHASE_RECORDS");
    if (!dir || !*dir)
      return;
    int fds[2];
#ifdef __linux__
    if (pipe2 (fds, O_CLOEXEC | O_NONBLOCK) != 0)
      return;
#else
    if (pipe (fds) != 0)
      return;
    for (const int fd : fds) {
      fcntl (fd, F_SETFD, FD_CLOEXEC);
      fcntl (fd, F_SETFL, fcntl (fd, F_GETFL) | O_NONBLOCK);
    }
#endif
    const long pipe_buf = fpathconf (fds[1], _PC_PIPE_BUF);
    // POSIX guarantees at least _POSIX_PIPE_BUF even if fpathconf fails.
    size_t packet_bytes =
        std::min (sizeof (phase_packet), size_t (pipe_buf > 0 ? pipe_buf : _POSIX_PIPE_BUF));
    packet_bytes = std::min (packet_bytes, std::max (packet_cap, size_t {_POSIX_PIPE_BUF}));
    const pid_t writer = fork ();
    if (writer == 0) {
      close (fds[1]);
      const int flags = fcntl (fds[0], F_GETFL);
      if (flags >= 0)
        fcntl (fds[0], F_SETFL, flags & ~O_NONBLOCK);
      if (writer_setup)
        writer_setup ();
      phase_records_writer_loop (fds[0], dir, packet_bytes);
    }
    close (fds[0]);
    if (writer < 0) {
      close (fds[1]);
      return;
    }
    auto& state = phase_record_process_state ();
    state.owner = getpid ();
    state.writer = writer;
    state.write_fd = fds[1];
    state.packet_bytes = packet_bytes;
    state.pid = state.owner;
    state.enabled = true;
    struct sigaction ignore {};
    ignore.sa_handler = SIG_IGN;
    sigemptyset (&ignore.sa_mask);
    sigaction (SIGPIPE, &ignore, nullptr);
    atexit (phase_records_close);
  }
  inline phase_stamp phase_start () noexcept {
    if (!phase_records_enabled ())
      return {};
    return {phase_clock (CLOCK_MONOTONIC), phase_clock (CLOCK_PROCESS_CPUTIME_ID)};
  }

  inline void phase_finish (const char* arm, const char* phase, phase_stamp start,
                            long long bdd_nodes = -1, long long bdd_cache = -1,
                            unsigned long long work_count = 0, unsigned long long size_bytes = 0,
                            const phase_memory* at_end = nullptr,
                            unsigned long long monitor_count = 0,
                            unsigned long long monitor_states = 0) noexcept {
    if (!start.wall || !phase_records_enabled ())
      return;
    const uint64_t wall = phase_clock (CLOCK_MONOTONIC);
    const uint64_t cpu = phase_clock (CLOCK_PROCESS_CPUTIME_ID);
    long rss = -1;
    if (FILE* status = std::fopen ("/proc/self/status", "r")) {
      char line[256];
      while (std::fgets (line, sizeof line, status))
        if (std::sscanf (line, "VmRSS: %ld kB", &rss) == 1)
          break;
      std::fclose (status);
    }
    rusage usage {};
    getrusage (RUSAGE_SELF, &usage);
#ifdef ACACIA_HAVE_MALLINFO2
    const auto heap = mallinfo2 ();
    const bool have_heap = true;
    const phase_memory fresh {rss,         usage.ru_maxrss, heap.arena,
                              heap.hblkhd, heap.uordblks,   heap.fordblks};
#else
    const bool have_heap = at_end != nullptr;
    const phase_memory fresh {rss, usage.ru_maxrss, 0, 0, 0, 0};
#endif
    const phase_memory memory = at_end ? *at_end : fresh;
    char heap_json[160] = "null";
    if (have_heap)
      std::snprintf (heap_json, sizeof heap_json,
                     "{\"arena\":%zu,\"hblkhd\":%zu,\"uordblks\":%zu,\"fordblks\":%zu}",
                     memory.arena, memory.hblkhd, memory.uordblks, memory.fordblks);
    char line[1024];
    const int n = std::snprintf (
        line, sizeof line,
        "{\"arm\":\"%s\",\"phase\":\"%s\",\"wall_ns\":%llu,\"cpu_ns\":%llu,"
        "\"rss_kb\":%ld,\"peak_rss_kb\":%ld,\"mallinfo2\":%s,"
        "\"bdd_nodes\":%lld,\"bdd_cache\":%lld,\"work_count\":%llu,"
        "\"size_bytes\":%llu,\"monitor_count\":%llu,\"monitor_states\":%llu}\n",
        arm, phase, (unsigned long long) (wall - start.wall),
        (unsigned long long) (cpu - start.cpu), memory.rss_kb, memory.peak_rss_kb, heap_json,
        bdd_nodes, bdd_cache, work_count, size_bytes, monitor_count, monitor_states);
    if (n <= 0 || size_t (n) >= sizeof line) {
      phase_records_drop ();
      return;
    }
    phase_records_send (line, size_t (n));
  }

  class phase_scope {
      const char* arm_;
      const char* phase_;
      phase_stamp start_;

    public:
      phase_scope (const char* arm, const char* phase) noexcept
        : arm_ (arm),
          phase_ (phase),
          start_ (phase_start ()) {
        worker_stage (phase);
      }
      ~phase_scope () { finish (); }
      phase_scope (const phase_scope&) = delete;
      phase_scope& operator= (const phase_scope&) = delete;
      void finish (long long bdd_nodes = -1, long long bdd_cache = -1) noexcept {
        if (start_.wall)
          phase_finish (arm_, phase_, start_, bdd_nodes, bdd_cache);
        start_ = {};
      }
  };
}
