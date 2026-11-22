#include "solver/restriction_prepass.hh"

#include "error_msg.hh"
#include "phase_records.hh"
#include <unordered_set>

namespace acacia::restriction {
  namespace {
    constexpr uint64_t attempt_ns = 200000000, total_ns = 400000000;
    constexpr rlim_t memory_bytes = rlim_t {512} * 1024 * 1024;
    constexpr int resource_exit = 4, error_exit = 5, deadline_exit = 6, cancelled_exit = 7;

    void record (const candidate& item, bool inputs, const char* outcome, const char* cause,
                 uint64_t start, pid_t child = -1) noexcept {
      if (!phase_records_enabled ())
        return;
      const auto* worker = active_worker_record ();
      char line[768];
      const auto now = phase_clock (CLOCK_MONOTONIC);
      const int size = snprintf (
          line, sizeof line,
          "{\"event\":\"restriction_attempt\",\"worker\":%u,\"worker_pid\":%ld,\"emitter_pid\":%"
          "ld,"
          "\"child_pid\":%ld,\"candidate\":\"%s\",\"family\":%u,\"polarity\":\"%s\","
          "\"outcome\":\"%s\",\"cause\":\"%s\",\"mono_ns\":%llu,\"wall_ns\":%llu,"
          "\"attempt_budget_ns\":%llu,\"total_budget_ns\":%llu,\"memory_bytes\":%llu}\n",
          worker ? worker->index : 0, long (getpid ()), long (getpid ()), long (child), item.name,
          item.family, inputs ? "UNREAL" : "REAL", outcome, cause, (unsigned long long) now,
          (unsigned long long) (now - start), (unsigned long long) attempt_ns,
          (unsigned long long) total_ns, (unsigned long long) memory_bytes);
      if (size > 0 && size_t (size) < sizeof line)
        phase_records_send (line, size_t (size));
      else
        phase_records_drop ();
    }

    bool limit_memory () {
#ifndef __SANITIZE_ADDRESS__
      // ASan reserves a large shadow mapping; the supervisor still checks RSS.
      rlimit limit {};
      if (getrlimit (RLIMIT_AS, &limit) != 0)
        return false;
      limit.rlim_cur = std::min (limit.rlim_cur, memory_bytes);
      if (setrlimit (RLIMIT_AS, &limit) != 0)
        return false;
#endif
      return true;
    }
  }  // namespace

  std::vector<candidate> candidates (const specification_metadata& metadata,
                                     const std::vector<std::string>& outputs, bool inputs) {
    std::vector<candidate> result;
    if (inputs) {
      candidate all {"input_families", 0, {}};
      unsigned index = 0;
      for (const auto& family : metadata.tlsf_declared_families) {
        if (!family.is_input || family.members.size () < 2)
          continue;
        candidate single {"input_family", ++index, {}};
        for (const auto& member : family.members) {
          auto representative = spot::formula::ap (family.members.front ());
          single.values.emplace (member, representative);
          all.values.emplace (member, representative);
        }
        if (result.size () < 2)
          result.push_back (std::move (single));
      }
      // One family duplicates the all-families candidate; at most three attempts.
      if (index < 2)
        result.clear ();
      if (index)
        result.insert (result.begin (), std::move (all));
    }
    else {
      std::unordered_set<std::string> family_members;
      for (const auto& family : metadata.tlsf_declared_families)
        if (!family.is_input)
          family_members.insert (family.members.begin (), family.members.end ());
      candidate scalar {"scalar_outputs_false", 0, {}};
      for (const auto& output : outputs)
        if (!family_members.contains (output))
          scalar.values.emplace (output, spot::formula::ff ());
      if (!scalar.values.empty ())
        result.push_back (std::move (scalar));
    }
    return result;
  }

  std::optional<spot::formula> substitute (spot::formula positive, const substitution& values,
                                           const std::vector<std::string>& inputs,
                                           const std::vector<std::string>& outputs,
                                           bool restrict_inputs) {
    const std::unordered_set<std::string> in (inputs.begin (), inputs.end ()),
        out (outputs.begin (), outputs.end ());
    if (in.size () != inputs.size () || out.size () != outputs.size ())
      return std::nullopt;
    for (const auto& ap : in)
      if (out.contains (ap))
        return std::nullopt;
    const auto& owner = restrict_inputs ? in : out;
    for (const auto& [ap, value] : values)
      if (!owner.contains (ap) ||
          !(value.is (spot::op::tt) || value.is (spot::op::ff) ||
            (value.is (spot::op::ap) && owner.contains (value.ap_name ()))))
        return std::nullopt;
    bool valid = true;
    std::unordered_map<spot::formula, spot::formula> memo;
    const auto visit = [&] (auto&& self, spot::formula part) -> spot::formula {
      if (part.is (spot::op::ap)) {
        valid &= in.contains (part.ap_name ()) || out.contains (part.ap_name ());
        if (const auto it = values.find (part.ap_name ()); it != values.end ())
          return it->second;
        return part;
      }
      if (const auto it = memo.find (part); it != memo.end ())
        return it->second;
      auto rewritten = part.map ([&] (spot::formula child) { return self (self, child); });
      memo.emplace (part, rewritten);
      return rewritten;
    };
    auto rewritten = visit (visit, positive);
    return valid ? std::optional {rewritten} : std::nullopt;
  }

