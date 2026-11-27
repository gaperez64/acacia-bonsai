#include "arg_parser.hh"
#include "configuration.hh"
#include "error_msg.hh"
#include "native_gr1_arm.hh"
#include "native_gr1_lift_arm.hh"
#include "native_param_lift_arm.hh"
#include "native_support.hh"
#include "parity_arm.hh"
#include "phase_records.hh"
#include "solver/controller_writer.hh"
#include "solver/obligation_helper.hh"
#include "solver/realizability_simplify.hh"
#include "solver/solver_invoker.hh"
#include "symcount_arm.hh"

#include <spot/tl/parse.hh>
#ifdef __linux__
# include <sys/prctl.h>
#endif
#include <string_view>
#include <unordered_map>

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <optional>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utils/verbose.hh>
#include <vector>

#include <posets/vectors/traits.hh>

using namespace std::literals;

// Definitions for some external/global variables.
unsigned utils::verbose = 0;
utils::voutstream utils::vout;
size_t posets::vectors::bool_threshold = 0;

namespace {
  volatile pid_t* g_child_pids = nullptr;
  volatile sig_atomic_t* g_child_interrupted = nullptr;
  volatile sig_atomic_t g_child_count = 0;
  volatile sig_atomic_t g_interrupted = 0;
  volatile pid_t g_publisher_pid = 0;
  pid_t g_main_pid = 0;
  acacia::worker_record* g_worker_records = nullptr;
  acacia::obligation::control* g_obligation = nullptr;
  acacia::synthesis::rendezvous_slot* g_synthesis = nullptr;
  std::string g_controller_directory;
  std::vector<std::string> g_controller_paths;
  sig_atomic_t g_eligibility_source = -1, g_helper_index = -1;
  bool g_eligibility_observed = false;
  uint64_t g_helper_rss_share = 0, g_parity_rss_share = 0;
  std::vector<sig_atomic_t> g_parity_workers;
  std::vector<sig_atomic_t> g_rss_exhausted;
  acacia::obligation::outcome g_helper_forced = acacia::obligation::outcome::pending;
  const char* g_helper_stop_reason = "none";

  void cleanup_controllers () {
    if (getpid () != g_main_pid)
      return;
    for (const auto& path : g_controller_paths)
      unlink (path.c_str ());
    if (!g_controller_directory.empty ())
      rmdir (g_controller_directory.c_str ());
  }

  void reap_worker_descendant (acacia::worker_record& record, bool blocking = true);

  void record_parent_terminal (sig_atomic_t index, int status, const char* reason, pid_t pid,
                               const rusage* usage, uint64_t reaped_mono_ns) {
    // I11: callers have reaped this worker before reading its final snapshot.
    // terminal/verified and telemetry completeness describe evidence only;
    // the verdict path must still check WIFEXITED and the accepted exit code.
    // Interrupted workers can be reaped by either the ordinary loop or cleanup.
    // Preserve that cancellation cause, but keep normally exited, rejected verdicts distinct.
    if (g_child_interrupted[index] && WIFSIGNALED (status) && WTERMSIG (status) == SIGKILL)
      reason = "interrupted";
    if (g_obligation && index == g_eligibility_source &&
        g_obligation->eligible.load (std::memory_order_acquire) ==
            acacia::obligation::eligibility::pending)
      g_obligation->eligible.store (acacia::obligation::eligibility::error,
                                     std::memory_order_release);
    if (g_obligation && index == g_helper_index) {
      using acacia::obligation::outcome;
      auto value = g_obligation->result.load (std::memory_order_acquire);
      const char* helper_reason = reason;
      if (g_helper_forced != outcome::pending) {
        value = g_helper_forced;
        helper_reason = g_helper_stop_reason;
      }
      else if (value == outcome::pending)
        value = std::strcmp (reason, "signal") == 0 || std::strcmp (reason, "exit") == 0
                    ? outcome::error : outcome::cancelled;
      if (g_helper_forced != outcome::pending ||
          g_obligation->result.load (std::memory_order_acquire) == outcome::pending)
        acacia::obligation::record ("completion", value, helper_reason, g_obligation->start_ns,
                                    int (index), pid);
      g_obligation->result.store (value, std::memory_order_release);
    }
    if (!g_worker_records)
      return;
    auto& record = g_worker_records[index];
    if (std::ranges::find (g_rss_exhausted, index) != g_rss_exhausted.end ()) {
      reason = "rss_share";
      if (std::ranges::find (g_parity_workers, index) != g_parity_workers.end ())
        acacia::parity_arm_exhausted (record);
    }
    record.pid = pid;
    const char* telemetry = record.dropped    ? "dropped"
                            : record.terminal ? "complete"
                                              : "incomplete";
    reap_worker_descendant (record, false);
    if (record.interruption.mono_ns) {
      acacia::resource_event (record, "interrupted", true, &record.interruption);
      record.interruption.mono_ns = 0;
    }
    acacia::worker_reaped (record, usage, status, reason, reaped_mono_ns);
    acacia::weakening_cancelled (record, reason);
    acacia::worker_event (record, "parent_terminal", reason,
                          WIFEXITED (status) ? WEXITSTATUS (status) : -1,
                          WIFSIGNALED (status) ? WTERMSIG (status) : 0, telemetry);
  }

  void terminate ([[maybe_unused]] int signum) {
    const int saved_errno = errno;
    // Keep the handler async-signal-safe; reaping, records and writer closure
    // run on the ordinary parent path after the interruption flag is seen.
    if (getpid () == g_main_pid) {  // Main process
      g_interrupted = 1;
      if (g_publisher_pid > 0)
        kill (g_publisher_pid, SIGKILL);
      for (sig_atomic_t i = 0; i < g_child_count; ++i)
        if (g_child_pids[i] > 0) {
          g_child_interrupted[i] = 1;
          if (g_worker_records && !g_worker_records[i].interruption.mono_ns)
            g_worker_records[i].interruption = acacia::resource_snapshot (g_child_pids[i]);
          kill (-g_child_pids[i], SIGKILL);
          kill (g_child_pids[i], SIGKILL);
        }
    }
    else
      _exit (EXIT_CODE_UNKNOWN);  // child procs avoid cleaning on exit
    errno = saved_errno;
  }

  void kill_child_groups (const char* reason) {
    if (g_publisher_pid > 0)
      kill (g_publisher_pid, SIGKILL);
    for (sig_atomic_t i = 0; i < g_child_count; ++i)
      if (g_child_pids[i] > 0) {
        if (g_worker_records) {
          auto record = g_worker_records[i];
          record.pid = g_child_pids[i];
          if (std::strcmp (reason, "interrupted") == 0 && record.interruption.mono_ns) {
            acacia::resource_event (record, reason, true, &record.interruption);
            g_worker_records[i].interruption.mono_ns = 0;
          }
          else
            acacia::resource_event (record, reason, true);
        }
        kill (-g_child_pids[i], SIGKILL);
        // Also cover the fork-to-setpgid window if group creation failed.
        kill (g_child_pids[i], SIGKILL);
      }
  }

  void reap_worker_descendant (acacia::worker_record& record, bool blocking) {
    const pid_t pid = record.descendant_pid;
    if (pid <= 0)
      return;
    int status = 0;
    rusage usage {};
    pid_t got;
    do {
      got = wait4 (pid, &status, blocking ? 0 : WNOHANG, &usage);
    } while (got < 0 && errno == EINTR);
    if (got == 0)
      return;
    if (got != pid)
      record.descendant_usage_complete = false;
    acacia::descendant_record (record, pid, got == pid ? &usage : nullptr, status, got != pid);
    record.descendant_pid = -1;
  }

  void reap_descendants () {
    if (g_worker_records)
      for (sig_atomic_t i = 0; i < g_child_count; ++i)
        reap_worker_descendant (g_worker_records[i]);
#ifdef __linux__
    // Also cover death between fork() and publishing descendant_pid. An adopted
    // child's process group still identifies its arm even after its PPID changes.
    char path[96];
    snprintf (path, sizeof path, "/proc/self/task/%ld/children", long (getpid ()));
    bool remaining = true;
    while (remaining) {
      remaining = false;
      FILE* children = fopen (path, "r");
      if (!children)
        break;
      long pid;
      while (fscanf (children, "%ld", &pid) == 1) {
        if (pid == acacia::phase_records_writer_pid ())
          continue;
        remaining = true;  // Killing an adoptee may expose more descendants.
        const pid_t group = getpgid (pid);
        acacia::worker_record* record = nullptr;
        for (sig_atomic_t i = 0; i < g_child_count; ++i)
          if ((g_worker_records ? g_worker_records[i].pid : g_child_pids[i]) == group) {
            kill (-group, SIGKILL);
            if (g_worker_records)
              record = &g_worker_records[i];
            break;
          }
        kill (pid, SIGKILL);
        int status = 0;
        rusage usage {};
        pid_t got;
        do {
          got = wait4 (pid, &status, 0, &usage);
        } while (got < 0 && errno == EINTR);
        if (record)
          acacia::descendant_record (*record, pid, got == pid ? &usage : nullptr, status,
                                     got != pid);
      }
      fclose (children);
    }
#endif
  }

