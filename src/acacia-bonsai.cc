#include "arg_parser.hh"
#include "configuration.hh"
#include "error_msg.hh"
#include "native_gr1_arm.hh"
#include "solver/solver_invoker.hh"
#include <unordered_map>

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstring>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
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

  void terminate ([[maybe_unused]] int signum) {
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
  }

  void kill_child_groups () {
    for (sig_atomic_t i = 0; i < g_child_count; ++i)
      if (g_child_pids[i] > 0) {
        kill (-g_child_pids[i], SIGKILL);
        // Also cover the fork-to-setpgid window if group creation failed.
        kill (g_child_pids[i], SIGKILL);
      }
  }

  void reap_remaining_children () {
    // SIGKILL cannot be ignored, but a child may not be scheduled immediately.
    // Poll each known PID so cleanup never waits in a blocking waitpid call.
    while (true) {
      bool remaining = false;
      for (sig_atomic_t i = 0; i < g_child_count; ++i) {
        const pid_t pid = g_child_pids[i];
        if (pid <= 0) continue;
        const pid_t reaped = waitpid (pid, nullptr, WNOHANG);
        if (reaped == pid || (reaped == -1 && errno == ECHILD))
          g_child_pids[i] = 0;
        else
          remaining = true;
      }
      if (!remaining) return;
      timespec pause{0, 1000000};
      nanosleep (&pause, nullptr);
    }
  }

  void stop_children () {
    kill_child_groups ();
    reap_remaining_children ();
  }

  const char* unreal_strategy_name (UNREAL_X_T strategy) {
    if (strategy == UNREAL_X_FORMULA)
      return "formula";
    if (strategy == UNREAL_X_AUTOMATON)
      return "automaton";
    return "unknown";
  }

  uint64_t monotonic_ns () {
    timespec now{};
    if (clock_gettime (CLOCK_MONOTONIC, &now) != 0) return 0;
    return uint64_t (now.tv_sec) * 1000000000ULL + now.tv_nsec;
  }

  uint64_t outer_deadline_ns () {
    const char* value = std::getenv ("ACACIA_OUTER_DEADLINE_MONOTONIC");
    if (!value || !*value) return 0;
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
    if (!value) return;
    std::string_view mode{value};
    for (sig_atomic_t i = 0; i < index; ++i) {
      const auto comma = mode.find (',');
      if (comma == std::string_view::npos) return;
      mode.remove_prefix (comma + 1);
    }
    mode = mode.substr (0, mode.find (','));
    if (mode == "stall") {
      signal (SIGTERM, SIG_IGN);
      while (true) pause ();
    }
    if (mode == "real-delayed") {
      timespec delay{0, 100000000};
      nanosleep (&delay, nullptr);
      _exit (EXIT_CODE_REAL);
    }
    if (mode == "real-late" && deadline_mono_ns) {
      while (monotonic_ns () < deadline_mono_ns + 20000000ULL) {
        timespec delay{0, 1000000};
        nanosleep (&delay, nullptr);
      }
      _exit (EXIT_CODE_REAL);
    }
    if (mode == "real") _exit (EXIT_CODE_REAL);
  }
#endif

}

