#include "solver/obligation_helper.hh"

#include <algorithm>
#include <new>
#include <spot/twaalgos/emptiness.hh>
#include <stdexcept>

namespace acacia::obligation {
  const char* name (outcome value) noexcept {
    switch (value) {
      case outcome::pending: return "pending";
      case outcome::real: return "completed_REAL";
      case outcome::unreal: return "completed_UNREAL";
      case outcome::inapplicable: return "inapplicable";
      case outcome::exhausted: return "exhausted";
      case outcome::cancelled: return "cancelled";
      case outcome::error: return "error";
    }
    return "error";
  }

  int exit_code (outcome value) noexcept {
    if (value == outcome::real)
      return EXIT_CODE_REAL;
    if (value == outcome::unreal)
      return EXIT_CODE_UNREAL;
    return EXIT_CODE_UNKNOWN;
  }

  bool eligible (spot::formula objective) noexcept {
    return objective && objective.is_syntactic_obligation ();
  }

  void publish_eligibility (control* channel, spot::formula objective) noexcept {
    if (channel)
      channel->eligible.store (
          eligible (objective) ? eligibility::eligible : eligibility::inapplicable,
          std::memory_order_release);
  }

  void record (const char* event, outcome value, const char* reason, uint64_t start_ns,
               bool same_library_checked, int worker, pid_t pid) noexcept {
    if (!phase_records_enabled ())
      return;
    if (auto* current = active_worker_record ()) {
      worker = int (current->index);
      pid = current->pid;
    }
    const auto now = phase_clock (CLOCK_MONOTONIC);
    char line[640];
    const int length =
        snprintf (line, sizeof line,
                  "{\"event\":\"obligation_%s\",\"worker\":%d,\"worker_pid\":%ld,"
                  "\"outcome\":\"%s\",\"reason\":\"%s\",\"mono_ns\":%llu,\"wall_ns\":%llu,"
                  "\"trust\":\"trusted_exact_spot\",\"same_library_checked\":%s}\n",
                  event, worker, long (pid), name (value), reason, (unsigned long long) now,
                  (unsigned long long) (start_ns ? now - start_ns : 0),
                  same_library_checked ? "true" : "false");
    if (length > 0 && size_t (length) < sizeof line)
      phase_records_send (line, size_t (length));
  }

  outcome completed (const spot::mtdswa_ptr& answer, bool decision_only) noexcept {
    if (!answer || answer->num_roots () == 0 || answer->colors.size () != answer->states.size ())
      return outcome::error;
    if (answer->acc.is_f ()) {
      if (answer->num_roots () == 1 && answer->states[0] == bddfalse)
        return outcome::unreal;
      return outcome::error;
    }
    if (!answer->acc.is_t () || answer->states[0] == bddfalse)
      return outcome::error;
    if (decision_only && (answer->num_roots () != 1 || answer->states[0] != bddtrue))
      return outcome::error;
    return outcome::real;
  }

  bool check_strategy (spot::formula objective, const spot::mtdswa_ptr& strategy) {
    if (completed (strategy, false) != outcome::real)
      return false;
    auto mealy = spot::mtdswa_strategy_to_mealy (strategy, false, false);
    if (!mealy || !mealy->num_states ())
      return false;
    const bdd outputs = strategy->get_controllable_variables ();
    for (unsigned state = 0; state < mealy->num_states (); ++state) {
      bdd coverage = bddfalse;
      for (const auto& edge : mealy->out (state))
        coverage |= edge.cond;
      if (bdd_exist (coverage, outputs) != bddtrue)
        return false;
    }
    auto negated =
        spot::obligation_to_mtdswa (spot::formula::Not (objective), strategy->get_dict ());
    if (!negated || !negated->num_roots ())
      return false;
    // Same-library language check, without a full Büchi translation. This is
    // not an independent certificate; the exact Spot decision remains trusted.
    return !negated->as_twa (false, false)->intersects (mealy);
  }

  result solve (spot::formula objective, const std::vector<std::string>& inputs,
                const std::vector<std::string>& outputs, timing semantics) {
    if (!eligible (objective))
      return {outcome::inapplicable, "non_obligation"};
    try {
      auto dict = spot::make_bdd_dict ();
      int owner = 0;
      struct cleanup {
          spot::bdd_dict_ptr dict;
          int* owner;
          ~cleanup () { dict->unregister_all_my_variables (owner); }
      } variables {dict, &owner};
      const auto register_variables = [&] (const std::vector<std::string>& aps) {
        for (const auto& ap : aps)
          dict->register_proposition (spot::formula::ap (ap), &owner);
      };
      if (semantics == timing::mealy) {
        register_variables (inputs);
        register_variables (outputs);
      }
      else {
        register_variables (outputs);
        register_variables (inputs);
      }
      // realizability=false retains a REAL strategy for the same-library check.
      // Both settings explicitly distinguish winners using acc=t/f in 2.16.
      // No partial/debug return or exception is allowed to become a verdict.
      auto answer = spot::obligation_synthesis (objective, dict, outputs, false, true, -1);
      const auto value = completed (answer, false);
      if (value == outcome::real) {
        if (!check_strategy (objective, answer))
          return {outcome::error, "strategy_check_failed"};
        return {outcome::real, "completed", true};
      }
      return {value, value == outcome::unreal ? "completed" : "invalid_return"};
    } catch (const std::bad_alloc&) {
      return {outcome::exhausted, "allocation"};
    } catch (const std::exception&) {
      return {outcome::error, "exception"};
    } catch (...) {
      return {outcome::error, "exception"};
    }
  }

  bool limit_address_space () noexcept {
    rlimit address {};
    if (getrlimit (RLIMIT_AS, &address) != 0)
      return false;
    constexpr rlim_t cap = rlim_t {8} * 1024 * 1024 * 1024;
    if (address.rlim_cur == RLIM_INFINITY || address.rlim_cur > cap) {
      address.rlim_cur = cap;
      return setrlimit (RLIMIT_AS, &address) == 0;
    }
    return true;
  }

  uint64_t rss_share (size_t workers) {
    uint64_t cap = uint64_t {8} * 1024 * 1024 * 1024;
    rlimit address {};
    if (getrlimit (RLIMIT_AS, &address) == 0 && address.rlim_cur != RLIM_INFINITY)
      cap = std::min (cap, uint64_t (address.rlim_cur));
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
            if (std::fscanf (file, "%llu", &scoped) == 1 && scoped)
              cap = std::min (cap, uint64_t (scoped));
            std::fclose (file);
          }
        }
      }
      std::fclose (membership);
    }
    return cap / std::max<size_t> (1, workers) / 2;
  }

  uint64_t resident_bytes (pid_t pid) noexcept {
#ifdef __linux__
    char path[64];
    snprintf (path, sizeof path, "/proc/%ld/statm", long (pid));
    if (FILE* file = std::fopen (path, "r")) {
      unsigned long long pages = 0;
      const bool read = std::fscanf (file, "%*u %llu", &pages) == 1;
      std::fclose (file);
      const long page_size = sysconf (_SC_PAGESIZE);
      if (read && page_size > 0)
        return pages * uint64_t (page_size);
    }
#endif
    return 0;
  }
}