  void reap_remaining_children (const char* reason) {
    // SIGKILL cannot be ignored, but a child may not be scheduled immediately.
    // Poll each known PID so cleanup never waits in a blocking waitpid call.
    while (true) {
      bool remaining = false;
      for (sig_atomic_t i = 0; i < g_child_count; ++i) {
        const pid_t pid = g_child_pids[i];
        if (pid <= 0)
          continue;
        int status = 0;
        rusage usage {};
        const pid_t reaped = wait4 (pid, &status, WNOHANG, &usage);
        if (reaped == pid || (reaped == -1 && errno == ECHILD)) {
          record_parent_terminal (i, reaped == pid ? status : 0xffff, reason, pid,
                                  reaped == pid ? &usage : nullptr,
                                  g_worker_records ? acacia::phase_clock (CLOCK_MONOTONIC) : 0);
          g_child_pids[i] = 0;
        }
        else
          remaining = true;
      }
      if (g_publisher_pid > 0) {
        const pid_t reaped = waitpid (g_publisher_pid, nullptr, WNOHANG);
        if (reaped > 0 || (reaped == -1 && errno == ECHILD))
          g_publisher_pid = 0;
        else
          remaining = true;
      }
      if (!remaining) {
        reap_descendants ();
        return;
      }
      timespec pause {0, 1000000};
      nanosleep (&pause, nullptr);
    }
  }

  void stop_children (const char* reason) {
    kill_child_groups (reason);
    reap_remaining_children (reason);
  }

  const char* unreal_strategy_name (UNREAL_X_T strategy) {
    if (strategy == UNREAL_X_FORMULA)
      return "formula";
    if (strategy == UNREAL_X_AUTOMATON)
      return "automaton";
    return "unknown";
  }

  uint64_t monotonic_ns () {
    timespec now {};
    if (clock_gettime (CLOCK_MONOTONIC, &now) != 0)
      return 0;
    return uint64_t (now.tv_sec) * 1000000000ULL + now.tv_nsec;
  }

  uint64_t outer_deadline_ns () {
    const char* value = std::getenv ("ACACIA_OUTER_DEADLINE_MONOTONIC");
    if (!value || !*value)
      return 0;
    char* end = nullptr;
    errno = 0;
    const long double seconds = std::strtold (value, &end);
    if (errno || end == value || *end || !std::isfinite (seconds) || seconds <= 0 ||
        seconds >= static_cast<long double> (UINT64_MAX) / 1000000000.0L)
      error (EXIT_CODE_ERROR, "Error: invalid ACACIA_OUTER_DEADLINE_MONOTONIC.\n");
    return std::max<uint64_t> (1, static_cast<uint64_t> (seconds * 1000000000.0L));
  }

#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
  // Test-only fake children exercise the parent without invoking a solver.
  void test_child_behavior (sig_atomic_t index, uint64_t deadline_mono_ns) {
    const char* value = std::getenv ("ACACIA_TEST_CHILD_MODES");
    if (!value)
      return;
    // A single pipe write survives fast exit/reaping and records the actual group.
    char report[128];
    const int length = snprintf (report, sizeof report,
                                 "{\"stage\":\"child_group\",\"pid\":%ld,\"pgid\":%ld}\n",
                                 long (getpid ()), long (getpgrp ()));
    ssize_t written;
    do {
      written = write (STDERR_FILENO, report, size_t (length));
    } while (written < 0 && errno == EINTR);
    if (written != length)
      acacia::worker_exit (EXIT_CODE_ERROR);
    std::string_view mode {value};
    for (sig_atomic_t i = 0; i < index; ++i) {
      const auto comma = mode.find (',');
      if (comma == std::string_view::npos)
        return;
      mode.remove_prefix (comma + 1);
    }
    mode = mode.substr (0, mode.find (','));
    if (mode == "decline") {
      acacia::accounting_phase phase ("fake_decline_phase");
      acacia::worker_decline ("optional_route_unavailable");
      phase.finish ();
      acacia::worker_terminal (EXIT_CODE_UNKNOWN);
      acacia::worker_exit (EXIT_CODE_UNKNOWN);
    }
    if (mode == "descendant-stall") {
      const pid_t descendant = fork ();
      if (descendant == 0)
        while (true)
          pause ();
      if (descendant < 0)
        acacia::worker_exit (EXIT_CODE_ERROR);
      if (auto* w = acacia::active_worker_record ()) {
        w->descendant_pid = descendant;
        acacia::descendant_record (*w, descendant, nullptr);
      }
      mode = "stall";
    }
    if (mode == "flood-stall") {
      constexpr char flood[] = "{\"event\":\"flood\"}\n";
      for (unsigned i = 0; i < 100000; ++i)
        acacia::phase_records_send (flood, sizeof flood - 1);
      mode = "stall";
    }
    if (mode == "stall") {
      acacia::accounting_phase phase ("fake_unfinished_phase");
      signal (SIGTERM, SIG_IGN);
      while (true)
        pause ();
    }
    if (mode == "real-delayed") {
      timespec delay {0, 100000000};
      nanosleep (&delay, nullptr);
      acacia::worker_exit (EXIT_CODE_REAL);
    }
    if (mode == "real-late" && deadline_mono_ns) {
      while (monotonic_ns () < deadline_mono_ns + 20000000ULL) {
        timespec delay {0, 1000000};
        nanosleep (&delay, nullptr);
      }
      acacia::worker_exit (EXIT_CODE_REAL);
    }
    if (mode.starts_with ("wait:")) {
      char byte;
      ssize_t count;
      do {
        count = read (std::atoi (mode.data () + 5), &byte, 1);
      } while (count < 0 && errno == EINTR);
      if (count != 1)
        acacia::worker_exit (EXIT_CODE_ERROR);
    }
    if (mode == "signal") {
      raise (SIGKILL);
      acacia::worker_exit (EXIT_CODE_ERROR);
    }
    if (mode == "real")
      acacia::worker_exit (EXIT_CODE_REAL);
  }
#endif

  spot::formula helper_objective (const arg_parse_result& args,
                                 const acacia::tlsf_frontend::specification& spec) {
    validate_ltl_partition (spec.inputs, spec.outputs);
    auto parsed = spot::parse_infix_psl (spec.formula, spot::default_environment::instance (),
                                       false, false);
    if (!parsed.f || !parsed.errors.empty ())
      throw std::runtime_error ("unable to parse original obligation objective");
    auto objective = parsed.f;
    if (!args.synth_fname)
      acacia::realizability::apply_simplifier (objective, spec.inputs);
    return objective;
  }

  spot::formula helper_objective (const arg_parse_result& args) {
    acacia::tlsf_frontend::specification child_local;
    return helper_objective (args, args.prepare_objective (child_local));
  }

  [[noreturn]] void dispatch_helper (const arg_parse_result& args) {
    using acacia::obligation::outcome;
    acacia::worker_route ("original_game", "obligation", "unknown");
    acacia::worker_stage ("obligation_preprocessing");
    acacia::obligation::result result;
    try {
      if (!acacia::obligation::limit_address_space ())
        result = {outcome::error, "address_space_limit"};
      else {
        acacia::accounting_phase preparation ("obligation_preprocessing");
        acacia::tlsf_frontend::specification child_local;
        const auto& spec = args.prepare_objective (child_local);
        auto conversion = acacia::obligation::conversion::none;
        if (args.tlsf_specified && args.synth_fname && spec.metadata.tlsf_target == "Moore")
          conversion = spec.metadata.tlsf_semantics.find ("Moore") != std::string::npos
                           ? acacia::obligation::conversion::undo_input_shift
                           : acacia::obligation::conversion::delay_outputs;
        // The linked frontend already lowered Moore/strict TLSF to effective
        // Mealy. Solve that positive normalized objective once, without a swap
        // or another X shift. Raw API Moore order is exercised in unit tests.
        const auto objective = helper_objective (args, spec);
        preparation.finish ();
        if (g_obligation->eligible.load (std::memory_order_acquire) ==
            acacia::obligation::eligibility::pending)
          acacia::obligation::publish_eligibility (g_obligation, objective);
        if (!acacia::obligation::eligible (objective))
          result = {outcome::inapplicable, "non_obligation"};
        else {
          acacia::worker_stage ("obligation_synthesis");
          result = acacia::obligation::solve (
              objective, spec.inputs, spec.outputs, acacia::obligation::timing::mealy,
              args.synth_fname.has_value (), conversion, args.aiger_mode);
          if (result.value == outcome::real && args.synth_fname) {
            acacia::accounting_phase output ("obligation_output");
            try {
              if (!acacia::synthesis::write_controller (*args.synth_fname, result.controller))
                result = {outcome::error, "controller_export_failed"};
            } catch (const std::bad_alloc&) {
              result = {outcome::exhausted, "allocation"};
            } catch (...) {
              result = {outcome::error, "controller_export_failed"};
            }
          }
        }
      }
    } catch (const std::bad_alloc&) {
      result = {outcome::exhausted, "allocation"};
    } catch (...) {
      result = {outcome::error, "preprocessing_exception"};
    }
    acacia::obligation::record ("completion", result.value, result.reason,
                                g_obligation->start_ns);
    const int code = acacia::obligation::exit_code (result.value);
    acacia::worker_route ("original_game", "obligation",
                          result.value == outcome::real     ? "REAL"
                          : result.value == outcome::unreal ? "UNREAL"
                                                            : "unknown");
    acacia::worker_terminal (code, result.reason);
    acacia::phase_records_summary ("both:obligation:spot");
    g_obligation->result.store (result.value, std::memory_order_release);
    acacia::worker_exit (code);
  }

