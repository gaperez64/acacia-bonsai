#include "arg_parser.hh"
#include "configuration.hh"
#include "error_msg.hh"
#include "solver/solver_invoker.hh"
#include <unordered_map>

#include <algorithm>
#include <cassert>
#include <csignal>
#include <cstring>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <sys/wait.h>
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
  void terminate ([[maybe_unused]] int signum) {
    if (getpgid (0) == getpid ()) {  // Main process
      signal (SIGTERM, SIG_IGN);
      kill (0, SIGTERM);
      while (wait (nullptr) != -1)
        /* no body */;
    }
    else
      _exit (EXIT_CODE_UNKNOWN);  // child procs avoid cleaning on exit
  }

  const char* unreal_strategy_name (UNREAL_X_T strategy) {
    if (strategy == UNREAL_X_FORMULA)
      return "formula";
    if (strategy == UNREAL_X_AUTOMATON)
      return "automaton";
    return "unknown";
  }
}

int main (int argc, char** argv) {
  // parse all arguments that were passed
  auto arg_values = arg_parser (argc, argv);
  // set the global verbose level
  utils::verbose = arg_values.verbose_level;

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
    const auto start_proc = [&] (std::optional<UNREAL_X_T> unreal_x,
                                 TRANSLATION_PREF_T translation_pref,
                                 acacia::game_backend backend) {
      if (fork () == 0) {
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
        verb_do (1, vout << "Starting solver child\n" << std::flush);
        const bool res = run_ltl (arg_values.inputs, arg_values.outputs, arg_values.opt_k,
                                  arg_values.opt_kmin, arg_values.opt_kinc, arg_values.formula,
                                  unreal_x, translation_pref, arg_values.spot_fast,
                                  backend,
                                  unreal_x.has_value () ? std::nullopt : arg_values.synth_fname,
                                  arg_values.metadata);
        verb_do (1, vout << "returning " << res << "\n");

        if (unreal_x.has_value ())
          exit (res ? EXIT_CODE_UNREAL : EXIT_CODE_UNKNOWN);
        else
          exit (res ? EXIT_CODE_REAL : EXIT_CODE_UNKNOWN);
      }
    };

    // We fork process for each (UN)REAL check now and then wait for them to
    // return to process their exit codes
    setpgid (0, 0);
    assert (getpgid (0) == getpid ());

    assert (arg_values.arms.has_value ());
    [[maybe_unused]] const size_t child_count = arg_values.arms->size ();
    verb_do (1, vout << "Starting " << child_count << " solver children\n" << std::flush);

    for (const auto& arm : *arg_values.arms) {
      acacia::game_backend backend = arm.backend;
      if (arg_values.synth_fname.has_value () and not arm.unreal and
          backend != acacia::game_backend::backward) {
        verb_do (1, vout << "Forcing the real backend to backward for synthesis\n" << std::flush);
        backend = acacia::game_backend::backward;
      }
      start_proc (arm.unreal ? std::make_optional<UNREAL_X_T> (arm.unreal_x) : std::nullopt,
                  arm.translation_pref, backend);
    }

    int status;
    bool child_reported_error = false;
    while (wait (&status) != -1) {  // as long as we have children to wait for
      // A child killed by a signal (SIGSEGV, SIGABRT, ...) has WIFEXITED
      // false; WEXITSTATUS would then return 0, which equals EXIT_CODE_REAL
      // and would be silently misreported as "REALIZABLE". Skip such children
      // so we either hear from a sibling that finished cleanly, or fall
      // through to UNKNOWN below.
      if (not WIFEXITED (status))
        continue;
      int ret = WEXITSTATUS (status);
      if (ret == EXIT_CODE_ERROR) {
        child_reported_error = true;
        continue;
      }
      if (ret == EXIT_CODE_REAL or ret == EXIT_CODE_UNREAL) {
        // One child has a definitive answer! Kill everyone else
        terminate (0);
        if (ret == EXIT_CODE_REAL)
          std::cout << "REALIZABLE\n";
        else
          std::cout << "UNREALIZABLE\n";
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
