#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <cerrno>
#include <climits>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// mallinfo2 is glibc 2.33 and later. Elsewhere (macOS, manylinux_2_28 wheels)
// records write "mallinfo2":null instead of inventing heap figures.
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
#include <malloc.h>
#define ACACIA_HAVE_MALLINFO2 1
#endif

namespace acacia {
  inline uint64_t phase_clock (clockid_t clock) noexcept {
    timespec t {};
    return clock_gettime (clock, &t) == 0
        ? uint64_t (t.tv_sec) * 1000000000ULL + uint64_t (t.tv_nsec) : 0;
  }

  struct phase_stamp { uint64_t wall = 0, cpu = 0; };
  struct phase_memory {
    long rss_kb, peak_rss_kb;
    size_t arena, hblkhd, uordblks, fordblks;
  };

  // One pipe packet is one atomic write, including its framing. The writer is
  // the only process that opens record paths. A full pipe only loses a sample.
  struct phase_packet {
    pid_t pid;
    uint16_t length;
    char line[1024];
  };
  static_assert (sizeof (phase_packet) <= PIPE_BUF);
  struct phase_record_state {
    pid_t pid = -1;
    pid_t owner = -1;
    pid_t writer = -1;
    int write_fd = -1;
    unsigned long long dropped = 0;
    bool enabled = false;
  };
  inline phase_record_state& phase_record_process_state () noexcept {
    static phase_record_state state;
    return state;
  }
  inline void phase_records_init () noexcept {
    auto& state = phase_record_process_state ();
    const pid_t pid = getpid ();
    if (state.pid == pid) return;
    state.pid = pid;
    state.dropped = 0;
    state.enabled = state.write_fd >= 0;
  }
  inline bool phase_records_enabled () noexcept {
    phase_records_init ();
    return phase_record_process_state ().enabled;
  }
  inline void phase_records_send (const char* line, size_t length) noexcept {
    auto& state = phase_record_process_state ();
    if (!state.enabled || length > sizeof (phase_packet::line)) return;
    phase_packet packet {};
    packet.pid = getpid ();
    packet.length = static_cast<uint16_t> (length);
    memcpy (packet.line, line, length);
    // The whole packet is within PIPE_BUF; O_NONBLOCK makes a full pipe an
    // immediate EAGAIN. SIGPIPE is ignored in every producer process.
    if (write (state.write_fd, &packet, sizeof packet) != sizeof packet)
      ++state.dropped;
  }
  inline void phase_records_summary (const char* arm) noexcept {
    if (!phase_records_enabled ()) return;
    char line[256];
    const int n = snprintf (line, sizeof line,
        "{\"arm\":\"%s\",\"phase\":\"record_summary\",\"dropped_records\":%llu}\n",
        arm, phase_record_process_state ().dropped);
    if (n > 0 && size_t (n) < sizeof line) phase_records_send (line, size_t (n));
  }
  inline void phase_records_writer_loop (int read_fd, const char* dir) noexcept {
    struct sigaction ignore {};
    ignore.sa_handler = SIG_IGN;
    sigemptyset (&ignore.sa_mask);
    sigaction (SIGXFSZ, &ignore, nullptr);
#ifdef ACACIA_PHASE_RECORDS_TEST_HOOKS
    if (getenv ("ACACIA_TEST_RECORD_WRITER_STALL"))
      while (true) pause ();
#endif
    struct stat directory {};
    if (stat (dir, &directory) != 0 || !S_ISDIR (directory.st_mode)) _exit (0);
    phase_packet packet {};
    size_t filled = 0;
    while (true) {
      const ssize_t n = read (read_fd, reinterpret_cast<char*> (&packet) + filled,
                              sizeof packet - filled);
      if (n == 0) break;
      if (n < 0) {
        if (errno == EINTR) continue;
        break;
      }
      filled += size_t (n);
      if (filled != sizeof packet) continue;
      filled = 0;
      if (!packet.length || packet.length > sizeof packet.line ||
          packet.line[packet.length - 1] != '\n') continue;
      char path[4096];
      const int path_len = snprintf (path, sizeof path, "%s/%ld.jsonl", dir,
                                     long (packet.pid));
      if (path_len <= 0 || size_t (path_len) >= sizeof path) continue;
      const int fd = open (path, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW |
                                O_CLOEXEC | O_NONBLOCK, 0644);
      if (fd < 0) continue;
      struct stat target {};
      if (fstat (fd, &target) == 0 && S_ISREG (target.st_mode))
        (void) write (fd, packet.line, packet.length);
      close (fd);
    }
    _exit (0);
  }
  inline void phase_records_close () noexcept {
    auto& state = phase_record_process_state ();
    if (getpid () != state.owner) return;
    if (state.write_fd >= 0) {
      phase_records_summary ("legacy_parent");
      close (state.write_fd);
      state.write_fd = -1;
    }
    if (state.writer <= 0) return;
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
      if (got == state.writer || (got < 0 && errno == ECHILD)) break;
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
  inline void phase_records_start_writer () noexcept {
    const char* dir = getenv ("ACACIA_PHASE_RECORDS");
    if (!dir || !*dir) return;
    int fds[2];
#ifdef __linux__
    if (pipe2 (fds, O_CLOEXEC | O_NONBLOCK) != 0) return;
#else
    if (pipe (fds) != 0) return;
    for (const int fd : fds) {
      fcntl (fd, F_SETFD, FD_CLOEXEC);
      fcntl (fd, F_SETFL, fcntl (fd, F_GETFL) | O_NONBLOCK);
    }
#endif
    const pid_t writer = fork ();
    if (writer == 0) {
      close (fds[1]);
      const int flags = fcntl (fds[0], F_GETFL);
      if (flags >= 0) fcntl (fds[0], F_SETFL, flags & ~O_NONBLOCK);
      phase_records_writer_loop (fds[0], dir);
    }
    close (fds[0]);
    if (writer < 0) { close (fds[1]); return; }
    auto& state = phase_record_process_state ();
    state.owner = getpid ();
    state.writer = writer;
    state.write_fd = fds[1];
    state.pid = state.owner;
    state.enabled = true;
    struct sigaction ignore {};
    ignore.sa_handler = SIG_IGN;
    sigemptyset (&ignore.sa_mask);
    sigaction (SIGPIPE, &ignore, nullptr);
    atexit (phase_records_close);
  }
  inline phase_stamp phase_start () noexcept {
    if (!phase_records_enabled ()) return {};
    return {phase_clock (CLOCK_MONOTONIC), phase_clock (CLOCK_PROCESS_CPUTIME_ID)};
  }

  inline void phase_finish (const char* arm, const char* phase, phase_stamp start,
                            long long bdd_nodes = -1, long long bdd_cache = -1,
                            unsigned long long work_count = 0,
                            unsigned long long size_bytes = 0,
                            const phase_memory* at_end = nullptr,
                            unsigned long long monitor_count = 0,
                            unsigned long long monitor_states = 0) noexcept {
    if (!start.wall || !phase_records_enabled ()) return;
    const uint64_t wall = phase_clock (CLOCK_MONOTONIC);
    const uint64_t cpu = phase_clock (CLOCK_PROCESS_CPUTIME_ID);
    long rss = -1;
    if (FILE* status = std::fopen ("/proc/self/status", "r")) {
      char line[256];
      while (std::fgets (line, sizeof line, status))
        if (std::sscanf (line, "VmRSS: %ld kB", &rss) == 1) break;
      std::fclose (status);
    }
    rusage usage {};
    getrusage (RUSAGE_SELF, &usage);
#ifdef ACACIA_HAVE_MALLINFO2
    const auto heap = mallinfo2 ();
    const bool have_heap = true;
    const phase_memory fresh {rss, usage.ru_maxrss, heap.arena, heap.hblkhd,
                              heap.uordblks, heap.fordblks};
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
    const int n = std::snprintf (line, sizeof line,
        "{\"arm\":\"%s\",\"phase\":\"%s\",\"wall_ns\":%llu,\"cpu_ns\":%llu,"
        "\"rss_kb\":%ld,\"peak_rss_kb\":%ld,\"mallinfo2\":%s,"
        "\"bdd_nodes\":%lld,\"bdd_cache\":%lld,\"work_count\":%llu,"
        "\"size_bytes\":%llu,\"monitor_count\":%llu,\"monitor_states\":%llu}\n",
        arm, phase, (unsigned long long) (wall - start.wall),
        (unsigned long long) (cpu - start.cpu), memory.rss_kb, memory.peak_rss_kb,
        heap_json, bdd_nodes, bdd_cache, work_count, size_bytes,
        monitor_count, monitor_states);
    if (n <= 0 || size_t (n) >= sizeof line) {
      ++phase_record_process_state ().dropped;
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
      : arm_ (arm), phase_ (phase), start_ (phase_start ()) {}
    ~phase_scope () { finish (); }
    phase_scope (const phase_scope&) = delete;
    phase_scope& operator= (const phase_scope&) = delete;
    void finish (long long bdd_nodes = -1, long long bdd_cache = -1) noexcept {
      if (start_.wall) phase_finish (arm_, phase_, start_, bdd_nodes, bdd_cache);
      start_ = {};
    }
  };
}
