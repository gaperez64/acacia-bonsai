#include "solver/obligation_helper.hh"

#include "solver/accounting_identity.hh"
#include "solver/mealy_to_moore.hh"
#include "solver/synthesis_rendezvous.hh"
#include <unordered_set>

#include <algorithm>
#include <new>
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
               int worker, pid_t pid) noexcept {
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
                  "{\"arm\":\"both:obligation:spot\",\"event\":\"obligation_%s\","
                  "\"worker\":%d,\"worker_pid\":%ld,"
                  "\"outcome\":\"%s\",\"reason\":\"%s\",\"mono_ns\":%llu,\"wall_ns\":%llu,"
                  "\"trust\":\"spot_exact\",\"same_library_checked\":false,"
                  "\"independent_checked\":false}\n",
                  event, worker, long (pid), name (value), reason, (unsigned long long) now,
                  (unsigned long long) (start_ns ? now - start_ns : 0));
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

  result solve (spot::formula objective, const std::vector<std::string>& inputs,
                const std::vector<std::string>& outputs, timing semantics, bool synthesis,
                conversion convert) {
    if (!eligible (objective))
      return {outcome::inapplicable, "non_obligation"};
    try {
      accounting_phase preparation ("obligation_preprocessing");
      // Spot 2.16 sizes its output-AP bitmap before constructing names from
      // outvars. Keep even absent output APs alive first, without BDD registration.
      std::vector<spot::formula> output_aps;
      output_aps.reserve (outputs.size ());
      for (const auto& output : outputs)
        output_aps.push_back (spot::formula::ap (output));
      if (phase_records_enabled ())
        accounting_identity (
            objective, inputs, outputs, "spot-mtdswa",
            synthesis ? "obligation:simplify_terms=true:debug=-1:decision=false"
                      : "obligation:simplify_terms=true:debug=-1:decision=true",
            semantics == timing::mealy ? "mealy:inputs-first" : "moore:outputs-first");
      auto dict = spot::make_bdd_dict ();
      int owner = 0;
      struct cleanup {
          spot::bdd_dict_ptr dict;
          int* owner;
          ~cleanup () { dict->unregister_all_my_variables (owner); }
      } variables {dict, &owner};
      // ltlsynt pre-registers only the first timing block in formula traversal
      // order. obligation_synthesis registers the remaining block in that order.
      const auto& first = semantics == timing::mealy ? inputs : outputs;
      const std::unordered_set<std::string> first_names (first.begin (), first.end ());
      objective.traverse ([&] (spot::formula ap) {
        if (ap.is (spot::op::ap) && first_names.contains (ap.ap_name ()))
          dict->register_proposition (ap, &owner);
        return false;
      });
      // One completed decision call: no strategy allocation or re-check.
      // Partial/debug returns and exceptions never become verdicts.
      preparation.finish ();
      accounting_phase spot_call ("obligation_spot");
      const auto answer =
          spot::obligation_synthesis (objective, dict, outputs, !synthesis, true, -1);
      spot_call.finish ();
      const auto value = completed (answer, !synthesis);
      if (!synthesis)
        return {value, value == outcome::real || value == outcome::unreal ? "completed"
                                                                          : "invalid_return"};
      if (value != outcome::real)
        return {value == outcome::unreal ? outcome::inapplicable : outcome::error,
                value == outcome::unreal ? "no_strategy" : "invalid_return"};
      acacia::synthesis::await_selection (EXIT_CODE_REAL);
      worker_stage ("obligation_export");
      accounting_phase strategy ("obligation_strategy");
      const auto mealy = spot::mtdswa_strategy_to_mealy (answer, false, false);
      if (!mealy || !mealy->num_states ())
        return {outcome::error, "strategy_export_failed"};
      auto circuit = spot::mealy_machines_to_aig (std::vector<spot::twa_graph_ptr> {mealy}, "isop",
                                                  inputs, {outputs});
      if (convert != conversion::none)
        circuit =
            acacia::synthesis::mealy_to_moore (circuit, convert == conversion::undo_input_shift);
      return {outcome::real, "completed", std::move (circuit)};
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

  uint64_t rss_share (size_t workers, memory_share policy) {
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
    return rss_share_for_limit (cap, workers, policy);
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