int main (int argc, char** argv) {
  const uint64_t deadline_mono_ns = outer_deadline_ns ();
  // parse all arguments that were passed
  auto arg_values = arg_parser (argc, argv);
  // set the global verbose level
  utils::verbose = arg_values.verbose_level;

  assert (arg_values.arms.has_value ());
  g_child_pids = new pid_t[arg_values.arms->size ()];
  g_main_pid = getpid ();

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
    const auto start_proc = [&] (const portfolio_arm& arm) {
      // Publish the child pid before a termination handler can run.
      sigset_t old_mask;
      sigprocmask (SIG_BLOCK, &block_set, &old_mask);
      const pid_t pid = fork ();
      if (pid == 0) {
        if (setpgid (0, 0) != 0) _exit (EXIT_CODE_ERROR);
        sigprocmask (SIG_SETMASK, &old_mask, nullptr);
#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
        test_child_behavior (g_child_count, deadline_mono_ns);
#endif
        if (arm.kind == portfolio_arm_kind::gr1) {
#if ACACIA_NATIVE_ARMS
          try {
            _exit (acacia::run_native_gr1_arm (arg_values, arm.unreal, deadline_mono_ns));
          }
          catch (const std::exception& exception) {
            acacia::native_diagnostic (arm.unreal, "native_exception", -1, exception.what ());
          }
          catch (...) {
            acacia::native_diagnostic (arm.unreal, "native_exception", -1, "unknown exception");
          }
          _exit (EXIT_CODE_UNKNOWN);
#else
          _exit (EXIT_CODE_ERROR);
#endif
        }
        auto backend = arm.legacy->backend;
        auto provider = arm.legacy->provider;
        if (arg_values.synth_fname.has_value () and not arm.unreal and
            backend != acacia::game_backend::backward) {
          verb_do (1, vout << "Forcing the real backend to backward for synthesis\n" << std::flush);
          backend = acacia::synthesis_backend (backend, true);
          provider = acacia::synthesis_provider (provider, true);
        }
        const auto unreal_x = arm.unreal ? std::make_optional<UNREAL_X_T> (arm.legacy->unreal_x)
                                         : std::nullopt;
        const auto translation_pref = arm.legacy->translation_pref;
        // we check one thing at a time here
        assert (not unreal_x.has_value () or *unreal_x != UNREAL_X_BOTH);
        utils::vout.set_prefix (
            std::string {"["} +
            (not unreal_x.has_value ()
                 ? std::string {"real="} + translation_pref_name (translation_pref)
                 : std::string {"unreal="} + unreal_strategy_name (*unreal_x) +
                       ",pref=" + translation_pref_name (translation_pref)) +
            ",backend=" + acacia::game_backend_name (backend) +
            "] ");
        verb_do (1, vout << "Starting solver child provider="
                          << acacia::automaton_provider_name (provider)
                          << " candidate_mode=" << acacia::candidate_mode_name (arg_values.candidate)
                          << "\n" << std::flush);
        const bool res = run_ltl (arg_values.inputs, arg_values.outputs, arg_values.opt_k,
                                  arg_values.opt_kmin, arg_values.opt_kinc, arg_values.formula,
                                  unreal_x, translation_pref, arg_values.spot_fast,
                                  backend,
                                  (unreal_x.has_value () and *unreal_x != UNREAL_X_FORMULA)
                                      ? std::nullopt
                                      : arg_values.synth_fname,
                                  arg_values.metadata, provider, arg_values.candidate);
        verb_do (1, vout << "returning " << res << "\n");

        if (unreal_x.has_value ())
          exit (res ? EXIT_CODE_UNREAL : EXIT_CODE_UNKNOWN);
        else
          exit (res ? EXIT_CODE_REAL : EXIT_CODE_UNKNOWN);
      }
      else {
        if (pid > 0) {
          // The child does this too. Setting it here closes the fork-to-setpgid
          // window before the parent can signal the group.
          setpgid (pid, pid);
          g_child_pids[g_child_count] = pid;
          g_child_count = g_child_count + 1;
        }
        // Restore the parent's mask even if fork failed.
        sigprocmask (SIG_SETMASK, &old_mask, nullptr);
      }
    };

    // We fork process for each (UN)REAL check now and then wait for them to
    // return to process their exit codes
    setpgid (0, 0);
    assert (getpgid (0) == getpid ());

    [[maybe_unused]] const size_t child_count = std::ranges::count_if (
        *arg_values.arms, [&] (const auto& arm) {
          return arm.kind != portfolio_arm_kind::legacy || arg_values.legacy_available;
        });
    verb_do (1, vout << "Starting " << child_count << " solver children\n" << std::flush);

    for (const auto& arm : *arg_values.arms) {
      if (arm.kind == portfolio_arm_kind::legacy && !arg_values.legacy_available)
        continue;
      if (deadline_mono_ns && monotonic_ns () >= deadline_mono_ns) {
        std::cerr << "{\"stage\":\"deadline\",\"status\":\"expired\"}\n";
        stop_children ();
        error (EXIT_CODE_UNKNOWN, "UNKNOWN\n");
      }
      if (g_interrupted) {
        stop_children ();
        error (EXIT_CODE_UNKNOWN, "UNKNOWN\n");
      }
      start_proc (arm);
    }

    int status;
    bool child_reported_error = false;
    while (true) {  // as long as we have children to wait for
      siginfo_t finished{};
      const int observed = waitid (P_ALL, 0, &finished, WEXITED | WNOHANG | WNOWAIT);
      pid_t reaped = observed == -1 ? -1 : 0;
      int wait_error = errno;
      if (observed == 0 && finished.si_pid > 0) {
        // WNOWAIT keeps the group leader's PID reserved until descendants in
        // its group have been killed, even if the leader exited earlier.
        kill (-finished.si_pid, SIGKILL);
        reaped = waitpid (finished.si_pid, &status, WNOHANG);
        wait_error = errno;
      }
      if (reaped > 0) {
        for (sig_atomic_t i = 0; i < g_child_count; ++i)
          if (g_child_pids[i] == reaped) {
            g_child_pids[i] = 0;
            break;
          }
      }
#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
      if (reaped > 0 && std::getenv ("ACACIA_TEST_DELAY_AFTER_REAP")) {
        std::cerr << "{\"stage\":\"test_reaped\",\"before_deadline\":"
                  << (monotonic_ns () < deadline_mono_ns ? "true" : "false") << "}\n";
        timespec delay{0, 500000000};
        nanosleep (&delay, nullptr);
      }
#endif
      const uint64_t now_ns = deadline_mono_ns ? monotonic_ns () : 0;
      if (deadline_mono_ns && now_ns >= deadline_mono_ns) {
        std::cerr << "{\"stage\":\"deadline\",\"status\":\"expired\"}\n";
        stop_children ();
        error (EXIT_CODE_UNKNOWN, "UNKNOWN\n");
      }
      if (g_interrupted) {
        stop_children ();
        error (EXIT_CODE_UNKNOWN, "UNKNOWN\n");
      }
      if (reaped == 0) {
        const uint64_t pause_ns = deadline_mono_ns
            ? std::min<uint64_t> (10000000, deadline_mono_ns - now_ns)
            : 10000000;
        timespec pause{0, static_cast<long> (pause_ns)};
        nanosleep (&pause, nullptr);
        continue;
      }
      if (reaped == -1) {
        if (wait_error == EINTR) continue;
        break;
      }

      // A child killed by a signal (SIGSEGV, SIGABRT, ...) has WIFEXITED
      // false; WEXITSTATUS would then return 0, which equals EXIT_CODE_REAL
      // and would be silently misreported as "REALIZABLE". Skip such children
      // so we either hear from a sibling that finished cleanly, or fall
      // through to UNKNOWN below.
      if (not WIFEXITED (status)) {
        if (WIFSIGNALED (status))
          std::cerr << "{\"stage\":\"child_signal\",\"status\":"
                    << WTERMSIG (status) << "}\n";
        continue;
      }
      int ret = WEXITSTATUS (status);
      if (ret == EXIT_CODE_ERROR) {
        child_reported_error = true;
        continue;
      }
      if (ret == EXIT_CODE_REAL or ret == EXIT_CODE_UNREAL) {
        // Publish the definitive answer before terminating the other children.
        if (ret == EXIT_CODE_REAL)
          std::cout << "REALIZABLE\n";
        else
          std::cout << "UNREALIZABLE\n";
        std::cout << std::flush;
        stop_children ();
        return ret;
      }
    }
    if (child_reported_error)
      error (EXIT_CODE_ERROR, "ERROR\n");
    error (EXIT_CODE_UNKNOWN, "UNKNOWN\n");

  } catch (const std::exception& e) {
    error (EXIT_CODE_ERROR, "Exception caught: %s\n", e.what ());
  } catch (...) {
    error (EXIT_CODE_ERROR, "Unknown exception\n");
  }
}
