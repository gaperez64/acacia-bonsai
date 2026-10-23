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
#include <fstream>
#include <memory>
#include <new>
#include <optional>
#include <sstream>
#include <string>
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
  pid_t g_main_pid = 0;
  acacia::worker_record* g_worker_records = nullptr;
  acacia::obligation::control* g_obligation = nullptr;
  acacia::synthesis::rendezvous_slot* g_synthesis = nullptr;
  sig_atomic_t g_eligibility_source = -1, g_helper_index = -1;
  bool g_eligibility_observed = false;
  uint64_t g_helper_rss_share = 0, g_parity_rss_share = 0;
  std::vector<sig_atomic_t> g_parity_workers;
  std::vector<sig_atomic_t> g_parity_exhausted;
  acacia::obligation::outcome g_helper_forced = acacia::obligation::outcome::pending;
  const char* g_helper_stop_reason = "none";

  void reap_worker_descendant (acacia::worker_record& record);

  void record_parent_terminal (sig_atomic_t index, int status, const char* reason, pid_t pid,
                               const rusage* usage, uint64_t reaped_mono_ns) {
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
    if (std::ranges::find (g_parity_exhausted, index) != g_parity_exhausted.end ()) {
      reason = "rss_share";
      acacia::parity_arm_exhausted (record);
    }
    record.pid = pid;
    const char* telemetry = record.dropped    ? "dropped"
                            : record.terminal ? "complete"
                                              : "incomplete";
    reap_worker_descendant (record);
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

  void reap_worker_descendant (acacia::worker_record& record) {
    const pid_t pid = record.descendant_pid;
    if (pid <= 0)
      return;
    int status = 0;
    rusage usage {};
    pid_t got;
    do {
      got = wait4 (pid, &status, 0, &usage);
    } while (got < 0 && errno == EINTR);
    if (got != pid)
      record.descendant_usage_complete = false;
    acacia::descendant_record (record, pid, got == pid ? &usage : nullptr, status, got != pid);
    record.descendant_pid = -1;
  }

  void reap_descendants () {
    if (!g_worker_records)
      return;
    for (sig_atomic_t i = 0; i < g_child_count; ++i)
      reap_worker_descendant (g_worker_records[i]);
#ifdef __linux__
    // Also cover death between fork() and publishing descendant_pid. An adopted
    // child's process group still identifies its arm even after its PPID changes.
    char path[96];
    snprintf (path, sizeof path, "/proc/self/task/%ld/children", long (getpid ()));
    if (FILE* children = fopen (path, "r")) {
      long pid;
      while (fscanf (children, "%ld", &pid) == 1) {
        if (pid == acacia::phase_records_writer_pid ())
          continue;
        const pid_t group = getpgid (pid);
        for (sig_atomic_t i = 0; i < g_child_count; ++i)
          if (g_worker_records[i].pid == group) {
            kill (pid, SIGKILL);
            int status = 0;
            rusage usage {};
            pid_t got;
            do {
              got = wait4 (pid, &status, 0, &usage);
            } while (got < 0 && errno == EINTR);
            acacia::descendant_record (g_worker_records[i], pid, got == pid ? &usage : nullptr,
                                       status, got != pid);
            break;
          }
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
    return static_cast<uint64_t> (seconds * 1000000000.0L);
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
      for (unsigned i = 0; i < 20000; ++i)
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
          result = acacia::obligation::solve (objective, spec.inputs, spec.outputs,
                                              acacia::obligation::timing::mealy,
                                              args.synth_fname.has_value (), conversion);
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
    g_obligation->result.store (result.value, std::memory_order_release);
    acacia::worker_exit (code);
  }

  [[noreturn]] void dispatch_child (const portfolio_arm& arm, const arg_parse_result& arg_values,
                                    uint64_t deadline_mono_ns,
                                    acacia::obligation::control* eligibility_channel) {
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
      const int code = acacia::run_parity_arm (arg_values);
      acacia::worker_terminal (code);
      acacia::worker_exit (code);
    }
    if (arm.kind != portfolio_arm_kind::legacy) {
      try {
        // A native-only portfolio has no legacy normalization boundary. Use one
        // existing child to prepare eligibility, while its siblings already run.
        if (eligibility_channel) {
          try {
            acacia::obligation::publish_eligibility (eligibility_channel,
                                                    helper_objective (arg_values));
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
                << " candidate_mode=" << acacia::candidate_mode_name (arg_values.candidate) << "\n"
                << std::flush);
    const bool res = run_ltl (arg_values.objective.inputs, arg_values.objective.outputs,
                              arg_values.opt_k, arg_values.opt_kmin, arg_values.opt_kinc,
                              arg_values.objective.formula,
                              unreal_x, translation_pref, arg_values.spot_fast, backend,
                              (unreal_x.has_value () and *unreal_x != UNREAL_X_FORMULA)
                                  ? std::nullopt
                                  : arg_values.synth_fname,
                              arg_values.objective.metadata, provider, arg_values.candidate,
                              arg_values.equivariance, deadline_mono_ns, arg_values.tlsf_sha256,
                              arg_values.weakening, arg_values.weakening_allowances,
                              eligibility_channel);
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
      if (g_synthesis || arm.kind == portfolio_arm_kind::obligation ||
          arm.kind == portfolio_arm_kind::parity) {
        // SIGKILL also terminates suspended synthesis competitors. Recheck the
        // parent to cover its death between fork and installing the signal.
        if (prctl (PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid () != g_main_pid)
          acacia::worker_exit (EXIT_CODE_UNKNOWN);
      }
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
              "\"translation\":\"%s\",\"transform\":\"%s\",\"provider\":\"%s\","
              "\"r_prepass\":%s,\"equivariance\":%s}\n",
              spawned.index, long (pid), (unsigned long long) fork_start.wall,
              (unsigned long long) fork_start.wall, spawned.requested_backend,
              arm.legacy ? "legacy" : arm.native_name (), spawned.original_polarity,
              arm.legacy ? translation_pref_name (arm.legacy->translation_pref) : "native",
              arm.legacy ? (arm.unreal ? unreal_strategy_name (arm.legacy->unreal_x) : "real")
                         : "exact",
              arm.legacy ? acacia::automaton_provider_name (arm.legacy->provider) : "native",
              spawned.r_prepass ? "true" : "false", spawned.equivariance ? "true" : "false");
          if (length > 0 && size_t (length) < sizeof specification)
            acacia::phase_records_final (specification, size_t (length));
          else
            acacia::phase_records_drop ();
        }
        if (arm.kind == portfolio_arm_kind::parity)
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
          return arm.kind == portfolio_arm_kind::parity;
        }))
      g_parity_rss_share =
          acacia::obligation::rss_share (child_count + size_t (g_obligation != nullptr));
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
      launch_child (arm, arg_values, deadline_mono_ns, block_set,
                    g_obligation && g_child_count == g_eligibility_source ? g_obligation
                                                                         : nullptr);
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
    const auto resume_competitors = [&] {
      synthesis_winner = -1;
      for (sig_atomic_t i = 0; i < g_child_count; ++i)
        if (g_child_pids[i] > 0)
          kill (-g_child_pids[i], SIGCONT);
    };
    int status;
    bool child_reported_error = false;
    while (true) {  // wait only for solver arms; the record writer is separate
      if (g_obligation && !g_eligibility_observed && synthesis_winner < 0) {
        using acacia::obligation::eligibility;
        const auto eligible = g_obligation->eligible.load (std::memory_order_acquire);
        if (eligible != eligibility::pending) {
          g_eligibility_observed = true;
          if (eligible != eligibility::inapplicable || arg_values.obligation_explicit)
            acacia::obligation::record (
                "eligibility",
                eligible == eligibility::eligible ? acacia::obligation::outcome::pending
                               : eligible == eligibility::inapplicable
                                   ? acacia::obligation::outcome::inapplicable
                                   : acacia::obligation::outcome::error,
                eligible == eligibility::eligible ? "eligible"
                : eligible == eligibility::inapplicable ? "non_obligation"
                                                        : "preprocessing_failed");
          if (eligible == eligibility::eligible && g_helper_index < 0 && !g_interrupted &&
              (!deadline_mono_ns || monotonic_ns () < deadline_mono_ns))
            launch_helper (false);
        }
      }
      if (g_helper_index >= 0 && g_child_pids[g_helper_index] > 0 &&
          g_helper_forced == acacia::obligation::outcome::pending) {
        const pid_t helper = g_child_pids[g_helper_index];
        const bool memory = acacia::obligation::resident_bytes (helper) > g_helper_rss_share;
        if (memory) {
          g_helper_forced = acacia::obligation::outcome::exhausted;
          g_helper_stop_reason = "rss_share";
          if (g_worker_records) {
            auto r = g_worker_records[g_helper_index];
            r.pid = helper;
            acacia::resource_event (r, "rss_share", true);
          }
          kill (-helper, SIGKILL);
          kill (helper, SIGKILL);
        }
      }
      for (const auto index : g_parity_workers) {
        const pid_t worker = g_child_pids[index];
        if (worker > 0 &&
            std::ranges::find (g_parity_exhausted, index) == g_parity_exhausted.end () &&
            acacia::obligation::resident_bytes (worker) > g_parity_rss_share) {
          g_parity_exhausted.push_back (index);
          if (g_worker_records) {
            auto r = g_worker_records[index];
            r.pid = worker;
            acacia::resource_event (r, "rss_share", true);
          }
          kill (-worker, SIGKILL);
          kill (worker, SIGKILL);
        }
      }
      if (g_synthesis && synthesis_winner < 0) {
        check_acceptance ();
        sig_atomic_t nominee = -1;
        uint64_t first = UINT64_MAX;
        for (sig_atomic_t i = 0; i < g_child_count; ++i) {
          const auto order = g_synthesis[i].order.load (std::memory_order_acquire);
          if (g_child_pids[i] > 0 && order && order < first &&
              std::ranges::find (g_parity_exhausted, i) == g_parity_exhausted.end ()) {
            nominee = i;
            first = order;
          }
        }
        if (nominee >= 0) {
          const sig_atomic_t i = nominee;
          synthesis_winner = i;
          // Suspend the existing fallback, preserving its work if export fails.
          // Every exporter waits here, so no competitor can open the destination.
          for (sig_atomic_t other = 0; other < g_child_count; ++other)
            if (other != i && g_child_pids[other] > 0)
              kill (-g_child_pids[other], SIGSTOP);
          if (g_worker_records) {
            // The worker resumes this sequence after selection. Parent observations
            // must not consume entries in the child's live record stream.
            auto selected = g_worker_records[i];
            acacia::worker_event (selected, "parent_verdict", "selected",
                                  g_synthesis[i].verdict.load (std::memory_order_acquire));
          }
          check_acceptance ();
          g_synthesis[i].proceed.store (true, std::memory_order_release);
        }
      }
      bool live_arm = false;
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
      siginfo_t finished {};
      const int observed = waitid (synthesis_winner >= 0 ? P_PID : P_ALL,
                                   synthesis_winner >= 0 ? g_child_pids[synthesis_winner] : 0,
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
        record_parent_terminal (reaped_index, status, WIFSIGNALED (status) ? "signal" : "exit",
                                reaped, &reaped_usage, reaped_mono_ns);
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
        if (g_synthesis && reaped_index == synthesis_winner)
          resume_competitors ();
        if (WIFSIGNALED (status)) {
          // One write per record: a child's own stderr line must not land inside this one.
          char line[64];
          const int length = snprintf (line, sizeof line,
                                       "{\"stage\":\"child_signal\",\"status\":%d}\n",
                                       WTERMSIG (status));
          (void) ::write (STDERR_FILENO, line, size_t (length));
        }
        continue;
      }
      int ret = WEXITSTATUS (status);
      if (std::ranges::find (g_parity_exhausted, reaped_index) != g_parity_exhausted.end ())
        ret = EXIT_CODE_UNKNOWN;
      if (g_obligation && reaped_index == g_helper_index &&
          (g_helper_forced != acacia::obligation::outcome::pending ||
           acacia::obligation::exit_code (
               g_obligation->result.load (std::memory_order_acquire)) != ret))
        ret = EXIT_CODE_UNKNOWN;
      if (g_synthesis && (ret == EXIT_CODE_REAL || ret == EXIT_CODE_UNREAL) &&
          (reaped_index < 0 || reaped_index != synthesis_winner ||
           g_synthesis[reaped_index].verdict.load (std::memory_order_acquire) != ret ||
           (ret == EXIT_CODE_REAL &&
            !g_synthesis[reaped_index].controller_complete.load (std::memory_order_acquire))))
        ret = EXIT_CODE_UNKNOWN;
      if (g_synthesis && reaped_index == synthesis_winner &&
          ret != EXIT_CODE_REAL && ret != EXIT_CODE_UNREAL)
        resume_competitors ();
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
  if (acacia::phase_records_enabled ()) {
    const size_t bytes = worker_slots * sizeof (acacia::worker_record);
    void* shared =
        mmap (nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared == MAP_FAILED)
      acacia::phase_records_disable ();
    else {
#ifdef __linux__
      // Adopt the safety-core subprocess only after its accounting area exists.
      if (prctl (PR_SET_CHILD_SUBREAPER, 1) != 0) {
        munmap (shared, bytes);
        acacia::phase_records_disable ();
      }
      else
#endif
      {
        g_worker_records = static_cast<acacia::worker_record*> (shared);
      }
    }
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
