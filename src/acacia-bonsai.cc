#include "arg_parser.hh"
#include "configuration.hh"
#include "error_msg.hh"
#include "native_gr1_arm.hh"
#include "native_gr1_lift_arm.hh"
#include "native_param_lift_arm.hh"
#include "native_support.hh"
#include "parity_arm.hh"
#include "phase_records.hh"
#include "solver/solver_invoker.hh"
#include "solver/controller_writer.hh"
#include "solver/obligation_helper.hh"
#include "solver/realizability_simplify.hh"
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
  volatile sig_atomic_t g_child_count = 0;
  volatile sig_atomic_t g_interrupted = 0;
  pid_t g_main_pid = 0;
  acacia::worker_record* g_worker_records = nullptr;
  bool g_worker_records_shared = false;
  acacia::obligation::control* g_obligation = nullptr;
  acacia::synthesis::rendezvous_slot* g_synthesis = nullptr;
  sig_atomic_t g_eligibility_source = -1, g_helper_index = -1;
  bool g_eligibility_observed = false;
  uint64_t g_helper_rss_share = 0, g_parity_rss_share = 0;
  std::vector<sig_atomic_t> g_parity_workers;
  std::vector<sig_atomic_t> g_parity_exhausted;
  acacia::obligation::outcome g_helper_forced = acacia::obligation::outcome::pending;
  const char* g_helper_stop_reason = "none";

  void record_parent_terminal (sig_atomic_t index, int status, const char* reason, pid_t pid) {
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
    const char* telemetry = !g_worker_records_shared ? "unavailable"
                            : record.dropped         ? "dropped"
                            : record.terminal        ? "complete"
                                                     : "incomplete";
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
          kill (-g_child_pids[i], SIGKILL);
          kill (g_child_pids[i], SIGKILL);
        }
    }
    else
      _exit (EXIT_CODE_UNKNOWN);  // child procs avoid cleaning on exit
    errno = saved_errno;
  }

  void kill_child_groups () {
    for (sig_atomic_t i = 0; i < g_child_count; ++i)
      if (g_child_pids[i] > 0) {
        kill (-g_child_pids[i], SIGKILL);
        // Also cover the fork-to-setpgid window if group creation failed.
        kill (g_child_pids[i], SIGKILL);
      }
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
        const pid_t reaped = waitpid (pid, &status, WNOHANG);
        if (reaped == pid || (reaped == -1 && errno == ECHILD)) {
          record_parent_terminal (i, reaped == pid ? status : 0xffff, reason, pid);
          g_child_pids[i] = 0;
        }
        else
          remaining = true;
      }
      if (!remaining)
        return;
      timespec pause {0, 1000000};
      nanosleep (&pause, nullptr);
    }
  }

  void stop_children (const char* reason) {
    kill_child_groups ();
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
      _exit (EXIT_CODE_ERROR);
    std::string_view mode {value};
    for (sig_atomic_t i = 0; i < index; ++i) {
      const auto comma = mode.find (',');
      if (comma == std::string_view::npos)
        return;
      mode.remove_prefix (comma + 1);
    }
    mode = mode.substr (0, mode.find (','));
    if (mode == "stall") {
      signal (SIGTERM, SIG_IGN);
      while (true)
        pause ();
    }
    if (mode == "real-delayed") {
      timespec delay {0, 100000000};
      nanosleep (&delay, nullptr);
      _exit (EXIT_CODE_REAL);
    }
    if (mode == "real-late" && deadline_mono_ns) {
      while (monotonic_ns () < deadline_mono_ns + 20000000ULL) {
        timespec delay {0, 1000000};
        nanosleep (&delay, nullptr);
      }
      _exit (EXIT_CODE_REAL);
    }
    if (mode == "real")
      _exit (EXIT_CODE_REAL);
  }
