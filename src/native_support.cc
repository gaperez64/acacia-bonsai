#include "native_support.hh"

#include "configuration.hh"
#include "error_msg.hh"
#include "phase_records.hh"

#if ACACIA_NATIVE_ARMS
# include <algorithm>
# include <cerrno>
# include <cstdio>
# include <cstring>
# include <string>
# include <sys/resource.h>
# include <unistd.h>

namespace acacia {
  native_failure_category native_failure (TlsfGr1LiftStatus status) {
    switch (status) {
      case TLSF_GR1_LIFT_LIMIT: return native_failure_category::resource;
      case TLSF_GR1_LIFT_UNSUPPORTED:
      case TLSF_GR1_LIFT_DECLINED: return native_failure_category::decline;
      case TLSF_GR1_LIFT_DEADLINE: return native_failure_category::deadline;
      case TLSF_GR1_LIFT_CANCELLED: return native_failure_category::cancelled;
      default: return native_failure_category::error;
    }
  }

  native_failure_category native_failure (TlsfGr1ReductionStatus status) {
    switch (status) {
      case TLSF_GR1_REDUCE_LIMIT: return native_failure_category::resource;
      case TLSF_GR1_REDUCE_UNSUPPORTED:
      case TLSF_GR1_REDUCE_DECLINED: return native_failure_category::decline;
      case TLSF_GR1_REDUCE_DEADLINE: return native_failure_category::deadline;
      case TLSF_GR1_REDUCE_CANCELLED: return native_failure_category::cancelled;
      default: return native_failure_category::error;
    }
  }

  native_failure_category native_failure (TlsfGr1CheckStatus status, TlsfGr1CheckVerdict verdict) {
    switch (status) {
      case TLSF_GR1_CHECK_LIMIT: return native_failure_category::resource;
      case TLSF_GR1_CHECK_OK:
        return verdict == TLSF_GR1_CHECK_CERT_FAILED || verdict == TLSF_GR1_CHECK_REFUTED
                   ? native_failure_category::decline
                   : native_failure_category::error;
      case TLSF_GR1_CHECK_DEADLINE: return native_failure_category::deadline;
      case TLSF_GR1_CHECK_CANCELLED: return native_failure_category::cancelled;
      default: return native_failure_category::error;
    }
  }

  native_failure_category native_failure (TlsfPipelineStatus status) {
    switch (status) {
      case TLSF_PIPELINE_LIMIT: return native_failure_category::resource;
      case TLSF_PIPELINE_DECLINED: return native_failure_category::decline;
      default: return native_failure_category::error;
    }
  }

  native_failure_category native_failure (OxiddFailureKind status) {
    switch (status) {
      case OXIDD_FAILURE_BDD:
      case OXIDD_FAILURE_HOST:
      case OXIDD_FAILURE_ARTIFACT_LIMIT: return native_failure_category::resource;
      case OXIDD_FAILURE_DEADLINE: return native_failure_category::deadline;
      case OXIDD_FAILURE_CANCELLED: return native_failure_category::cancelled;
      default: return native_failure_category::error;
    }
  }

  const char* native_failure_reason (native_failure_category category) {
    switch (category) {
      case native_failure_category::decline: return "decline";
      case native_failure_category::resource: return "resource";
      case native_failure_category::deadline: return "deadline";
      case native_failure_category::cancelled: return "cancelled";
      case native_failure_category::error: return "error";
    }
    return "error";
  }

