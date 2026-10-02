#include "configuration.hh"
#include "native_support.hh"
#include "error_msg.hh"
#include "phase_records.hh"

#if ACACIA_NATIVE_ARMS
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/resource.h>
#include <unistd.h>

namespace acacia {
  void native_arm_diagnostic (std::string_view arm, std::string_view stage, int status,
                                     std::string_view message) {
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
                                 std::string_view message) {
    native_arm_diagnostic (arm, stage, status, message);
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
          if (!path.empty () && path.back () == '\n') path.pop_back ();
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
    if (stage.substr (0, 7) != "budget-" && stage != "allocation" &&
        stage != "aag-size" && stage != "publish-size" &&
        stage != "metadata-size") return std::string (reason);
    return std::string (reason) + " formula_nodes=" + std::to_string (work.formula_nodes) +
        " ap=" + std::to_string (work.ap_count) +
        " conjuncts=" + std::to_string (work.conjuncts) +
        " max_conjunct_nodes=" + std::to_string (work.max_conjunct_nodes) +
        " temporal_depth=" + std::to_string (work.max_temporal_depth) +
        " predicted_states=" + std::to_string (work.predicted_monitor_states) +
        " monitors=" + std::to_string (work.monitors_completed) +
        " states=" + std::to_string (work.states) +
        " edges=" + std::to_string (work.edges) +
        " peak_rss_bytes=" + std::to_string (work.peak_rss_bytes);
  }

  void native_budget_record (std::string_view arm, std::string_view stage,
                             const TlsfGr1ConstructionWork& work,
                             uint64_t started_ns) noexcept {
    if (!phase_records_enabled () ||
        (stage.substr (0, 7) != "budget-" && stage != "allocation" &&
         stage != "aag-size" && stage != "publish-size" &&
         stage != "metadata-size")) return;
    char line[1024];
    const auto value = [] (uint64_t n) { return static_cast<unsigned long long> (n); };
    const int n = std::snprintf (
        line, sizeof line,
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
    if (n > 0 && size_t (n) < sizeof line) phase_records_send (line, size_t (n));
  }
}
#endif