  bool accepts (bool inputs, int verdict) {
    return verdict == (inputs ? EXIT_CODE_UNREAL : EXIT_CODE_REAL);
  }

  bool try_prepass (spot::formula positive, const specification_metadata& metadata,
                    const std::vector<std::string>& inputs,
                    const std::vector<std::string>& outputs, bool restrict_inputs,
                    uint64_t outer_deadline, const std::function<int (spot::formula)>& solve) try {
    legacy_phase phase ("restriction_prepass");
    const auto start = phase_clock (CLOCK_MONOTONIC);
    auto deadline = start + total_ns;
    if (outer_deadline)
      deadline = std::min (deadline, outer_deadline);
    const auto items = candidates (metadata, outputs, restrict_inputs);
    if (items.empty ()) {
      worker_decline ("restriction_no_candidate");
      return false;
    }
    for (const auto& item : items) {
      const auto entered = phase_clock (CLOCK_MONOTONIC);
      if (entered >= deadline) {
        worker_stopped ("deadline");
        break;
      }
      const auto attempt_deadline = std::min (deadline, entered + attempt_ns);
      record (item, restrict_inputs, "start", "none", entered);
      const auto parent = getpid ();
      const auto child = fork ();
      if (child == 0) {
#ifdef __linux__
        if (prctl (PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid () != parent)
          _exit (error_exit);
#endif
        // Keep candidate telemetry/cause state private; only a reaped exit can transfer a verdict.
        worker_record local;
        active_worker_record () = &local;
        phase_record_process_state ().enabled = false;
        unsetenv ("ACACIA_SPOT_CAPTURE_DIR");
        try {
          if (!limit_memory ())
            _exit (error_exit);
          const auto formula =
              substitute (positive, item.values, inputs, outputs, restrict_inputs);
          if (!formula)
            _exit (error_exit);
          const int verdict = solve (*formula);
          if (local.stopped)
            _exit (std::strcmp (local.reason, "resource") == 0    ? resource_exit
                   : std::strcmp (local.reason, "deadline") == 0  ? deadline_exit
                   : std::strcmp (local.reason, "cancelled") == 0 ? cancelled_exit
                                                                  : error_exit);
          _exit (verdict);
        } catch (const std::bad_alloc&) {
          _exit (resource_exit);
        } catch (...) {
          _exit (error_exit);
        }
      }
      if (child < 0) {
        worker_stopped ("error");
        record (item, restrict_inputs, "stopped", "error", entered);
        break;
      }
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
        else if (phase_clock (CLOCK_MONOTONIC) >= attempt_deadline)
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
      const int verdict = WIFEXITED (status) ? WEXITSTATUS (status) : error_exit;
      // Enforce the deadline even if a completed result raced the final poll.
      if (phase_clock (CLOCK_MONOTONIC) >= attempt_deadline)
        cause = "deadline";
      if (reaped && WIFEXITED (status) && std::strcmp (cause, "none") == 0 &&
          accepts (restrict_inputs, verdict)) {
        record (item, restrict_inputs, "accepted", "none", entered, child);
        worker_route (restrict_inputs ? "restriction_input" : "restriction_output", nullptr,
                      restrict_inputs ? "UNREAL" : "REAL");
        worker_verified (restrict_inputs ? "UNREAL" : "REAL", restrict_inputs ? "UNREAL" : "REAL");
        return true;
      }
      if (std::strcmp (cause, "none") == 0) {
        if (verdict == resource_exit)
          cause = "resource";
        else if (verdict == deadline_exit)
          cause = "deadline";
        else if (verdict == cancelled_exit)
          cause = "cancelled";
        else if (verdict == error_exit || verdict == EXIT_CODE_ERROR || !WIFEXITED (status))
          cause = "error";
      }
      if (std::strcmp (cause, "none") != 0)
        worker_stopped (cause);
      else if (verdict == EXIT_CODE_REAL || verdict == EXIT_CODE_UNREAL) {
        worker_decline ("restriction_opposite");
        cause = "applicability";
      }
      else {
        worker_decline ("restriction_inconclusive");
        cause = "applicability";
      }
      record (item, restrict_inputs, "fallback", cause, entered, child);
      // A later successful attempt must be allowed to clear a previous stop.
      if (worker) {
        worker->stopped = false;
        worker_record_text (worker->reason, "none");
      }
    }
    return false;
  } catch (const std::bad_alloc&) {
    worker_stopped ("resource");
    return false;
  } catch (...) {
    worker_stopped ("error");
    return false;
  }
}  // namespace acacia::restriction