#endif

  spot::formula helper_objective (const arg_parse_result& args) {
    std::string formula = args.formula;
    auto inputs = args.inputs, outputs = args.outputs;
#if ACACIA_ENABLE_TLSF_FRONTEND
    if (args.tlsf_specified && !args.formula_specified) {
      auto spec = acacia::tlsf_frontend::parse (args.tlsf_source);
      formula = std::move (spec.formula);
      inputs = std::move (spec.inputs);
      outputs = std::move (spec.outputs);
    }
#endif
    validate_ltl_partition (inputs, outputs);
    auto parsed = spot::parse_infix_psl (formula, spot::default_environment::instance (),
                                          false, false);
    if (!parsed.f || !parsed.errors.empty ())
      throw std::runtime_error ("unable to parse original obligation objective");
    auto objective = parsed.f;
    if (ACACIA_ENABLE_REALIZABILITY_SIMPLIFIER && !args.synth_fname)
      acacia::realizability::apply_simplifier (objective, inputs);
    return objective;
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
        auto inputs = args.inputs, outputs = args.outputs;
        auto conversion = acacia::obligation::conversion::none;
#if ACACIA_ENABLE_TLSF_FRONTEND
        if (args.tlsf_specified && (!args.formula_specified || args.synth_fname)) {
          auto spec = acacia::tlsf_frontend::parse (args.tlsf_source);
          inputs = std::move (spec.inputs);
          outputs = std::move (spec.outputs);
          if (args.synth_fname) {
            if (spec.metadata.tlsf_target == "Moore")
              conversion = spec.metadata.tlsf_semantics.find ("Moore") != std::string::npos
                               ? acacia::obligation::conversion::undo_input_shift
                               : acacia::obligation::conversion::delay_outputs;
          }
        }
#endif
        // The linked frontend already lowered Moore/strict TLSF to effective
        // Mealy. Solve that positive normalized objective once, without a swap
        // or another X shift. Raw API Moore order is exercised in unit tests.
        const auto objective = helper_objective (args);
        if (g_obligation->eligible.load (std::memory_order_acquire) ==
            acacia::obligation::eligibility::pending)
          acacia::obligation::publish_eligibility (g_obligation, objective);
        if (!acacia::obligation::eligible (objective))
          result = {outcome::inapplicable, "non_obligation"};
        else {
          acacia::worker_stage ("obligation_synthesis");
          result = acacia::obligation::solve (objective, inputs, outputs,
                                              acacia::obligation::timing::mealy,
                                              args.synth_fname.has_value (), conversion);
          if (result.value == outcome::real && args.synth_fname) {
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
    _exit (code);
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
      _exit (code);
    }
    if (arm.kind != portfolio_arm_kind::legacy) {
#if ACACIA_NATIVE_ARMS
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
        _exit (result);
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
      _exit (EXIT_CODE_UNKNOWN);
#else
      _exit (EXIT_CODE_ERROR);
#endif
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
    const bool res = run_ltl (arg_values.inputs, arg_values.outputs, arg_values.opt_k,
                              arg_values.opt_kmin, arg_values.opt_kinc, arg_values.formula,
                              unreal_x, translation_pref, arg_values.spot_fast, backend,
                              (unreal_x.has_value () and *unreal_x != UNREAL_X_FORMULA)
                                  ? std::nullopt
                                  : arg_values.synth_fname,
                              arg_values.metadata, provider, arg_values.candidate,
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
          _exit (EXIT_CODE_UNKNOWN);
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
        acacia::worker_event (*acacia::active_worker_record (), "worker_start");
        acacia::worker_route ("dispatch");
      }
      const acacia::phase_stamp child_start =
          fork_start.wall ? acacia::phase_stamp {fork_start.wall,
                                                 acacia::phase_clock (CLOCK_PROCESS_CPUTIME_ID)}
                          : acacia::phase_stamp {};
      if (setpgid (0, 0) != 0)
        _exit (EXIT_CODE_ERROR);
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
          char specification[512];
          const int length = snprintf (
              specification, sizeof specification,
              "{\"event\":\"worker_spec\",\"worker\":%u,\"worker_pid\":%ld,"
              "\"kind\":\"%s\",\"requested_polarity\":\"%s\","
              "\"translation\":\"%s\",\"transform\":\"%s\",\"provider\":\"%s\","
              "\"r_prepass\":%s,\"equivariance\":%s}\n",
              spawned.index, long (pid), arm.legacy ? "legacy" : arm.native_name (),
              spawned.original_polarity,
              arm.legacy ? translation_pref_name (arm.legacy->translation_pref) : "native",
              arm.legacy ? (arm.unreal ? unreal_strategy_name (arm.legacy->unreal_x) : "real")
                         : "exact",
              arm.legacy ? acacia::automaton_provider_name (arm.legacy->provider) : "native",
              spawned.r_prepass ? "true" : "false", spawned.equivariance ? "true" : "false");
          if (length > 0 && size_t (length) < sizeof specification)
            acacia::phase_records_send (specification, size_t (length));
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
      g_helper_rss_share = acacia::obligation::rss_share (size_t (g_child_count) + 1);
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
      validate_ltl_partition (arg_values.inputs, arg_values.outputs);
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
      siginfo_t finished {};
      const int observed = waitid (synthesis_winner >= 0 ? P_PID : P_ALL,
                                   synthesis_winner >= 0 ? g_child_pids[synthesis_winner] : 0,
                                   &finished, WEXITED | WNOHANG | WNOWAIT);
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
        reaped = waitpid (finished.si_pid, &status, WNOHANG);
        wait_error = errno;
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
        std::cerr << "{\"stage\":\"test_reaped\",\"before_deadline\":"
                  << (monotonic_ns () < deadline_mono_ns ? "true" : "false") << "}\n";
        timespec delay {0, 500000000};
        nanosleep (&delay, nullptr);
      }
#endif
      const uint64_t now_ns = deadline_mono_ns ? monotonic_ns () : 0;
      if (deadline_mono_ns && now_ns >= deadline_mono_ns) {
        std::cerr << "{\"stage\":\"deadline\",\"status\":\"expired\"}\n";
        if (reaped_index >= 0)
          record_parent_terminal (reaped_index, status, "deadline_rejected", reaped);
        stop_children ("deadline");
        error (EXIT_CODE_UNKNOWN, "UNKNOWN\n");
      }
      if (g_interrupted) {
        if (reaped_index >= 0)
          record_parent_terminal (reaped_index, status, "interrupted_rejected", reaped);
        stop_children ("interrupted");
        error (EXIT_CODE_UNKNOWN, "UNKNOWN\n");
      }
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

      if (reaped_index >= 0)
        record_parent_terminal (reaped_index, status, WIFSIGNALED (status) ? "signal" : "exit",
                                reaped);

      // A child killed by a signal (SIGSEGV, SIGABRT, ...) has WIFEXITED
      // false; WEXITSTATUS would then return 0, which equals EXIT_CODE_REAL
      // and would be silently misreported as "REALIZABLE". Skip such children
      // so we either hear from a sibling that finished cleanly, or fall
      // through to UNKNOWN below.
      if (not WIFEXITED (status)) {
        if (g_synthesis && reaped_index == synthesis_winner)
          resume_competitors ();
        if (WIFSIGNALED (status))
          std::cerr << "{\"stage\":\"child_signal\",\"status\":" << WTERMSIG (status) << "}\n";
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
    g_worker_records_shared = shared != MAP_FAILED;
    g_worker_records = g_worker_records_shared
                           ? static_cast<acacia::worker_record*> (shared)
                           : new (std::nothrow) acacia::worker_record[worker_slots];
    if (!g_worker_records) {
      constexpr char unavailable[] =
          "{\"event\":\"attribution_unavailable\",\"reason\":\"allocation\"}\n";
      acacia::phase_records_send (unavailable, sizeof unavailable - 1);
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