  [[noreturn]] void dispatch_child (const portfolio_arm& arm, const arg_parse_result& arg_values,
                                    uint64_t deadline_mono_ns,
                                    acacia::obligation::control* eligibility_channel) {
#if ACACIA_HAVE_ABC
    // Private extraction must finish before optional work can consume its budget.
    if (arg_values.synth_fname && arg_values.aiger_abc && g_controller_paths.empty ())
      acacia::synthesis::aiger_abc = {
          true, deadline_mono_ns,
          arm.kind == portfolio_arm_kind::obligation ? g_helper_rss_share
          : arm.kind == portfolio_arm_kind::parity || arm.kind == portfolio_arm_kind::symcount
              ? g_parity_rss_share
              : acacia::obligation::rss_share (arg_values.arms->size ())};
#endif
    if (arm.kind == portfolio_arm_kind::obligation)
      dispatch_helper (arg_values);
    if (arm.kind == portfolio_arm_kind::parity) {
      if (eligibility_channel) {
        try {
          acacia::obligation::publish_eligibility (eligibility_channel,
                                                  helper_objective (arg_values));
        } catch (...) {
          eligibility_channel->eligible.store (acacia::obligation::eligibility::error,
                                               std::memory_order_release);
        }
      }
      const int code =
          acacia::run_parity_arm (arg_values, arm, deadline_mono_ns, g_parity_rss_share);
      acacia::worker_terminal (code);
      acacia::phase_records_summary (arm.native_name ());
      acacia::worker_exit (code);
    }
    if (arm.kind != portfolio_arm_kind::legacy) {
      try {
        const bool count = arm.kind == portfolio_arm_kind::symcount;
        acacia::tlsf_frontend::captured_specification count_local;
        const acacia::tlsf_frontend::specification* count_objective = nullptr;
        if (count) {
          acacia::worker_route ("original_game", "symcount", "unknown");
          acacia::worker_stage ("symcount");
          if (acacia::obligation::limit_address_space ())
            count_objective = &arg_values.prepare_count_objective (count_local);
          else
            acacia::worker_stopped ("address_space_limit");
        }
        // A native-only portfolio has no legacy normalization boundary. Use one
        // existing child to prepare eligibility, while its siblings already run.
        if (eligibility_channel) {
          try {
            if (count && !count_objective)
              eligibility_channel->eligible.store (acacia::obligation::eligibility::error,
                                                   std::memory_order_release);
            else
              acacia::obligation::publish_eligibility (
                  eligibility_channel, count_objective
                                           ? helper_objective (arg_values, *count_objective)
                                           : helper_objective (arg_values));
          } catch (...) {
            eligibility_channel->eligible.store (acacia::obligation::eligibility::error,
                                                   std::memory_order_release);
          }
        }
# ifdef ACACIA_NATIVE_TEST_HOOKS
        acacia::native_attribution_test_exception ();
# endif
        const int result =
            arm.kind == portfolio_arm_kind::gr1
                ? acacia::run_native_gr1_arm (arg_values, arm.unreal, arm.both, deadline_mono_ns)
            : arm.kind == portfolio_arm_kind::gr1_lift ||
                    arm.kind == portfolio_arm_kind::gr1_real_lift
                ? acacia::run_native_gr1_lift_arm (arg_values, deadline_mono_ns,
                                                   arm.kind == portfolio_arm_kind::gr1_real_lift)
            : count_objective ? acacia::run_symcount_arm (*count_objective, deadline_mono_ns)
            : count           ? EXIT_CODE_UNKNOWN
                              : acacia::run_native_param_lift_arm (arg_values, deadline_mono_ns);
        if (result != EXIT_CODE_REAL && result != EXIT_CODE_UNREAL &&
            acacia::active_worker_record () &&
            std::strcmp (acacia::active_worker_record ()->reason, "none") == 0)
          acacia::worker_stopped ("inconclusive");
        acacia::worker_terminal (result);
        acacia::phase_records_summary (arm.native_name ());
        acacia::worker_exit (result);
      } catch (const std::bad_alloc& exception) {
        acacia::native_arm_diagnostic (arm.native_name (), "native_exception", -1,
                                       exception.what (),
                                       acacia::native_failure_category::resource);
      } catch (const std::exception& exception) {
        acacia::native_arm_diagnostic (arm.native_name (), "native_exception", -1,
                                       exception.what ());
      } catch (...) {
        acacia::native_arm_diagnostic (arm.native_name (), "native_exception", -1,
                                       "unknown exception");
      }
      if (eligibility_channel && arm.kind == portfolio_arm_kind::symcount) {
        auto pending = acacia::obligation::eligibility::pending;
        eligibility_channel->eligible.compare_exchange_strong (
            pending, acacia::obligation::eligibility::error, std::memory_order_release);
      }
      acacia::worker_terminal (EXIT_CODE_UNKNOWN);
      acacia::phase_records_summary ("native");
      acacia::worker_exit (EXIT_CODE_UNKNOWN);
    }
    acacia::selected_oracle_layout = arg_values.oracle_layout;
    auto backend = arm.legacy->backend;
    auto provider = arm.legacy->provider;
    if (arg_values.synth_fname.has_value () and not arm.unreal and
        backend != acacia::game_backend::backward) {
      verb_do (1, vout << "Forcing the real backend to backward for synthesis\n" << std::flush);
      backend = acacia::synthesis_backend (backend, true);
      provider = acacia::synthesis_provider (provider, true);
    }
    acacia::worker_route (
        arm.unreal
            ? (arm.legacy->unreal_x == UNREAL_X_FORMULA ? "unreal_formula" : "unreal_automaton")
            : "real",
        acacia::game_backend_name (backend), "REAL");
    const auto unreal_x =
        arm.unreal ? std::make_optional<UNREAL_X_T> (arm.legacy->unreal_x) : std::nullopt;
    const auto translation_pref = arm.legacy->translation_pref;
    // we check one thing at a time here
    assert (not unreal_x.has_value () or *unreal_x != UNREAL_X_BOTH);
    utils::vout.set_prefix (std::string {"["} +
                            (not unreal_x.has_value ()
                                 ? std::string {"real="} + translation_pref_name (translation_pref)
                                 : std::string {"unreal="} + unreal_strategy_name (*unreal_x) +
                                       ",pref=" + translation_pref_name (translation_pref)) +
                            ",backend=" + acacia::game_backend_name (backend) + "] ");
    verb_do (
        1, vout << "Starting solver child provider=" << acacia::automaton_provider_name (provider)
                << " translation_effort=" << translation_effort_name (arm.legacy->translation_effort)
                << " candidate_mode=" << acacia::candidate_mode_name (arg_values.candidate) << "\n"
                << std::flush);
    const auto restriction_arm = std::ranges::find_if (*arg_values.arms, [&] (const auto& other) {
      return other.legacy &&
             (arm.unreal ? other.unreal && other.legacy->unreal_x == UNREAL_X_FORMULA
                         : !other.unreal);
    });
    // shortcut: decision only; add output/counterstrategy decoding before enabling -s.
    const bool restrictions =
        arg_values.restriction_prepass && arg_values.tlsf_specified && !arg_values.synth_fname &&
        restriction_arm != arg_values.arms->end () && &arm == &*restriction_arm &&
        (!arm.unreal || arm.legacy->unreal_x == UNREAL_X_FORMULA);
    const bool res = run_ltl (
        arg_values.objective.inputs, arg_values.objective.outputs, arg_values.opt_k,
        arg_values.opt_kmin, arg_values.opt_kinc, arg_values.objective.formula, unreal_x,
        translation_pref, arm.legacy->translation_effort, arg_values.spot_fast, backend,
        (unreal_x.has_value () and *unreal_x != UNREAL_X_FORMULA) ? std::nullopt
                                                                  : arg_values.synth_fname,
        arg_values.objective.metadata, provider, arg_values.candidate, arg_values.equivariance,
        deadline_mono_ns, arg_values.tlsf_sha256, arg_values.weakening, eligibility_channel,
        arg_values.arms->size (), restrictions, arg_values.aiger_mode);
    if (res && g_synthesis)
      acacia::synthesis::await_selection (arm.unreal ? EXIT_CODE_UNREAL : EXIT_CODE_REAL);
    verb_do (1, vout << "returning " << res << "\n");

    if (!res && acacia::active_worker_record () &&
        std::strcmp (acacia::active_worker_record ()->reason, "none") == 0)
      acacia::worker_stopped ("inconclusive");
    acacia::worker_terminal (res ? (arm.unreal ? EXIT_CODE_UNREAL : EXIT_CODE_REAL)
                                 : EXIT_CODE_UNKNOWN);
    acacia::phase_records_summary ("legacy");
    if (unreal_x.has_value ())
      exit (res ? EXIT_CODE_UNREAL : EXIT_CODE_UNKNOWN);
    else
      exit (res ? EXIT_CODE_REAL : EXIT_CODE_UNKNOWN);
  }

