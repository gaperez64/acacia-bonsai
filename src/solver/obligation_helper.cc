#include "solver/obligation_helper.hh"

#include "solver/controller_check.hh"
#include "solver/mealy_to_moore.hh"
#include <unordered_set>

#include <algorithm>
#include <new>
#include <spot/twaalgos/translate.hh>
#include <sstream>
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
               bool independent_checked, int worker, pid_t pid) noexcept {
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
                  "\"trust\":\"%s\",\"same_library_checked\":false,\"independent_checked\":%s}\n",
                  event, worker, long (pid), name (value), reason, (unsigned long long) now,
                  (unsigned long long) (start_ns ? now - start_ns : 0),
                  independent_checked ? "independent_controller_check" : "trusted_exact_spot",
                  independent_checked ? "true" : "false");
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

  bool check_controller (spot::formula objective, const spot::aig_ptr& controller, bool moore) {
    if (!objective || !controller)
      return false;
    if (moore) {
      std::vector<bool> input_dependent (controller->max_var () / 2 + 1, false);
      for (unsigned i = 0; i < controller->num_inputs (); ++i)
        input_dependent[controller->input_var (i) / 2] = true;
      for (unsigned i = 0; i < controller->num_gates (); ++i) {
        const auto [left, right] = controller->gates ()[i];
        input_dependent[controller->gate_var (i) / 2] =
            input_dependent[left / 2] || input_dependent[right / 2];
      }
      for (const unsigned output : controller->outputs ())
        if (input_dependent[output / 2])
          return false;
    }
    const auto machine = controller->as_automaton (false);
    spot::translator translator (machine->get_dict ());
    translator.set_type (spot::postprocessor::BA);
    const auto negated = translator.run (spot::formula::Not (objective));
    return acacia::synthesis::controller_satisfies (negated, controller);
  }

  result solve (spot::formula objective, const std::vector<std::string>& inputs,
                const std::vector<std::string>& outputs, timing semantics, bool synthesis,
                conversion convert, spot::formula controller_objective) {
    if (!eligible (objective))
      return {outcome::inapplicable, "non_obligation"};
    try {
      // Spot 2.16 sizes its output-AP bitmap before constructing names from
      // outvars. Keep even absent output APs alive first, without BDD registration.
      std::vector<spot::formula> output_aps;
      output_aps.reserve (outputs.size ());
      for (const auto& output : outputs)
        output_aps.push_back (spot::formula::ap (output));
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
      auto answer = spot::obligation_synthesis (objective, dict, outputs, !synthesis, true, -1);
      const auto value = completed (answer, !synthesis);
      if (!synthesis)
        return {value, value == outcome::real || value == outcome::unreal ? "completed"
                                                                          : "invalid_return"};
      if (value != outcome::real)
        return {value == outcome::unreal ? outcome::inapplicable : outcome::error,
                value == outcome::unreal ? "no_strategy" : "invalid_return"};
      worker_stage ("obligation_export");
      const auto mealy = spot::mtdswa_strategy_to_mealy (answer, false, false);
      if (!mealy || !mealy->num_states ())
        return {outcome::error, "strategy_export_failed"};
      auto circuit = spot::mealy_machines_to_aig (std::vector<spot::twa_graph_ptr> {mealy}, "isop",
                                                  inputs, {outputs});
      if (convert != conversion::none)
        circuit =
            acacia::synthesis::mealy_to_moore (circuit, convert == conversion::undo_input_shift);
      std::ostringstream exported;
      spot::print_aiger (exported, circuit);
      if (!exported)
        return {outcome::error, "controller_export_failed"};
      // Check the actual serialized controller, including conversion/reset, in
      // the original target frame. The general translator does not use MTBDDs.
      std::istringstream serialized (exported.str ());
      auto checked = spot::aig::parse_aag (serialized, "obligation controller");
      if (!checked || checked->input_names () != inputs || checked->output_names () != outputs)
        return {outcome::error, "controller_interface_failed"};
      worker_stage ("obligation_independent_check");
      if (!check_controller (controller_objective ? controller_objective : objective, checked,
                             semantics == timing::moore || convert != conversion::none))
        return {outcome::error, "controller_check_failed"};
      return {outcome::real, "completed", true, exported.str ()};
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
