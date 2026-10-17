#pragma once

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
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
      uint64_t wall, cpu;
  };
  struct phase_memory {
      long rss_kb, peak_rss_kb;
      size_t arena, hblkhd, uordblks, fordblks;
  };

  struct resource_stamp {
      uint64_t mono_ns = 0, cpu_ns = 0, start_ticks = 0;
      long rss_kb = -1;
      uint64_t cpu_resolution_ns = 1;
      bool have_cpu = false;
  };
  struct accounting_entry {
      uint64_t id = 0, parent = 0;
      unsigned subjob = 0, run_id = 0;
      phase_stamp start, segment_start;
      char name[64] {};
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
      bool r_prepass = true, equivariance = true;
      bool terminal = false, verified = false, stopped = false;
      unsigned long long prepass = 0, declines = 0, stops = 0;
      unsigned run_id = 0;
      bool run_active = false, run_weakening = false;
      phase_stamp run_start {};
      uint64_t deadline_ns = 0;
      unsigned subjob = 0, subjobs = 0, subjobs_executed = 0;
      long long k = -1;
      unsigned long long stage_id = 0;
      uint64_t stage_start_ns = 0, stage_start_cpu_ns = 0;
      char observed_stage[48] = "unknown";
      uint64_t start_mono_ns = 0, next_accounting_id = 0;
      unsigned accounting_depth = 0;
      accounting_entry accounting[16];
      resource_stamp boundaries[5];
      resource_stamp interruption;
      unsigned boundary_committed[5] {};
      pid_t descendant_pid = -1;
      uint64_t descendant_user_ns = 0, descendant_sys_ns = 0;
      bool descendant_usage_complete = true;
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
      long clock_ticks = 0, page_size = 0;
      size_t packet_bytes = sizeof (phase_packet);
  };
  inline phase_record_state& phase_record_process_state () noexcept {
    static phase_record_state state;
    return state;
  }
  inline void phase_records_init () noexcept {
    auto& state = phase_record_process_state ();
    if (state.write_fd < 0)
      return;
    const pid_t pid = getpid ();
    if (state.pid == pid)
      return;
    state.pid = pid;
    state.dropped = 0;
    state.enabled = state.write_fd >= 0;
  }
  inline bool phase_records_enabled () noexcept {
    if (!phase_record_process_state ().enabled)
      return false;
    phase_records_init ();
    return phase_record_process_state ().enabled;
  }
  inline void phase_records_warn () noexcept {
    static bool warned = false;
    if (!warned) {
      warned = true;
      constexpr char message[] =
          "acacia: phase recording failed; accounting disabled or incomplete\n";
      if (signal (SIGPIPE, SIG_IGN) != SIG_ERR)
        (void) write (STDERR_FILENO, message, sizeof message - 1);
    }
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
  // One /proc read supplies process-only CPU, RSS, state and PID start identity.
  // In particular, child CPU (cutime/cstime) is deliberately excluded.
  // This routine is also used by the parent's signal handler. It uses only
  // async-signal-safe syscalls and scalar parsing; conversion factors are
  // cached at recorder startup, before any worker can be published.
  inline resource_stamp resource_snapshot (pid_t pid) noexcept {
    resource_stamp stamp;
    stamp.mono_ns = phase_clock (CLOCK_MONOTONIC);
#ifdef __linux__
    char path[64], digits[24], data[4096];
    size_t used = 0, count = 0;
    for (const char c : "/proc/")
      if (c)
        path[used++] = c;
    unsigned long number = pid > 0 ? static_cast<unsigned long> (pid) : 0;
    do {
      digits[count++] = char ('0' + number % 10);
      number /= 10;
    } while (number);
    while (count)
      path[used++] = digits[--count];
    for (const char c : "/stat")
      path[used++] = c;
    const int fd = open (path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd >= 0) {
      const ssize_t n = read (fd, data, sizeof data - 1);
      close (fd);
      if (n > 0) {
        data[n] = 0;
        char* field = nullptr;
        for (char* at = data; *at; ++at)
          if (*at == ')')
            field = at + 1;
        uint64_t user = 0, sys = 0, pages = 0;
        bool have_pages = false;
        for (unsigned i = 3; field && i <= 24; ++i) {
          while (*field == ' ')
            ++field;
          if (!*field)
            break;
          if (i == 14 || i == 15 || i == 22 || i == 24) {
            const bool valid = *field >= '0' && *field <= '9';
            uint64_t value = 0;
            while (*field >= '0' && *field <= '9')
              value = value * 10 + unsigned (*field++ - '0');
            if (i == 14)
              user = value;
            if (i == 15)
              sys = value;
            if (i == 22 && valid)
              stamp.start_ticks = value;
            if (i == 24 && valid) {
              pages = value;
              have_pages = true;
            }
          }
          while (*field && *field != ' ')
            ++field;
        }
        const auto& state = phase_record_process_state ();
        if (state.clock_ticks > 0 && stamp.start_ticks) {
          stamp.cpu_resolution_ns = 1000000000ULL / state.clock_ticks;
          stamp.cpu_ns = (user + sys) * stamp.cpu_resolution_ns;
          stamp.have_cpu = true;
        }
        if (have_pages && state.page_size > 0)
          stamp.rss_kb = long (pages * (state.page_size / 1024));
      }
    }
#endif
    if (pid == getpid ()) {
      stamp.cpu_ns = phase_clock (CLOCK_PROCESS_CPUTIME_ID);
      stamp.have_cpu = stamp.cpu_ns != 0;
      stamp.cpu_resolution_ns = 1;
    }
    return stamp;
  }

  // Final records bypass the lossy producer pipe. Supervisors use this
  // path for specs, cancellation and wait4 results. A separate file avoids racing the writer's
  // appends.
  inline void phase_records_final (const char* line, size_t length) noexcept {
    const char* final_directory = getenv ("ACACIA_PHASE_RECORDS");
    if (!final_directory || !phase_records_enabled ())
      return;
    char path[4096];
    const int n =
        snprintf (path, sizeof path, "%s/%ld-final.jsonl", final_directory, long (getpid ()));
    if (n <= 0 || size_t (n) >= sizeof path) {
      phase_records_drop ();
      return;
    }
    const int fd =
        open (path, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0644);
    struct stat target {};
    bool ok = fd >= 0 && fstat (fd, &target) == 0 && S_ISREG (target.st_mode);
    size_t offset = 0;
    while (ok && offset < length) {
      const ssize_t wrote = write (fd, line + offset, length - offset);
      if (wrote < 0 && errno == EINTR)
        continue;
      if (wrote <= 0) {
        ok = false;
        break;
      }
      offset += size_t (wrote);
    }
    if (fd >= 0)
      close (fd);
    if (!ok) {
      phase_records_drop ();
      phase_records_warn ();
    }
  }

  inline void resource_event (worker_record& record, const char* boundary, bool final = false,
                              const resource_stamp* saved = nullptr) noexcept {
    if (!phase_records_enabled ())
      return;
    const auto sample = saved ? *saved : resource_snapshot (record.pid);
    char cpu[32] = "null", rss[32] = "null", line[768];
    if (sample.have_cpu)
      snprintf (cpu, sizeof cpu, "%llu", (unsigned long long) sample.cpu_ns);
    if (sample.rss_kb >= 0)
      snprintf (rss, sizeof rss, "%ld", sample.rss_kb);
    const int n =
        snprintf (line, sizeof line,
                  "{\"event\":\"worker_resource\",\"boundary\":\"%s\",\"worker_pid\":%ld,"
                  "\"start_mono_ns\":%llu,\"start_ticks\":%llu,\"mono_ns\":%llu,"
                  "\"process_cpu_ns\":%s,\"cpu_resolution_ns\":%llu,\"rss_kb\":%s,"
                  "\"dropped_records\":%llu,\"recovered\":%s}\n",
                  boundary, long (record.pid), (unsigned long long) record.start_mono_ns,
                  (unsigned long long) sample.start_ticks, (unsigned long long) sample.mono_ns,
                  cpu, (unsigned long long) sample.cpu_resolution_ns, rss, record.dropped,
                  final && saved ? "true" : "false");
    if (n <= 0 || size_t (n) >= sizeof line) {
      phase_records_drop ();
      return;
    }
    if (final)
      phase_records_final (line, size_t (n));
    else
      phase_records_send (line, size_t (n));
  }

  // Each segment belongs to the current leaf only. A nested phase closes its
  // parent's segment at the same wall/CPU boundary at which the child starts.
  // Parent segments resume at child completion, so these intervals never overlap.
  inline void accounting_segment (worker_record& worker, const accounting_entry& entry,
                                  phase_stamp end = {}, bool final = false,
                                  uint64_t reaped_ns = 0) noexcept {
    if (!entry.segment_start.wall)
      return;
    char line[640], wall[32] = "null", cpu[32] = "null", end_cpu[32] = "null";
    if (end.wall) {
      snprintf (wall, sizeof wall, "%llu",
                (unsigned long long) (end.wall - entry.segment_start.wall));
      snprintf (cpu, sizeof cpu, "%llu", (unsigned long long) (end.cpu - entry.segment_start.cpu));
      snprintf (end_cpu, sizeof end_cpu, "%llu", (unsigned long long) end.cpu);
    }
    const int n = snprintf (
        line, sizeof line,
        "{\"event\":\"accounting_segment%s\",\"worker_pid\":%ld,\"phase_id\":%llu,"
        "\"parent_phase_id\":%llu,\"phase\":\"%s\",\"entry_ns\":%llu,\"entry_cpu_ns\":%llu,"
        "\"mono_ns\":%llu,\"process_cpu_ns\":%s,\"wall_ns\":%s,\"cpu_ns\":%s,\"dropped_records\":%"
        "llu}\n",
        end.wall ? "" : "_censored", long (worker.pid), (unsigned long long) entry.id,
        (unsigned long long) entry.parent, entry.name,
        (unsigned long long) entry.segment_start.wall,
        (unsigned long long) entry.segment_start.cpu,
        (unsigned long long) (end.wall ? end.wall : reaped_ns), end_cpu, wall, cpu,
        worker.dropped);
    if (n <= 0 || size_t (n) >= sizeof line) {
      phase_records_drop ();
      return;
    }
    if (final)
      phase_records_final (line, size_t (n));
    else
      phase_records_send (line, size_t (n));
  }

  inline void worker_reaped (worker_record& record, const rusage* usage, int status,
                             const char* reason, uint64_t reaped_mono_ns = 0) noexcept {
    if (!phase_records_enabled ())
      return;
    char user[32] = "null", sys[32] = "null", peak[32] = "null", self_cpu[32] = "null", line[768];
    if (usage) {
      snprintf (user, sizeof user, "%llu",
                (unsigned long long) usage->ru_utime.tv_sec * 1000000000ULL +
                    usage->ru_utime.tv_usec * 1000ULL);
      snprintf (sys, sizeof sys, "%llu",
                (unsigned long long) usage->ru_stime.tv_sec * 1000000000ULL +
                    usage->ru_stime.tv_usec * 1000ULL);
      snprintf (peak, sizeof peak, "%ld", usage->ru_maxrss);
      const uint64_t inclusive =
          uint64_t (usage->ru_utime.tv_sec + usage->ru_stime.tv_sec) * 1000000000ULL +
          uint64_t (usage->ru_utime.tv_usec + usage->ru_stime.tv_usec) * 1000ULL;
      const uint64_t descendants = record.descendant_user_ns + record.descendant_sys_ns;
      if (record.descendant_usage_complete && inclusive >= descendants)
        snprintf (self_cpu, sizeof self_cpu, "%llu",
                  (unsigned long long) (inclusive - descendants));
    }
    const int n = snprintf (
        line, sizeof line,
        "{\"event\":\"worker_reap\",\"worker_pid\":%ld,\"start_mono_ns\":%llu,"
        "\"mono_ns\":%llu,\"user_ns\":%s,\"sys_ns\":%s,\"peak_rss_kb\":%s,"
        "\"self_cpu_ns\":%s,\"usage_scope\":\"wait4_includes_reaped_descendants\","
        "\"reason\":\"%s\","
        "\"exit_code\":%d,\"signal\":%d,\"dropped_records\":%llu}\n",
        long (record.pid), (unsigned long long) record.start_mono_ns,
        (unsigned long long) (reaped_mono_ns ? reaped_mono_ns : phase_clock (CLOCK_MONOTONIC)),
        user, sys, peak, self_cpu, reason, usage && WIFEXITED (status) ? WEXITSTATUS (status) : -1,
        usage && WIFSIGNALED (status) ? WTERMSIG (status) : 0, record.dropped);
    if (n > 0 && size_t (n) < sizeof line)
      phase_records_final (line, size_t (n));
    constexpr const char* boundaries[] = {"decline", "route_stopped", "verification_complete",
                                          "terminal_result", "exit"};
    for (unsigned i = 0; i < 5; ++i)
      if (record.boundary_committed[i] && record.boundaries[i].mono_ns)
        resource_event (record, boundaries[i], true, &record.boundaries[i]);
    for (unsigned i = 0; i < std::min (record.accounting_depth, 16u); ++i) {
      const auto& entry = record.accounting[i];
      accounting_segment (record, entry, {}, true, reaped_mono_ns);
      const int size = snprintf (
          line, sizeof line,
          "{\"event\":\"accounting_censored\",\"worker_pid\":%ld,\"phase_id\":%llu,"
          "\"parent_phase_id\":%llu,\"subjob\":%u,\"run_id\":%u,\"phase\":\"%s\",\"entry_ns\":%"
          "llu,\"entry_cpu_ns\":%llu,"
          "\"mono_ns\":%llu,\"cpu_ns\":null,\"dropped_records\":%llu}\n",
          long (record.pid), (unsigned long long) entry.id, (unsigned long long) entry.parent,
          entry.subjob, entry.run_id, entry.name, (unsigned long long) entry.start.wall,
          (unsigned long long) entry.start.cpu,
          (unsigned long long) (reaped_mono_ns ? reaped_mono_ns : phase_clock (CLOCK_MONOTONIC)),
          record.dropped);
      if (size > 0 && size_t (size) < sizeof line)
        phase_records_final (line, size_t (size));
    }
  }

  inline void descendant_record (worker_record& worker, pid_t pid, const rusage* usage,
                                 int status = 0, bool censored = false) noexcept {
    if (!phase_records_enabled ())
      return;
    const uint64_t user = usage ? uint64_t (usage->ru_utime.tv_sec) * 1000000000ULL +
                                      usage->ru_utime.tv_usec * 1000ULL
                                : 0;
    const uint64_t sys = usage ? uint64_t (usage->ru_stime.tv_sec) * 1000000000ULL +
                                     usage->ru_stime.tv_usec * 1000ULL
                               : 0;
    char line[512], user_text[32] = "null", sys_text[32] = "null";
    if (usage) {
      snprintf (user_text, sizeof user_text, "%llu", (unsigned long long) user);
      snprintf (sys_text, sizeof sys_text, "%llu", (unsigned long long) sys);
    }
    const int n = snprintf (
        line, sizeof line,
        "{\"event\":\"descendant_%s\",\"worker_pid\":%ld,\"descendant_pid\":%ld,"
        "\"mono_ns\":%llu,\"user_ns\":%s,\"sys_ns\":%s,\"peak_rss_kb\":%ld,"
        "\"exit_code\":%d,\"signal\":%d}\n",
        usage      ? "reap"
        : censored ? "censored"
                   : "spawn",
        long (worker.pid), long (pid), (unsigned long long) phase_clock (CLOCK_MONOTONIC),
        user_text, sys_text, usage ? usage->ru_maxrss : -1,
        usage && WIFEXITED (status) ? WEXITSTATUS (status) : -1,
        usage && WIFSIGNALED (status) ? WTERMSIG (status) : 0);
    if (n > 0 && size_t (n) < sizeof line)
      phase_records_final (line, size_t (n));
  }

  inline void worker_event (worker_record& record, const char* event, const char* reason = "none",
                            int exit_code = -1, int signal = 0,
                            const char* telemetry = "pending") noexcept {
    if (!phase_records_enabled ())
      return;
    constexpr const char* boundaries[] = {"decline", "route_stopped", "verification_complete",
                                          "terminal_result", "exit"};
    for (unsigned i = 0; i < 5; ++i)
      if (strcmp (event, boundaries[i]) == 0) {
        const auto sample = resource_snapshot (record.pid);
        if (record.pid == getpid ()) {
          record.boundary_committed[i] = 0;
          record.boundaries[i] = sample;
          std::atomic_thread_fence (std::memory_order_release);
          record.boundary_committed[i] = 1;
        }
        resource_event (record, event, false, &sample);
        break;
      }
    if (std::strcmp (event, "parent_terminal") == 0 && record.stage_id && record.stage_start_ns) {
      char snapshot[512], total[24] = "null", bound[32] = "null";
      if (record.subjobs)
        snprintf (total, sizeof total, "%u", record.subjobs);
      if (record.k >= 0)
        snprintf (bound, sizeof bound, "%lld", record.k);
      const int length =
          snprintf (snapshot, sizeof snapshot,
                    "{\"event\":\"stage_censored\",\"worker\":%u,\"worker_pid\":%ld,\"seq\":%llu,"
                    "\"subjob\":%u,\"subjobs\":%s,\"run_id\":%u,\"k\":%s,\"stage_id\":%llu,"
                    "\"stage\":\"%s\",\"entry_ns\":%llu,\"entry_cpu_ns\":%llu,\"cpu_ns\":null,"
                    "\"mono_ns\":%llu,\"signal\":%d,"
                    "\"dropped_records\":%llu}\n",
                    record.index, long (record.pid), ++record.sequence, record.subjob, total,
                    record.run_id, bound, record.stage_id, record.observed_stage,
                    (unsigned long long) record.stage_start_ns,
                    (unsigned long long) record.stage_start_cpu_ns,
                    (unsigned long long) phase_clock (CLOCK_MONOTONIC), signal, record.dropped);
      if (length > 0 && size_t (length) < sizeof snapshot)
        phase_records_final (snapshot, size_t (length));
      else
        phase_records_drop ();
    }
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
    char context[96] = "";
    if (record.prepass)
      snprintf (context, sizeof context, "\"prepass\":%llu,\"run_id\":%u,", record.prepass,
                record.run_id);
    char line[1024];
    const int n = snprintf (
        line, sizeof line,
        "{\"event\":\"%s\",\"worker\":%u,\"worker_pid\":%ld,\"seq\":%llu,"
        "\"requested_backend\":\"%s\",\"effective_backend\":\"%s\","
        "\"original_polarity\":\"%s\",\"proof_polarity\":\"%s\","
        "\"route\":\"%s\",\"stage\":\"%s\",\"reason\":\"%s\","
        "\"r_prepass\":%s,\"equivariance\":%s,%s%s\"mono_ns\":%llu,"
        "\"dropped_records\":%llu,\"telemetry\":\"%s\"}\n",
        event, record.index, long (record.pid), record.sequence, record.requested_backend,
        record.effective_backend, record.original_polarity, record.proof_polarity, record.route,
        record.stage, escaped_reason, record.r_prepass ? "true" : "false",
        record.equivariance ? "true" : "false", outcome, context,
        (unsigned long long) phase_clock (CLOCK_MONOTONIC), record.dropped, telemetry);
    if (n > 0 && size_t (n) < sizeof line)
      phase_records_send (line, size_t (n));
    else
      phase_records_drop ();
  }
  inline void weakening_event (worker_record& record, const char* event,
                               const char* fields) noexcept {
    if (!phase_records_enabled ())
      return;
    char line[1024];
    const int n =
        snprintf (line, sizeof line,
                  "{\"event\":\"%s\",\"worker_pid\":%ld,\"seq\":%llu,\"prepass\":%llu,"
                  "\"run_id\":%u,\"mono_ns\":%llu,\"dropped_records\":%llu,%s}\n",
                  event, long (record.pid), ++record.sequence, record.prepass, record.run_id,
                  (unsigned long long) phase_clock (CLOCK_MONOTONIC), record.dropped, fields);
    if (n > 0 && size_t (n) < sizeof line)
      phase_records_send (line, size_t (n));
    else
      phase_records_drop ();
  }

  inline void weakening_cancelled (worker_record& record, const char* reason) noexcept {
    if (!record.run_active || !record.run_weakening)
      return;
    char fields[320], remaining[32] = "null";
    const auto now = phase_clock (CLOCK_MONOTONIC);
    if (record.deadline_ns)
      snprintf (remaining, sizeof remaining, "%llu",
                (unsigned long long) (now < record.deadline_ns ? record.deadline_ns - now : 0));
    snprintf (fields, sizeof fields,
              "\"outcome\":\"cancellation\",\"reason\":\"%s\",\"observer\":\"parent\","
              "\"wall_ns\":%llu,\"cpu_ns\":null,\"peak_rss_kb\":null,"
              "\"memory_reason\":\"killed_worker\",\"deadline_ns\":%llu,\"remaining_ns\":%s",
              reason, (unsigned long long) (now - record.run_start.wall),
              (unsigned long long) record.deadline_ns, remaining);
    weakening_event (record, "weakening_attempt_end", fields);
    record.run_active = false;
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
      ++worker->declines;
      worker_record_text (worker->reason, reason);
      worker_event (*worker, "decline", worker->reason);
    }
  }
  inline void worker_stopped (const char* reason) noexcept {
    if (auto* worker = active_worker_record ()) {
      ++worker->stops;
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
  [[noreturn]] inline void worker_exit (int code) noexcept {
    if (auto* worker = active_worker_record ())
      worker_event (*worker, "exit");
    _exit (code);
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
      char summary[256];
      const int n =
          snprintf (summary, sizeof summary,
                    "{\"event\":\"parent_record_summary\",\"pid\":%ld,\"dropped_records\":%llu}\n",
                    long (getpid ()), state.dropped);
      if (n > 0 && size_t (n) < sizeof summary)
        phase_records_final (summary, size_t (n));
      close (state.write_fd);
      state.write_fd = -1;
    }
    state.enabled = false;
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
  inline void phase_records_disable () noexcept {
    phase_records_warn ();
    phase_record_process_state ().enabled = false;
    phase_records_close ();
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
    const int directory = open (dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    char name[64];
    snprintf (name, sizeof name, "%ld-final.jsonl", long (getpid ()));
    const int probe =
        directory < 0
            ? -1
            : openat (directory, name,
                      O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0644);
    struct stat target {};
    const bool writable = probe >= 0 && fstat (probe, &target) == 0 && S_ISREG (target.st_mode);
    if (probe >= 0)
      close (probe);
    if (directory >= 0)
      close (directory);
    if (!writable) {
      phase_records_warn ();
      return;
    }
    int fds[2];
#ifdef __linux__
    if (pipe2 (fds, O_CLOEXEC | O_NONBLOCK) != 0) {
      phase_records_warn ();
      return;
    }
#else
    if (pipe (fds) != 0) {
      phase_records_warn ();
      return;
    }
    for (const int fd : fds) {
      if (fcntl (fd, F_SETFD, FD_CLOEXEC) < 0 || fcntl (fd, F_SETFL, O_NONBLOCK) < 0) {
        close (fds[0]);
        close (fds[1]);
        phase_records_warn ();
        return;
      }
    }
#endif
    if (fcntl (fds[0], F_SETFL, 0) < 0) {
      close (fds[0]);
      close (fds[1]);
      phase_records_warn ();
      return;
    }
    const long pipe_buf = fpathconf (fds[1], _PC_PIPE_BUF);
    // POSIX guarantees at least _POSIX_PIPE_BUF even if fpathconf fails.
    size_t packet_bytes =
        std::min (sizeof (phase_packet), size_t (pipe_buf > 0 ? pipe_buf : _POSIX_PIPE_BUF));
    packet_bytes = std::min (packet_bytes, std::max (packet_cap, size_t {_POSIX_PIPE_BUF}));
    auto& state = phase_record_process_state ();
    state.pid = state.owner = getpid ();
    state.write_fd = fds[1];
    state.packet_bytes = packet_bytes;
    state.clock_ticks = sysconf (_SC_CLK_TCK);
    state.page_size = sysconf (_SC_PAGESIZE);
    struct sigaction ignore {}, old_pipe {}, old_size {};
    ignore.sa_handler = SIG_IGN;
    sigemptyset (&ignore.sa_mask);
    const bool pipe_ok = sigaction (SIGPIPE, &ignore, &old_pipe) == 0;
    const bool size_ok = sigaction (SIGXFSZ, &ignore, &old_size) == 0;
    const auto restore_signals = [&] {
      if (pipe_ok)
        sigaction (SIGPIPE, &old_pipe, nullptr);
      if (size_ok)
        sigaction (SIGXFSZ, &old_size, nullptr);
    };
    if (!pipe_ok || !size_ok || state.clock_ticks <= 0 || state.page_size <= 0 ||
        atexit (phase_records_close) != 0) {
      close (fds[0]);
      restore_signals ();
      phase_records_disable ();
      return;
    }
    // The writer inherits the prepared descriptors and signal dispositions;
    // all fallible recorder setup runs here, before solving or enabling it.
    const pid_t writer = fork ();
    if (writer == 0) {
      close (fds[1]);
      if (writer_setup)
        writer_setup ();
      phase_records_writer_loop (fds[0], dir, packet_bytes);
    }
    close (fds[0]);
    if (writer < 0) {
      restore_signals ();
      phase_records_disable ();
      return;
    }
    state.writer = writer;
    state.enabled = true;
  }

  inline phase_stamp phase_start () noexcept {
    if (!phase_records_enabled ())
      return {};
    return {phase_clock (CLOCK_MONOTONIC), phase_clock (CLOCK_PROCESS_CPUTIME_ID)};
  }

  inline void accounting_emit (const accounting_entry& entry, const char* event,
                               phase_stamp end = {}) noexcept {
    char line[768], wall_delta[32] = "null", cpu_delta[32] = "null";
    if (end.wall) {
      snprintf (wall_delta, sizeof wall_delta, "%llu",
                (unsigned long long) (end.wall - entry.start.wall));
      snprintf (cpu_delta, sizeof cpu_delta, "%llu",
                (unsigned long long) (end.cpu - entry.start.cpu));
    }
    const int n = snprintf (
        line, sizeof line,
        "{\"event\":\"%s\",\"worker_pid\":%ld,\"phase_id\":%llu,\"parent_phase_id\":%llu,"
        "\"subjob\":%u,\"run_id\":%u,\"phase\":\"%s\",\"entry_ns\":%llu,\"entry_cpu_ns\":%llu,"
        "\"mono_ns\":%llu,\"process_cpu_ns\":%llu,\"wall_ns\":%s,\"cpu_ns\":%s,"
        "\"dropped_records\":%llu}\n",
        event, long (getpid ()), (unsigned long long) entry.id, (unsigned long long) entry.parent,
        entry.subjob, entry.run_id, entry.name, (unsigned long long) entry.start.wall,
        (unsigned long long) entry.start.cpu,
        (unsigned long long) (end.wall ? end.wall : entry.start.wall),
        (unsigned long long) (end.wall ? end.cpu : entry.start.cpu), wall_delta, cpu_delta,
        phase_record_process_state ().dropped);
    if (n > 0 && size_t (n) < sizeof line)
      phase_records_send (line, size_t (n));
    else
      phase_records_drop ();
  }
  inline uint64_t accounting_begin (const char* name, phase_stamp start = {}) noexcept {
    auto* w = active_worker_record ();
    if (!w || w->pid != getpid ())
      return 0;
    const auto id = ++w->next_accounting_id;
    if (w->accounting_depth >= 16) {
      ++w->accounting_depth;
      phase_records_drop ();
      return id;
    }
    accounting_entry entry;
    entry.id = id;
    entry.subjob = w->subjob;
    entry.run_id = w->run_id;
    entry.start = entry.segment_start = start.wall ? start : phase_start ();
    if (w->accounting_depth) {
      auto& parent = w->accounting[w->accounting_depth - 1];
      entry.parent = parent.id;
      accounting_segment (*w, parent, entry.start);
      parent.segment_start = {};
    }
    worker_record_text (entry.name, name);
    const unsigned slot = w->accounting_depth;
    w->accounting[slot] = entry;
    std::atomic_thread_fence (std::memory_order_release);
    w->accounting_depth = slot + 1;
    accounting_emit (entry, "accounting_entry");
    return id;
  }
  inline void accounting_end () noexcept {
    auto* w = active_worker_record ();
    if (!w || !w->accounting_depth)
      return;
    if (w->accounting_depth > 16) {
      --w->accounting_depth;
      return;
    }
    const auto end = phase_start ();
    auto& current = w->accounting[w->accounting_depth - 1];
    accounting_segment (*w, current, end);
    current.segment_start = {};
    accounting_emit (current, "accounting_end", end);
    --w->accounting_depth;
    if (w->accounting_depth)
      w->accounting[w->accounting_depth - 1].segment_start = end;
  }
  class accounting_phase {
      uint64_t id_;

    public:
      explicit accounting_phase (const char* name) noexcept {
        if (phase_record_process_state ().enabled)
          id_ = accounting_begin (name);
      }
      void finish () noexcept {
        if (!phase_record_process_state ().enabled)
          return;
        if (id_) {
          accounting_end ();
          id_ = 0;
        }
      }
      ~accounting_phase () { finish (); }
      accounting_phase (const accounting_phase&) = delete;
  };

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
        "{\"arm\":\"%s\",\"phase\":\"%s\",\"entry_ns\":%llu,\"mono_ns\":%llu,\"entry_cpu_ns\":%"
        "llu,\"process_cpu_ns\":%llu,\"wall_ns\":%llu,\"cpu_ns\":%llu,"
        "\"rss_kb\":%ld,\"peak_rss_kb\":%ld,\"mallinfo2\":%s,"
        "\"bdd_nodes\":%lld,\"bdd_cache\":%lld,\"work_count\":%llu,"
        "\"size_bytes\":%llu,\"monitor_count\":%llu,\"monitor_states\":%llu}\n",
        arm, phase, (unsigned long long) start.wall, (unsigned long long) wall,
        (unsigned long long) start.cpu, (unsigned long long) cpu,
        (unsigned long long) (wall - start.wall), (unsigned long long) (cpu - start.cpu),
        memory.rss_kb, memory.peak_rss_kb, heap_json, bdd_nodes, bdd_cache, work_count, size_bytes,
        monitor_count, monitor_states);
    if (n <= 0 || size_t (n) >= sizeof line) {
      phase_records_drop ();
      return;
    }
    phase_records_send (line, size_t (n));
  }

  inline void legacy_stage_event (const char* event, const char* stage, phase_stamp start = {},
                                  const char* reason = "none") noexcept {
    if (!phase_records_enabled ())
      return;
    auto* w = active_worker_record ();
    if (!w)
      return;
    char line[1024], total[24] = "null", k[32] = "null", timing[192] = "";
    if (w->subjobs)
      snprintf (total, sizeof total, "%u", w->subjobs);
    if (w->k >= 0)
      snprintf (k, sizeof k, "%lld", w->k);
    const auto now = phase_clock (CLOCK_MONOTONIC);
    if (start.wall) {
      rusage usage {};
      const bool sampled = getrusage (RUSAGE_SELF, &usage) == 0;
      snprintf (timing, sizeof timing, ",\"wall_ns\":%llu,\"cpu_ns\":%llu",
                (unsigned long long) (now - start.wall),
                (unsigned long long) (phase_clock (CLOCK_PROCESS_CPUTIME_ID) - start.cpu));
      if (sampled) {
        const auto used = strlen (timing);
        snprintf (timing + used, sizeof timing - used, ",\"peak_rss_kb\":%ld", usage.ru_maxrss);
      }
    }
    if (!start.wall) {
      snprintf (timing, sizeof timing, ",\"process_cpu_ns\":%llu",
                (unsigned long long) w->stage_start_cpu_ns);
    }
    const int n = snprintf (
        line, sizeof line,
        "{\"event\":\"%s\",\"worker\":%u,\"worker_pid\":%ld,\"seq\":%llu,"
        "\"subjob\":%u,\"subjobs\":%s,\"run_id\":%u,\"k\":%s,\"stage_id\":%llu,"
        "\"stage\":\"%s\",\"mono_ns\":%llu,\"dropped_records\":%llu,\"reason\":\"%s\"%s}\n",
        event, w->index, long (w->pid), ++w->sequence, w->subjob, total, w->run_id, k, w->stage_id,
        stage, (unsigned long long) now, w->dropped, reason, timing);
    if (n > 0 && size_t (n) < sizeof line)
      phase_records_send (line, size_t (n));
    else
      phase_records_drop ();
  }

  inline void legacy_metric (const char* key, const char* value) noexcept {
    if (!phase_records_enabled ())
      return;
    auto* w = active_worker_record ();
    if (!w)
      return;
    char escaped[384];
    size_t used = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*> (value); *p; ++p) {
      if (used + 7 >= sizeof escaped) {
        phase_records_drop ();
        return;
      }
      if (*p < 32)
        used += size_t (snprintf (escaped + used, sizeof escaped - used, "\\u%04x", *p));
      else {
        if (*p == '"' || *p == '\\')
          escaped[used++] = '\\';
        escaped[used++] = char (*p);
      }
    }
    escaped[used] = 0;
    char line[1024];
    const int n =
        snprintf (line, sizeof line,
                  "{\"event\":\"stage_metric\",\"worker_pid\":%ld,\"seq\":%llu,"
                  "\"stage_id\":%llu,\"mono_ns\":%llu,\"dropped_records\":%llu,"
                  "\"key\":\"%s\",\"value\":\"%s\"}\n",
                  long (w->pid), ++w->sequence, w->stage_id,
                  (unsigned long long) phase_clock (CLOCK_MONOTONIC), w->dropped, key, escaped);
    if (n > 0 && size_t (n) < sizeof line)
      phase_records_send (line, size_t (n));
    else
      phase_records_drop ();
  }
  inline void legacy_count (const char* key, unsigned long long value) noexcept {
    if (!phase_records_enabled ())
      return;
    char text[32];
    snprintf (text, sizeof text, "%llu", value);
    legacy_metric (key, text);
  }

  class legacy_phase {
      const char* name_;
      phase_stamp start_;
      uint64_t id_, previous_id_, previous_start_, previous_cpu_;
      char previous_stage_[48], previous_observed_stage_[48];
      int exceptions_;

    public:
      explicit legacy_phase (const char* name) {
        if (!phase_record_process_state ().enabled)
          return;
        name_ = name;
        start_ = phase_start ();
        exceptions_ = std::uncaught_exceptions ();
        id_ = accounting_begin (name, start_);
        if (auto* w = active_worker_record ()) {
          previous_id_ = w->stage_id;
          previous_start_ = w->stage_start_ns;
          previous_cpu_ = w->stage_start_cpu_ns;
          worker_record_text (previous_stage_, w->stage);
          worker_record_text (previous_observed_stage_, w->observed_stage);
          worker_record_text (w->observed_stage, name);
          w->stage_id = id_;
          w->stage_start_ns = start_.wall;
          w->stage_start_cpu_ns = start_.cpu;
          worker_stage (name);
          legacy_stage_event ("stage_entry", name);
        }
      }
      legacy_phase (const legacy_phase&) = delete;
      void finish (const char* reason = "none", bool stopped = false) noexcept {
        if (!phase_record_process_state ().enabled || !start_.wall)
          return;
        legacy_stage_event (stopped ? "stage_stopped" : "stage_completion", name_, start_, reason);
        start_ = {};
        if (id_)
          accounting_end ();
        if (auto* w = active_worker_record ()) {
          w->stage_id = previous_id_;
          w->stage_start_ns = previous_start_;
          w->stage_start_cpu_ns = previous_cpu_;
          worker_record_text (w->stage, previous_stage_);
          worker_record_text (w->observed_stage, previous_observed_stage_);
        }
      }
      ~legacy_phase () {
        if (!phase_record_process_state ().enabled || !start_.wall)
          return;
        const bool failed = std::uncaught_exceptions () > exceptions_;
        finish (failed ? "exception" : "none", failed);
      }
  };
  inline void legacy_inapplicable (const char* stage, const char* reason) noexcept {
    legacy_phase phase (stage);
    phase.finish (reason);
  }

  class phase_scope {
      const char* arm_;
      const char* phase_;
      phase_stamp start_;
      uint64_t id_;

    public:
      phase_scope (const char* arm, const char* phase) noexcept {
        if (!phase_record_process_state ().enabled)
          return;
        arm_ = arm;
        phase_ = phase;
        start_ = phase_start ();
        id_ = accounting_begin (phase, start_);
        worker_stage (phase);
      }
      ~phase_scope () { finish (); }
      phase_scope (const phase_scope&) = delete;
      phase_scope& operator= (const phase_scope&) = delete;
      void finish (long long bdd_nodes = -1, long long bdd_cache = -1) noexcept {
        if (!phase_record_process_state ().enabled)
          return;
        if (id_) {
          accounting_end ();
          id_ = 0;
        }
        if (start_.wall)
          phase_finish (arm_, phase_, start_, bdd_nodes, bdd_cache);
        start_ = {};
      }
  };
}