  void launch_child (const portfolio_arm& arm, const arg_parse_result& arg_values,
                     uint64_t deadline_mono_ns, const sigset_t& block_set,
                     acacia::obligation::control* eligibility_channel = nullptr) {
    // Publish the child PID before a termination handler can run.
    sigset_t old_mask;
    sigprocmask (SIG_BLOCK, &block_set, &old_mask);
    if (g_worker_records) {
      auto& record = g_worker_records[g_child_count];
      record = acacia::worker_record {};
      record.index = unsigned (g_child_count);
      record.r_prepass = arg_values.r_prepass;
      record.equivariance = arg_values.equivariance;
      const char* backend = arm.legacy ? acacia::game_backend_name (arm.legacy->backend)
                            : arm.kind == portfolio_arm_kind::obligation ? "obligation"
                            : arm.kind == portfolio_arm_kind::parity ? "parity"
                            : arm.kind == portfolio_arm_kind::symcount ? "symcount"
                                                                         : "oxidd";
      acacia::worker_record_text (record.requested_backend, backend);
      acacia::worker_record_text (record.effective_backend, backend);
      acacia::worker_record_text (record.original_polarity, arm.both     ? "both"
                                                            : arm.unreal ? "UNREAL"
                                                                         : "REAL");
      acacia::worker_record_text (record.proof_polarity, arm.legacy ? "REAL" : "unknown");
    }
    const acacia::phase_stamp fork_start = acacia::phase_start ();
    if (g_worker_records)
      g_worker_records[g_child_count].start_mono_ns = fork_start.wall;
    const auto parent_context =
        g_worker_records ? g_worker_records[g_child_count] : acacia::worker_record {};
    const pid_t pid = fork ();
    if (pid == 0) {
#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
      acacia::synthesis::test_worker_started (arm.kind == portfolio_arm_kind::obligation,
                                               arm.kind == portfolio_arm_kind::parity);
      if (g_synthesis)
        acacia::synthesis::test_before_worker_protection ();
#endif
#ifdef __linux__
      // SIGKILL also terminates suspended synthesis competitors. Recheck the
      // parent to cover its death between fork and installing the signal.
      if (prctl (PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid () != g_main_pid)
        acacia::worker_exit (EXIT_CODE_UNKNOWN);
#endif
      if (g_synthesis) {
        acacia::synthesis::current_slot = &g_synthesis[g_child_count];
        g_synthesis[g_child_count].suspend_ready.store (true, std::memory_order_release);
      }
      acacia::phase_records_init ();
      if (g_worker_records) {
        acacia::active_worker_record () = &g_worker_records[g_child_count];
        acacia::active_worker_record ()->pid = getpid ();
        atexit ([] {
          if (auto* w = acacia::active_worker_record ())
            acacia::worker_event (*w, "exit");
        });
        acacia::worker_event (*acacia::active_worker_record (), "worker_start");
        acacia::worker_route ("dispatch");
      }
      const acacia::phase_stamp child_start =
          fork_start.wall ? acacia::phase_stamp {fork_start.wall,
                                                 acacia::phase_clock (CLOCK_PROCESS_CPUTIME_ID)}
                          : acacia::phase_stamp {};
      if (setpgid (0, 0) != 0)
        acacia::worker_exit (EXIT_CODE_ERROR);
      sigprocmask (SIG_SETMASK, &old_mask, nullptr);
      if (child_start.wall) {
        const char* name = arm.kind == portfolio_arm_kind::legacy ? "legacy" : arm.native_name ();
        acacia::phase_finish (name, "child_startup", child_start);
      }
#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
      test_child_behavior (g_child_count, deadline_mono_ns);
#endif
      if (!g_controller_paths.empty ()) {
        auto private_args = arg_values;
        private_args.synth_fname = g_controller_paths[g_child_count];
        dispatch_child (arm, private_args, deadline_mono_ns, eligibility_channel);
      }
      dispatch_child (arm, arg_values, deadline_mono_ns, eligibility_channel);
    }
    else {
      if (pid > 0) {
        // Also close the fork-to-setpgid window in the parent.
        setpgid (pid, pid);
        if (arm.kind == portfolio_arm_kind::obligation)
          acacia::obligation::record (
              "start", acacia::obligation::outcome::pending,
              eligibility_channel ? "preparation" : "eligible", g_obligation->start_ns,
              int (g_child_count), pid);
        if (g_worker_records) {
          // Use the pre-fork context; the child owns the live snapshot.
          auto spawned = parent_context;
          spawned.pid = pid;
          acacia::worker_event (spawned, "worker_spawn");
          char specification[768];
          const int length = snprintf (
              specification, sizeof specification,
              "{\"event\":\"worker_spec\",\"worker\":%u,\"worker_pid\":%ld,"
              "\"mono_ns\":%llu,\"start_mono_ns\":%llu,\"requested_backend\":\"%s\",\"kind\":\"%"
              "s\",\"requested_polarity\":\"%s\","
              "\"translation\":\"%s\",\"translation_effort\":\"%s\","
              "\"transform\":\"%s\",\"provider\":\"%s\","
              "\"r_prepass\":%s,\"equivariance\":%s}\n",
              spawned.index, long (pid), (unsigned long long) fork_start.wall,
              (unsigned long long) fork_start.wall, spawned.requested_backend,
              arm.legacy ? "legacy" : arm.native_name (), spawned.original_polarity,
              arm.legacy ? translation_pref_name (arm.legacy->translation_pref) : "native",
              arm.legacy ? translation_effort_name (arm.legacy->translation_effort) : "unused",
              arm.legacy ? (arm.unreal ? unreal_strategy_name (arm.legacy->unreal_x) : "real")
                         : "exact",
              arm.legacy ? acacia::automaton_provider_name (arm.legacy->provider) : "native",
              spawned.r_prepass ? "true" : "false", spawned.equivariance ? "true" : "false");
          if (length > 0 && size_t (length) < sizeof specification)
            acacia::phase_records_final (specification, size_t (length));
          else
            acacia::phase_records_drop ();
        }
        if (arm.kind == portfolio_arm_kind::parity || arm.kind == portfolio_arm_kind::symcount)
          g_parity_workers.push_back (sig_atomic_t (g_child_count));
        g_child_pids[g_child_count] = pid;
        g_child_count = g_child_count + 1;
      }
      if (pid < 0 && g_worker_records)
        acacia::worker_event (g_worker_records[g_child_count], "parent_terminal", "fork_failed",
                              -1, 0, "unavailable");
      sigprocmask (SIG_SETMASK, &old_mask, nullptr);
      if (pid > 0 && g_synthesis) {
        // Never suspend a child before it can be killed on parent death.
        auto& slot = g_synthesis[g_child_count - 1];
        while (!slot.suspend_ready.load (std::memory_order_acquire)) {
          if (g_interrupted || (deadline_mono_ns && monotonic_ns () >= deadline_mono_ns))
            return;
          siginfo_t exited {};
          const int observed = waitid (P_PID, pid, &exited, WEXITED | WNOHANG | WNOWAIT);
          if ((observed == 0 && exited.si_pid == pid) || (observed < 0 && errno == ECHILD))
            break;
          if (observed < 0 && errno != EINTR)
            throw std::runtime_error ("unable to initialize synthesis worker");
          timespec pause {0, 1000000};
          nanosleep (&pause, nullptr);
        }
      }
    }
  }

  int run_portfolio (const arg_parse_result& arg_values, uint64_t deadline_mono_ns,
                     const sigset_t& block_set) {
    if (g_synthesis) {
      const auto slash = arg_values.synth_fname->find_last_of ('/');
      g_controller_directory =
          arg_values.synth_fname->substr (0, slash == std::string::npos ? 0 : slash + 1) +
          ".acacia-private-XXXXXX";
      if (mkdtemp (g_controller_directory.data ())) {
        atexit (cleanup_controllers);
        bool usable = chmod (g_controller_directory.c_str (), 0700) == 0;
        for (size_t i = 0;
             usable && i < arg_values.arms->size () + size_t (g_obligation != nullptr); ++i) {
          g_controller_paths.push_back (g_controller_directory + "/" + std::to_string (i));
          // Probe staging and preserve worker writes/parent reads regardless of umask.
          const int file =
              open (g_controller_paths.back ().c_str (), O_CREAT | O_EXCL | O_WRONLY, 0600);
          usable = file >= 0 && fchmod (file, 0600) == 0;
          if (file >= 0 && close (file) != 0)
            usable = false;
        }
        if (!usable) {
          cleanup_controllers ();
          g_controller_paths.clear ();
        }
      }
      if (g_controller_paths.empty ())
        // Retain master's selection and controller-write failure reporting without staging.
        g_controller_directory.clear ();
    }
    // We fork process for each (UN)REAL check now and then wait for them to
    // return to process their exit codes
    setpgid (0, 0);
    assert (getpgid (0) == getpid ());

    [[maybe_unused]] const size_t child_count =
        std::ranges::count_if (*arg_values.arms, [&] (const auto& arm) {
          return arm.kind != portfolio_arm_kind::legacy || arg_values.legacy_available;
        });
    verb_do (1, vout << "Starting " << child_count << " solver children\n" << std::flush);

    if (std::ranges::any_of (*arg_values.arms, [] (const auto& arm) {
          return arm.kind == portfolio_arm_kind::parity || arm.kind == portfolio_arm_kind::symcount;
        }))
      g_parity_rss_share = acacia::obligation::rss_share (
          child_count + size_t (g_obligation != nullptr), arg_values.parity_memory_share);
    std::vector<int> worker_verdict;
    for (const auto& arm : *arg_values.arms) {
      if (arm.kind == portfolio_arm_kind::legacy && !arg_values.legacy_available)
        continue;
      if (deadline_mono_ns && monotonic_ns () >= deadline_mono_ns) {
        std::cerr << "{\"stage\":\"deadline\",\"status\":\"expired\"}\n";
        stop_children ("deadline");
        error (EXIT_CODE_UNKNOWN, "UNKNOWN\n");
      }
      if (g_interrupted) {
        stop_children ("interrupted");
        error (EXIT_CODE_UNKNOWN, "UNKNOWN\n");
      }
      launch_child (
          arm, arg_values, deadline_mono_ns, block_set,
          g_obligation && g_child_count == g_eligibility_source ? g_obligation : nullptr);
      if (g_synthesis && size_t (g_child_count) > worker_verdict.size ())
        worker_verdict.push_back (arm.both ? -1 : arm.unreal ? EXIT_CODE_UNREAL : EXIT_CODE_REAL);
    }

    const auto launch_helper = [&] (bool prepare) {
      g_helper_index = g_child_count;
      g_helper_rss_share = acacia::obligation::rss_share (size_t (g_child_count) + 1,
                                                          arg_values.obligation_memory_share);
      g_obligation->start_ns = monotonic_ns ();
      const auto helper = portfolio_arm::native (false, portfolio_arm_kind::obligation, true);
      verb_do (1, vout << "Starting additional obligation worker\n" << std::flush);
      launch_child (helper, arg_values, deadline_mono_ns, block_set,
                    prepare ? g_obligation : nullptr);
      if (g_child_count == g_helper_index) {
        acacia::obligation::record ("completion", acacia::obligation::outcome::error,
                                    "fork_failed", g_obligation->start_ns);
        g_helper_index = -1;
      }
      else if (g_synthesis)
        worker_verdict.push_back (-1);
    };
    if (g_obligation && g_child_count == 0) {
      validate_ltl_partition (arg_values.objective.inputs, arg_values.objective.outputs);
      // With this arm alone, its existing limits cover preparation as well as solving.
      if (!g_interrupted && (!deadline_mono_ns || monotonic_ns () < deadline_mono_ns))
        launch_helper (true);
    }

    const auto check_acceptance = [&] {
      if (deadline_mono_ns && monotonic_ns () >= deadline_mono_ns) {
        std::cerr << "{\"stage\":\"deadline\",\"status\":\"expired\"}\n";
        stop_children ("deadline");
        error (EXIT_CODE_UNKNOWN, "UNKNOWN\n");
      }
      if (g_interrupted) {
        stop_children ("interrupted");
        error (EXIT_CODE_UNKNOWN, "UNKNOWN\n");
      }
    };
    sig_atomic_t synthesis_winner = -1;
#ifdef __linux__
    const bool extraction_fallback =
        !g_controller_paths.empty () &&
        (acacia::obligation::scope_memory ("memory.max").has_value () || g_parity_rss_share ||
         g_helper_rss_share);
#else
    // Admission needs Linux RSS/anonymous readings; retain master's scheduling elsewhere.
    constexpr bool extraction_fallback = false;
#endif
    int synthesis_verdict = -1;
    sig_atomic_t last_extractor = -1;
    sig_atomic_t original_extractor = -1;
    sig_atomic_t ready_cursor = -1, undecided_cursor = -1;
    bool suspension_pending = false;
    uint64_t extraction_started = 0;
    uint64_t original_rss = 0;
    const size_t slots = arg_values.arms->size () + size_t (g_obligation != nullptr);
    std::vector<uint64_t> extraction_turn (g_synthesis ? slots : 0, 0);
    std::vector<uint64_t> extraction_budget (g_synthesis ? slots : 0, UINT64_MAX);
    std::vector<std::optional<rlim_t>> extraction_soft_limit (g_synthesis ? slots : 0);
    std::vector<uint64_t> extraction_reservation (g_synthesis ? slots : 0, 0);
    std::vector<const char*> pending_discard (g_synthesis ? slots : 0, nullptr);
    const auto extraction_event = [&] (sig_atomic_t i, const char* reason) {
      if (g_worker_records) {
        auto record = g_worker_records[i];
        acacia::worker_event (record, "parent_extraction", reason, synthesis_verdict);
      }
    };
    const auto start_extraction_turn = [&] (sig_atomic_t i) {
      extraction_started = monotonic_ns ();
      original_rss = original_extractor >= 0 && g_child_pids[original_extractor] > 0
                         ? acacia::obligation::resident_bytes (g_child_pids[original_extractor])
                         : 0;
      const auto remaining =
          deadline_mono_ns
              ? (deadline_mono_ns > extraction_started ? deadline_mono_ns - extraction_started : 0)
              : UINT64_MAX;
      if (!extraction_turn[i])
        extraction_turn[i] = deadline_mono_ns ? remaining / 4 : 2000000000ULL;
      extraction_turn[i] = std::min (extraction_turn[i], remaining);
    };
    const auto ready = [&] (sig_atomic_t i) {
      return g_child_pids[i] > 0 && !pending_discard[i] &&
             g_synthesis[i].order.load (std::memory_order_acquire) &&
             (!extraction_fallback || synthesis_verdict < 0 ||
              g_synthesis[i].verdict.load (std::memory_order_acquire) == synthesis_verdict) &&
             std::ranges::find (g_rss_exhausted, i) == g_rss_exhausted.end ();
    };
    const auto undecided = [&] (sig_atomic_t i) {
      return g_child_pids[i] > 0 && !pending_discard[i] &&
             !g_synthesis[i].order.load (std::memory_order_acquire) &&
             (worker_verdict[i] < 0 || worker_verdict[i] == synthesis_verdict) &&
             std::ranges::find (g_rss_exhausted, i) == g_rss_exhausted.end ();
    };
    const auto discard_extractor = [&] (sig_atomic_t i, const char* reason) {
      if (pending_discard[i])
        return;
      pending_discard[i] = reason;
      const pid_t pid = g_child_pids[i];
      kill (-pid, SIGKILL);
      kill (pid, SIGKILL);
    };
    const auto discarding = [&] {
      for (sig_atomic_t i = 0; i < g_child_count; ++i)
        if (pending_discard[i] ||
            (g_worker_records && g_child_pids[i] == 0 && g_worker_records[i].descendant_pid > 0))
          return true;
      return false;
    };
    const auto sole_extractor = [&] (sig_atomic_t index) {
      if (discarding ())
        return false;
      for (sig_atomic_t i = 0; i < g_child_count; ++i)
        if (i != index && g_child_pids[i] > 0)
          return false;
      return true;
    };
    const auto worker_rss_share = [&] (sig_atomic_t index) {
      return index == g_helper_index ? g_helper_rss_share
             : std::ranges::find (g_parity_workers, index) != g_parity_workers.end ()
                 ? g_parity_rss_share
                 : 0;
    };
    const auto admit = [&] (sig_atomic_t index) {
      if (discarding ())
        return false;
      if (sole_extractor (index))
        return true;
      const auto limit = acacia::obligation::scope_memory ("memory.max");
      const auto used = acacia::obligation::scope_memory ("memory.current");
      const auto resident = acacia::obligation::resident_bytes (g_child_pids[index]);
      const auto latent = acacia::obligation::latent_anonymous_bytes (g_child_pids[index]);
      const auto cow = resident;  // RSS conservatively bounds fork-shared anonymous pages.
      const auto growth = std::max<uint64_t> (64 * 1024 * 1024, resident);
      const auto reserve = cow + growth;
      const auto reservation = latent.value_or (0) + reserve;
      const auto current_original =
          original_extractor >= 0 && g_child_pids[original_extractor] > 0
              ? acacia::obligation::resident_bytes (g_child_pids[original_extractor])
              : 0;
      const auto original_growth =
          current_original > original_rss ? current_original - original_rss : 0;
      const auto share = worker_rss_share (index);
      if (!resident || !latent ||
          (limit ? !(used && *used < *limit - *limit / 8 &&
                     reservation < *limit - *limit / 8 - *used &&
                     original_growth < *limit - *limit / 8 - *used - reservation)
                 : !(share && reservation < share && original_growth < share - reservation))) {
        extraction_event (index, "scope_headroom");
        return false;
      }
      if (!acacia::obligation::limit_extraction_address_space (g_child_pids[index], growth,
                                                               extraction_soft_limit[index])) {
        extraction_event (index, "address_space_limit_failed");
        return false;
      }
      extraction_budget[index] = resident + growth;
      extraction_reservation[index] = reservation;
      return true;
    };
    const auto next_extractor = [&] (sig_atomic_t after) {
      for (sig_atomic_t step = 1; step <= g_child_count; ++step) {
        const sig_atomic_t i = (ready_cursor + step) % g_child_count;
        if (g_child_pids[i] > 0 && g_synthesis[i].order.load (std::memory_order_acquire) &&
            g_synthesis[i].verdict.load (std::memory_order_acquire) != synthesis_verdict)
          extraction_event (i, "verdict_conflict");
        if (i != original_extractor && i != after && ready (i) && admit (i))
          return i;
      }
      // Unfinished searches get a turn only after all ready matching proofs are refused.
      for (sig_atomic_t step = 1; step <= g_child_count; ++step) {
        const sig_atomic_t i = (undecided_cursor + step) % g_child_count;
        if (i != original_extractor && i != after && undecided (i) && admit (i))
          return i;
      }
      return sig_atomic_t {-1};
    };
    const auto select_extractor = [&] (sig_atomic_t i) {
      if (discarding ())
        return;
      if (extraction_fallback && last_extractor >= 0 && last_extractor != i)
        extraction_event (i, "fallback");
      synthesis_winner = last_extractor = i;
      suspension_pending = false;
      start_extraction_turn (i);
      auto& slot = g_synthesis[i];
      const bool decided = ready (i);
      (decided ? ready_cursor : undecided_cursor) = i;
      if (decided && !slot.proceed.load (std::memory_order_acquire) && g_worker_records) {
        // Parent observations do not consume the child's live record sequence.
        auto selected = g_worker_records[i];
        acacia::worker_event (selected, "parent_verdict", "selected", synthesis_verdict);
      }
      check_acceptance ();
#ifdef __linux__
      if (!extraction_fallback && !g_controller_paths.empty ())
        extraction_event (i, "no_budget");
#endif
      if (decided)
        slot.proceed.store (true, std::memory_order_release);
      kill (-g_child_pids[i], SIGCONT);
#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
      acacia::synthesis::test_extraction_turn (g_child_pids[i], extraction_turn[i],
                                               extraction_started, deadline_mono_ns);
#endif
    };
    const auto resume_after_failure = [&] {
      synthesis_winner = -1;
      suspension_pending = false;
      if (!extraction_fallback) {
        synthesis_verdict = -1;
        for (sig_atomic_t i = 0; i < g_child_count; ++i)
          if (g_child_pids[i] > 0)
            kill (-g_child_pids[i], SIGCONT);
      }
    };
    int status;
    bool child_reported_error = false;
    while (true) {  // wait only for solver arms; the record writer is separate
      if (g_synthesis)
        for (sig_atomic_t i = 0; i < g_child_count; ++i)
          if (g_child_pids[i] == 0) {
            if (g_worker_records)
              reap_worker_descendant (g_worker_records[i], false);
            if (pending_discard[i] &&
                (!g_worker_records || g_worker_records[i].descendant_pid <= 0)) {
              pending_discard[i] = nullptr;
              unlink (g_controller_paths[i].c_str ());
#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
              acacia::synthesis::test_after_extraction_cleanup ();
#endif
            }
          }
      if (g_obligation && !g_eligibility_observed && synthesis_verdict < 0) {
        using acacia::obligation::eligibility;
        const auto eligible = g_obligation->eligible.load (std::memory_order_acquire);
        if (eligible != eligibility::pending) {
          g_eligibility_observed = true;
          if (eligible != eligibility::inapplicable || arg_values.obligation_explicit)
            acacia::obligation::record (
                "eligibility",
                eligible == eligibility::eligible       ? acacia::obligation::outcome::pending
                : eligible == eligibility::inapplicable ? acacia::obligation::outcome::inapplicable
                                                        : acacia::obligation::outcome::error,
                eligible == eligibility::eligible       ? "eligible"
                : eligible == eligibility::inapplicable ? "non_obligation"
                                                        : "preprocessing_failed");
          if (eligible == eligibility::eligible && g_helper_index < 0 && !g_interrupted &&
              (!deadline_mono_ns || monotonic_ns () < deadline_mono_ns))
            launch_helper (false);
        }
      }
      if (g_synthesis)
        for (sig_atomic_t i = 0; i < g_child_count; ++i)
          if (g_child_pids[i] > 0 && !pending_discard[i] && extraction_budget[i] != UINT64_MAX &&
              sole_extractor (i)) {
            if (!acacia::obligation::limit_extraction_address_space (g_child_pids[i], UINT64_MAX,
                                                                     extraction_soft_limit[i]))
              throw std::runtime_error ("unable to restore sole extractor address space");
            extraction_budget[i] = UINT64_MAX;
            extraction_event (i, "sole_extractor");
          }
      if (g_synthesis && extraction_fallback && original_extractor >= 0 &&
          g_child_pids[original_extractor] > 0 && !discarding ())
        for (sig_atomic_t i = 0; i < g_child_count; ++i) {
          // Stopped fallbacks retain their admission charges until reaped too.
          if (i == original_extractor || g_child_pids[i] <= 0 || !extraction_reservation[i])
            continue;
          const auto limit = acacia::obligation::scope_memory ("memory.max");
          const auto used = acacia::obligation::scope_memory ("memory.current");
          // The last eighth covers the 10 ms poll: crossing it would require the original to
          // allocate > memory.max/8 in one poll, implausible here; fallback growth has RLIMIT_AS.
          if (limit ? !(used && *used < *limit - *limit / 8 &&
                        extraction_reservation[i] < *limit - *limit / 8 - *used)
                    : !worker_rss_share (i)) {
            extraction_event (i, "scope_reserve");
            discard_extractor (i, "scope_reserve");
            if (synthesis_winner == i) {
              synthesis_winner = original_extractor;
              suspension_pending = false;
              start_extraction_turn (original_extractor);
            }
            break;  // Recheck headroom after this victim and its descendants are reaped.
          }
        }
      // Preserve master's helper-before-parity supervision order, also without -s.
      for (sig_atomic_t turn = -1; turn < g_child_count; ++turn) {
        const auto index = turn < 0 ? g_helper_index : turn;
        if (index < 0 || (turn >= 0 && index == g_helper_index))
          continue;
        const pid_t worker = g_child_pids[index];
        if (worker <= 0 || std::ranges::find (g_rss_exhausted, index) != g_rss_exhausted.end ())
          continue;
        const bool helper =
            index == g_helper_index && g_helper_forced == acacia::obligation::outcome::pending;
        const bool parity = std::ranges::find (g_parity_workers, index) != g_parity_workers.end ();
        const auto budget = g_synthesis ? extraction_budget[index] : UINT64_MAX;
        if (!helper && !parity && budget == UINT64_MAX)
          continue;
        const auto resident = acacia::obligation::resident_bytes (worker);
        const auto scope_limit =
            parity && arg_values.parity_memory_share == acacia::obligation::memory_share::headroom
                ? acacia::obligation::scope_memory ("memory.max").value_or (0)
                : 0;
        if (resident > budget || (helper && resident > g_helper_rss_share) ||
            (parity &&
             acacia::obligation::rss_share_exceeded (resident, g_parity_rss_share, scope_limit))) {
          g_rss_exhausted.push_back (index);
          if (helper) {
            g_helper_forced = acacia::obligation::outcome::exhausted;
            g_helper_stop_reason = "rss_share";
          }
          if (resident > budget)
            extraction_event (index, "rss_budget");
          if (g_worker_records) {
            auto r = g_worker_records[index];
            r.pid = worker;
            acacia::resource_event (r, "rss_share", true);
          }
          kill (-worker, SIGKILL);
          kill (worker, SIGKILL);
        }
      }
      siginfo_t finished {};
      if (g_synthesis && synthesis_winner < 0) {
        check_acceptance ();
        sig_atomic_t nominee = -1;
        if (synthesis_verdict < 0 || !extraction_fallback) {
          uint64_t first = UINT64_MAX;
          for (sig_atomic_t i = 0; i < g_child_count; ++i)
            if (ready (i) && g_synthesis[i].order.load (std::memory_order_acquire) < first) {
              nominee = i;
              first = g_synthesis[i].order.load (std::memory_order_acquire);
            }
          if (nominee >= 0) {
#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
            acacia::synthesis::test_before_selection ();
#endif
            synthesis_verdict = g_synthesis[nominee].verdict.load (std::memory_order_acquire);
            original_extractor = nominee;
            for (sig_atomic_t other = 0; other < g_child_count; ++other)
              if (other != nominee && g_child_pids[other] > 0)
                kill (-g_child_pids[other], SIGSTOP);
          }
        }
        else {
          nominee = next_extractor (last_extractor);
          if (nominee < 0 && g_child_pids[original_extractor] > 0) {
            synthesis_winner = original_extractor;
            start_extraction_turn (original_extractor);
          }
          if (nominee < 0 && synthesis_winner < 0 && !discarding ()) {
            // Reclaim unusable workers first; retry admission after they are reaped.
            for (sig_atomic_t i = 0; i < g_child_count; ++i)
              if (g_child_pids[i] > 0 && !ready (i) && !undecided (i)) {
                discard_extractor (i, "extraction_irrelevant");
                break;
              }
            if (!discarding ())
              for (sig_atomic_t i = 0; i < g_child_count; ++i)
                if (undecided (i)) {
                  discard_extractor (i, "extraction_irrelevant");
                  break;
                }
            if (!discarding ()) {
              stop_children ("extraction_failed");
              break;
            }
          }
        }
        if (nominee >= 0)
          select_extractor (nominee);
      }
      if (g_synthesis && extraction_fallback && synthesis_winner >= 0) {
        const auto i = synthesis_winner;
        auto& slot = g_synthesis[i];
        if (slot.order.load (std::memory_order_acquire) &&
            !slot.proceed.load (std::memory_order_acquire)) {
          if (slot.verdict.load (std::memory_order_acquire) != synthesis_verdict) {
            extraction_event (i, "verdict_conflict");
            discard_extractor (i, "verdict_conflict");
            resume_after_failure ();
            continue;
          }
          select_extractor (i);
        }
        bool expired = monotonic_ns () - extraction_started >= extraction_turn[i];
#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
        expired =
            acacia::synthesis::test_extraction_grace (expired, deadline_mono_ns, g_child_pids[i]);
#endif
        if (!suspension_pending && expired) {
          extraction_event (i, "grace_expired");
          extraction_turn[i] = std::min (extraction_turn[i], UINT64_MAX / 2) * 2;
          const auto next = next_extractor (i);
          if (next < 0)
            extraction_event (i, "no_alternative");
          else if (i == original_extractor) {
            check_acceptance ();
            select_extractor (next);
          }
          else {
            check_acceptance ();
            kill (-g_child_pids[i], SIGSTOP);
            suspension_pending = true;
          }
          if (synthesis_winner == i)
            start_extraction_turn (i);
        }
        if (suspension_pending) {
          const int observed =
              waitid (P_PID, g_child_pids[i], &finished, WSTOPPED | WEXITED | WNOWAIT | WNOHANG);
          if (observed < 0 && errno != EINTR)
            throw std::runtime_error ("unable to suspend controller extractor");
          // An exit racing SIGSTOP goes through normal acceptance before switching.
          if (finished.si_pid && finished.si_code == CLD_STOPPED) {
            finished = {};
            const auto next = next_extractor (i);
            if (next < 0)
              extraction_event (i, "no_alternative");
            synthesis_winner = -1;
            suspension_pending = false;
            select_extractor (next < 0 ? i : next);
          }
        }
      }
      bool live_arm = g_synthesis && discarding ();
      for (sig_atomic_t i = 0; i < g_child_count; ++i)
        if (g_child_pids[i] > 0) {
          live_arm = true;
          break;
        }
      if (!live_arm)
        break;
#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
      if (g_synthesis && synthesis_winner >= 0)
        acacia::synthesis::test_before_reap (g_child_pids[synthesis_winner]);
#endif
      const int observed =
          finished.si_pid > 0
              ? 0
              : waitid (!extraction_fallback && synthesis_winner >= 0 ? P_PID : P_ALL,
                        !extraction_fallback && synthesis_winner >= 0
                            ? g_child_pids[synthesis_winner]
                            : 0,
                        &finished, WEXITED | WNOHANG | WNOWAIT);
      rusage reaped_usage {};
      uint64_t reaped_mono_ns = 0;
      const pid_t finished_group =
          g_worker_records && finished.si_pid > 0 ? getpgid (finished.si_pid) : -1;
      pid_t reaped = observed == -1 ? -1 : 0;
      int wait_error = errno;
      if (observed == 0 && finished.si_pid == acacia::phase_records_writer_pid ()) {
        waitpid (finished.si_pid, nullptr, WNOHANG);
        acacia::phase_records_writer_reaped ();
        continue;
      }
      if (observed == 0 && finished.si_pid > 0) {
        // WNOWAIT keeps the group leader's PID reserved until descendants in
        // its group have been killed, even if the leader exited earlier.
        kill (-finished.si_pid, SIGKILL);
        if (g_worker_records)
          for (sig_atomic_t i = 0; i < g_child_count; ++i)
            if (g_child_pids[i] == finished.si_pid) {
              auto record = g_worker_records[i];
              record.pid = finished.si_pid;
              acacia::resource_event (record, "exit_observed", true);
              break;
            }
        reaped = wait4 (finished.si_pid, &status, WNOHANG, &reaped_usage);
        wait_error = errno;
        if (reaped > 0 && g_worker_records)
          reaped_mono_ns = acacia::phase_clock (CLOCK_MONOTONIC);
      }
      if (reaped > 0 && g_worker_records) {
        bool descendant = false;
        for (sig_atomic_t i = 0; i < g_child_count; ++i)
          if (g_worker_records[i].descendant_pid == reaped ||
              (g_worker_records[i].pid == finished_group && reaped != finished_group)) {
            acacia::descendant_record (g_worker_records[i], reaped, &reaped_usage, status);
            g_worker_records[i].descendant_pid = -1;
            descendant = true;
            break;
          }
        if (descendant)
          continue;
      }
      sig_atomic_t reaped_index = -1;
      if (reaped > 0) {
        for (sig_atomic_t i = 0; i < g_child_count; ++i)
          if (g_child_pids[i] == reaped) {
            g_child_pids[i] = 0;
            reaped_index = i;
            break;
          }
      }
      if (reaped > 0 && reaped_index < 0)
        continue;  // Adopted descendants cannot supply a worker verdict.
#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
      if (reaped > 0 && std::getenv ("ACACIA_TEST_DELAY_AFTER_REAP")) {
        char line[64];
        const int length = snprintf (line, sizeof line,
                                     "{\"stage\":\"test_reaped\",\"before_deadline\":%s}\n",
                                     monotonic_ns () < deadline_mono_ns ? "true" : "false");
        (void) ::write (STDERR_FILENO, line, size_t (length));
        timespec delay {0, 500000000};
        nanosleep (&delay, nullptr);
      }
#endif
      const uint64_t now_ns = deadline_mono_ns ? monotonic_ns () : 0;
      if (deadline_mono_ns && now_ns >= deadline_mono_ns) {
        std::cerr << "{\"stage\":\"deadline\",\"status\":\"expired\"}\n";
        if (reaped_index >= 0)
          record_parent_terminal (reaped_index, status, "deadline_rejected", reaped, &reaped_usage,
                                  reaped_mono_ns);
        stop_children ("deadline");
        error (EXIT_CODE_UNKNOWN, "UNKNOWN\n");
      }
      if (g_interrupted) {
        if (reaped_index >= 0)
          record_parent_terminal (reaped_index, status, "interrupted_rejected", reaped,
                                  &reaped_usage, reaped_mono_ns);
        stop_children ("interrupted");
        error (EXIT_CODE_UNKNOWN, "UNKNOWN\n");
      }
      if (reaped_index >= 0)
        record_parent_terminal (reaped_index, status,
                                g_synthesis && pending_discard[reaped_index]
                                    ? pending_discard[reaped_index]
                                : WIFSIGNALED (status) ? "signal"
                                                       : "exit",
                                reaped, &reaped_usage, reaped_mono_ns);
      if (reaped_index >= 0 && g_synthesis && pending_discard[reaped_index])
        continue;
      if (reaped == 0) {
        const uint64_t pause_ns =
            deadline_mono_ns ? std::min<uint64_t> (10000000, deadline_mono_ns - now_ns) : 10000000;
        timespec pause {0, static_cast<long> (pause_ns)};
        nanosleep (&pause, nullptr);
        continue;
      }
      if (reaped == -1) {
        if (wait_error == EINTR)
          continue;
        break;
      }

      // A child killed by a signal (SIGSEGV, SIGABRT, ...) has WIFEXITED
      // false; WEXITSTATUS would then return 0, which equals EXIT_CODE_REAL
      // and would be silently misreported as "REALIZABLE". Skip such children
      // so we either hear from a sibling that finished cleanly, or fall
      // through to UNKNOWN below.
      if (not WIFEXITED (status)) {
        if (!g_controller_paths.empty ())
          unlink (g_controller_paths[reaped_index].c_str ());
        if (g_synthesis && reaped_index == synthesis_winner)
          resume_after_failure ();
        if (WIFSIGNALED (status)) {
          // One write per record: a child's own stderr line must not land inside this one.
          char line[64];
          const int length =
              snprintf (line, sizeof line, "{\"stage\":\"child_signal\",\"status\":%d}\n",
                        WTERMSIG (status));
          (void) ::write (STDERR_FILENO, line, size_t (length));
        }
        continue;
      }
      int ret = WEXITSTATUS (status);
      if (std::ranges::find (g_rss_exhausted, reaped_index) != g_rss_exhausted.end ())
        ret = EXIT_CODE_UNKNOWN;
      if (g_obligation && reaped_index == g_helper_index &&
          (g_helper_forced != acacia::obligation::outcome::pending ||
           acacia::obligation::exit_code (g_obligation->result.load (std::memory_order_acquire)) !=
               ret))
        ret = EXIT_CODE_UNKNOWN;
      if (g_synthesis && (ret == EXIT_CODE_REAL || ret == EXIT_CODE_UNREAL) &&
          (reaped_index < 0 ||
           !g_synthesis[reaped_index].proceed.load (std::memory_order_acquire) ||
           ret != synthesis_verdict ||
           g_synthesis[reaped_index].verdict.load (std::memory_order_acquire) != ret ||
           (ret == EXIT_CODE_REAL &&
            !g_synthesis[reaped_index].controller_complete.load (std::memory_order_acquire))))
        ret = EXIT_CODE_UNKNOWN;
      if (g_synthesis && reaped_index == synthesis_winner && ret != EXIT_CODE_REAL &&
          ret != EXIT_CODE_UNREAL)
        resume_after_failure ();
      if (!g_controller_paths.empty () && ret != EXIT_CODE_REAL && ret != EXIT_CODE_UNREAL)
        unlink (g_controller_paths[reaped_index].c_str ());
      if (ret == EXIT_CODE_ERROR) {
        child_reported_error = true;
        continue;
      }
      if (ret == EXIT_CODE_REAL or ret == EXIT_CODE_UNREAL) {
        check_acceptance ();
        // A closed stdout can terminate the parent with SIGPIPE. Reap suspended
        // synthesis competitors before publishing the definitive answer.
#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
        if (g_synthesis)
          acacia::synthesis::test_before_publication ();
#endif
        if (g_synthesis) {
          stop_children ("winner_cancelled");
          check_acceptance ();
          if (ret == EXIT_CODE_REAL && !g_controller_paths.empty ()) {
            sigset_t old_mask;
            sigprocmask (SIG_BLOCK, &block_set, &old_mask);
            const pid_t publisher = fork ();
            if (publisher == 0) {
#ifdef __linux__
              if (prctl (PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid () != g_main_pid)
                _exit (EXIT_CODE_UNKNOWN);
#endif
              sigprocmask (SIG_SETMASK, &old_mask, nullptr);
#if ACACIA_HAVE_ABC
              // Reuse publication supervision, after competitors have been reaped.
              acacia::synthesis::aiger_abc = {arg_values.aiger_abc, deadline_mono_ns, UINT64_MAX};
#endif
              try {
                std::ifstream controller (g_controller_paths[reaped_index]);
                if (controller) {
                  const std::string aiger {std::istreambuf_iterator<char> (controller), {}};
                  // Preserve master's writer semantics without blocking the supervisor.
                  if (!controller.bad () &&
                      acacia::synthesis::write_controller (*arg_values.synth_fname, aiger))
                    _exit (0);
                }
              } catch (...) {
              }
              _exit (EXIT_CODE_UNKNOWN);
            }
            g_publisher_pid = publisher > 0 ? publisher : 0;
            sigprocmask (SIG_SETMASK, &old_mask, nullptr);
            bool published = false;
            while (g_publisher_pid > 0) {
              int publication_status = 0;
              const pid_t reaped = waitpid (g_publisher_pid, &publication_status, WNOHANG);
              const int publication_error = errno;
              if (reaped > 0 || (reaped < 0 && publication_error == ECHILD))
                g_publisher_pid = 0;
              check_acceptance ();
              if (reaped > 0) {
                published =
                    WIFEXITED (publication_status) && WEXITSTATUS (publication_status) == 0;
                break;
              }
              if (reaped < 0 && publication_error != EINTR) {
                stop_children ("controller_export_failed");
                break;
              }
              timespec pause {0, 10000000};
              nanosleep (&pause, nullptr);
            }
            check_acceptance ();
            if (!published) {
              extraction_event (reaped_index, "controller_export_failed");
              cleanup_controllers ();
              error (EXIT_CODE_UNKNOWN, "UNKNOWN\n");
            }
          }
          if (g_worker_records && ret == EXIT_CODE_REAL) {
            auto extracted = g_worker_records[reaped_index];
            acacia::worker_event (extracted, "parent_extraction", "published", ret);
          }
          cleanup_controllers ();
        }
        if (g_worker_records && reaped_index >= 0) {
          auto& winner = g_worker_records[reaped_index];
          acacia::worker_record_text (winner.original_polarity,
                                      ret == EXIT_CODE_REAL ? "REAL" : "UNREAL");
          acacia::worker_event (winner, "parent_winner", "accepted", ret, 0,
                                winner.dropped    ? "dropped"
                                : winner.terminal ? "complete"
                                                  : "incomplete");
        }
        if (ret == EXIT_CODE_REAL)
          std::cout << "REALIZABLE\n";
        else
          std::cout << "UNREALIZABLE\n";
        std::cout << std::flush;
        if (!g_synthesis)
          stop_children ("winner_cancelled");
        return ret;
      }
    }
    reap_descendants ();
    if (child_reported_error)
      error (EXIT_CODE_ERROR, "ERROR\n");
    error (EXIT_CODE_UNKNOWN, "UNKNOWN\n");
    return EXIT_CODE_UNKNOWN;
  }
}

int main (int argc, char** argv) {
  acacia::phase_records_start_writer ();
  // parse all arguments that were passed
  acacia::phase_scope argument_phase ("legacy_parent", "argument_handling_total");
  auto arg_values = arg_parser (argc, argv);
  argument_phase.finish ();
  const uint64_t deadline_mono_ns = outer_deadline_ns ();
  acacia::equivariance_budget::invocation = {arg_values.equivariance_budget, deadline_mono_ns,
                                             arg_values.equivariance_budget_ns};
  acacia::equivariance_budget::invocation_allowance.reset ();
  // set the global verbose level
  utils::verbose = arg_values.verbose_level;

  assert (arg_values.arms.has_value ());
  // Keep optional membership out of ordinary arms' construction/thread/RSS budgets.
  const bool helper_requested = std::erase_if (*arg_values.arms, [] (const auto& arm) {
    return arm.kind == portfolio_arm_kind::obligation;
  }) != 0;
  const size_t worker_slots = arg_values.arms->size () + size_t (helper_requested);
  g_child_pids = new pid_t[worker_slots];
  g_child_interrupted = new sig_atomic_t[worker_slots] {};
  if (helper_requested) {
    void* shared = mmap (nullptr, sizeof (acacia::obligation::control), PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared != MAP_FAILED) {
      g_obligation = new (shared) acacia::obligation::control;
      sig_atomic_t index = 0;
      for (const auto& arm : *arg_values.arms) {
        if (arm.kind == portfolio_arm_kind::legacy) {
          if (!arg_values.legacy_available)
            continue;
          g_eligibility_source = index;
          break;
        }
        ++index;
      }
      if (g_eligibility_source < 0)
        g_eligibility_source = 0;
    }
    else
      acacia::obligation::record ("eligibility", acacia::obligation::outcome::error,
                                  "shared_allocation");
  }
  if (arg_values.synth_fname) {
    const size_t slots_bytes = worker_slots * sizeof (acacia::synthesis::rendezvous_slot);
    const size_t bytes = slots_bytes + sizeof (std::atomic<uint64_t>);
    void* shared = mmap (nullptr, bytes, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared == MAP_FAILED)
      error (EXIT_CODE_ERROR, "Error: unable to synchronize controller synthesis.\n");
    g_synthesis = static_cast<acacia::synthesis::rendezvous_slot*> (shared);
    acacia::synthesis::verdict_sequence =
        new (static_cast<char*> (shared) + slots_bytes) std::atomic<uint64_t> {0};
    for (size_t i = 0; i < worker_slots; ++i)
      new (&g_synthesis[i]) acacia::synthesis::rendezvous_slot;
  }
  g_main_pid = getpid ();
#ifdef __linux__
  // Own orphaned worker descendants even when accounting is disabled.
  if (prctl (PR_SET_CHILD_SUBREAPER, 1) != 0)
    error (EXIT_CODE_ERROR, "Error: unable to become child subreaper: %s.\n", strerror (errno));
#endif
  if (acacia::phase_records_enabled ()) {
    const size_t bytes = worker_slots * sizeof (acacia::worker_record);
    void* shared =
        mmap (nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared == MAP_FAILED)
      acacia::phase_records_disable ();
    else
      g_worker_records = static_cast<acacia::worker_record*> (shared);
  }

  sigset_t block_set;
  sigemptyset (&block_set);
  sigaddset (&block_set, SIGTERM);
  sigaddset (&block_set, SIGINT);
  sigaddset (&block_set, SIGQUIT);
  sigaddset (&block_set, SIGABRT);

  // set up signal handlers to avoid crashing and reporting a wrong response
  // on Ctrl-C, for instance
  struct sigaction action;
  memset (&action, 0, sizeof (struct sigaction));
  action.sa_handler = terminate;
  action.sa_mask = block_set;
  sigaction (SIGTERM, &action, nullptr);
  sigaction (SIGINT, &action, nullptr);
  sigaction (SIGQUIT, &action, nullptr);
  sigaction (SIGABRT, &action, nullptr);

  try {
    return run_portfolio (arg_values, deadline_mono_ns, block_set);
  } catch (const std::exception& e) {
    if ((g_obligation || g_synthesis) && getpid () == g_main_pid)
      stop_children ("exception");
    acacia::worker_terminal (EXIT_CODE_ERROR, "exception");
    error (EXIT_CODE_ERROR, "Exception caught: %s\n", e.what ());
  } catch (...) {
    if ((g_obligation || g_synthesis) && getpid () == g_main_pid)
      stop_children ("exception");
    acacia::worker_terminal (EXIT_CODE_ERROR, "exception");
    error (EXIT_CODE_ERROR, "Unknown exception\n");
  }
}