  void native_arm_diagnostic (std::string_view arm, std::string_view stage, int status,
                              std::string_view message, native_failure_category category) {
    if (auto* worker = active_worker_record ()) {
      // Preserve the obstruction even if the child exits before its caller can
      // publish a terminal packet. Messages remain on the incumbent stderr path.
      const std::string reason (stage);
      worker_stage (reason.c_str ());
      if (category == native_failure_category::decline) {
        if (!worker->stopped && std::strcmp (worker->reason, reason.c_str ()) != 0)
          worker_decline (reason.c_str ());
      }
      else if (!worker->stopped) {
        worker_stopped (native_failure_reason (category));
      }
    }
    const auto quote = [] (std::string_view value) {
      std::string out = "\"";
      for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\')
          out += '\\';
        if (ch < 0x20) {
          constexpr char hex[] = "0123456789abcdef";
          out += "\\u00";
          out += hex[ch >> 4];
          out += hex[ch & 15];
        }
        else
          out += char (ch);
      }
      return out + '"';
    };
    const std::string line =
        std::string ("{\"arm\":") + quote (arm) + ",\"stage\":" + quote (stage) +
        ",\"status\":" + std::to_string (status) + ",\"message\":" + quote (message) + "}\n";
    // One write keeps concurrent children's JSON records intact on stderr.
    (void) ::write (STDERR_FILENO, line.data (), line.size ());
  }

  void native_diagnostic (std::string_view arm, std::string_view stage, int status,
                          std::string_view message, native_failure_category category) {
    native_arm_diagnostic (arm, stage, status, message, category);
  }

  bool native_limit_address_space (std::string_view arm) {
    rlimit address {};
    if (getrlimit (RLIMIT_AS, &address) != 0) {
      native_arm_diagnostic (arm, "memory", errno, "could not read address-space cap");
      return false;
    }
    constexpr rlim_t cap = rlim_t {8} * 1024 * 1024 * 1024;
    if (address.rlim_cur == RLIM_INFINITY || address.rlim_cur > cap) {
      address.rlim_cur = cap;
      if (setrlimit (RLIMIT_AS, &address) != 0) {
        native_arm_diagnostic (arm, "memory", errno, "could not set address-space cap");
        return false;
      }
    }
    return true;
  }

  TlsfGr1ConstructionBudget native_construction_budget (size_t arm_count) {
    constexpr uint64_t address_cap = uint64_t {8} * 1024 * 1024 * 1024;
    uint64_t invocation_limit = address_cap;
    // cgroup v2 exposes the current scope's path, not its memory.max at the
    // mount root. A missing or unbounded scope falls back to our RLIMIT_AS.
    if (FILE* membership = std::fopen ("/proc/self/cgroup", "r")) {
      char line[4096] {};
      if (std::fgets (line, sizeof line, membership)) {
        if (const char* group = std::strstr (line, "::/")) {
          std::string path = "/sys/fs/cgroup";
          path += group + 2;
          if (!path.empty () && path.back () == '\n')
            path.pop_back ();
          path += "/memory.max";
          if (FILE* file = std::fopen (path.c_str (), "r")) {
            unsigned long long scoped = 0;
            if (std::fscanf (file, "%llu", &scoped) == 1 && scoped > 0)
              invocation_limit = std::min (invocation_limit, uint64_t (scoped));
            std::fclose (file);
          }
        }
      }
      std::fclose (membership);
    }
    TlsfGr1ConstructionBudget budget {};
    // Global structural baselines, independent of names and expected verdicts.
    // The explicit research scale changes only these six precheck thresholds.
    budget.max_formula_nodes = 4096;
    budget.max_ap_count = 2048;
    budget.max_conjuncts = 1024;
    budget.max_conjunct_nodes = 1024;
    budget.max_temporal_depth = 16;
    budget.max_predicted_monitor_states = 1u << 24;
    budget.max_total_states = 10000;
    budget.max_total_edges = 150000;
    budget.max_rss_bytes = invocation_limit / std::max<size_t> (1, arm_count) / 2;
    return budget;
  }

  std::string native_budget_message (std::string_view stage, std::string_view reason,
                                     const TlsfGr1ConstructionWork& work) {
    if (stage.substr (0, 7) != "budget-" && stage != "allocation" && stage != "aag-size" &&
        stage != "publish-size" && stage != "metadata-size")
      return std::string (reason);
    return std::string (reason) + " formula_nodes=" + std::to_string (work.formula_nodes) +
           " ap=" + std::to_string (work.ap_count) +
           " conjuncts=" + std::to_string (work.conjuncts) +
           " max_conjunct_nodes=" + std::to_string (work.max_conjunct_nodes) +
           " temporal_depth=" + std::to_string (work.max_temporal_depth) +
           " predicted_states=" + std::to_string (work.predicted_monitor_states) +
           " monitors=" + std::to_string (work.monitors_completed) +
           " states=" + std::to_string (work.states) + " edges=" + std::to_string (work.edges) +
           " peak_rss_bytes=" + std::to_string (work.peak_rss_bytes);
  }

  void native_budget_record (std::string_view arm, std::string_view stage,
                             const TlsfGr1ConstructionWork& work, uint64_t started_ns) noexcept {
    if (!phase_records_enabled () ||
        (stage.substr (0, 7) != "budget-" && stage != "allocation" && stage != "aag-size" &&
         stage != "publish-size" && stage != "metadata-size"))
      return;
    char line[1024];
    const auto value = [] (uint64_t n) { return static_cast<unsigned long long> (n); };
    const int n =
        std::snprintf (line, sizeof line,
                       "{\"arm\":\"%.*s\",\"phase\":\"budget_decline\",\"stage\":\"%.*s\","
                       "\"elapsed_ns\":%llu,\"formula_nodes\":%llu,\"ap_count\":%llu,"
                       "\"conjuncts\":%llu,\"max_conjunct_nodes\":%llu,"
                       "\"max_temporal_depth\":%llu,\"predicted_monitor_states\":%llu,"
                       "\"monitors\":%llu,\"states\":%llu,\"edges\":%llu,"
                       "\"peak_rss_bytes\":%llu}\n",
                       int (arm.size ()), arm.data (), int (stage.size ()), stage.data (),
                       value (phase_clock (CLOCK_MONOTONIC) - started_ns),
                       value (work.formula_nodes), value (work.ap_count), value (work.conjuncts),
                       value (work.max_conjunct_nodes), value (work.max_temporal_depth),
                       value (work.predicted_monitor_states), value (work.monitors_completed),
                       value (work.states), value (work.edges), value (work.peak_rss_bytes));
    if (n > 0 && size_t (n) < sizeof line)
      phase_records_send (line, size_t (n));
  }
}
#endif
