#include "arg_parser.hh"
#include "configuration.hh"
#include "error_msg.hh"
#include "solver/solver_invoker.hh"
#include <unordered_map>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <signal.h>
#include <spot/misc/optionmap.hh>
#include <spot/misc/timer.hh>
#include <spot/misc/tmpfile.hh>
#include <spot/twaalgos/aiger.hh>
#include <spot/twaalgos/translate.hh>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utils/verbose.hh>
#include <vector>

#include <posets/downsets.hh>

#define debug_(A...)               \
  do {                             \
    if (utils::verbose > 0) {      \
      std::cout << A << std::endl; \
    }                              \
  } while (0)

using namespace std::literals;

// Definitions for some external variables.
// FIXME: Could be refactored.
int utils::verbose = 0;
utils::voutstream utils::vout;
size_t posets::vectors::bool_threshold = 0;
size_t posets::vectors::bitset_threshold = 0;

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

int main (int argc, char** argv) {
  // parse all arguments that were passed
  auto arg_values = arg_parser (argc, argv);
  // set the global verbose level
  utils::verbose = arg_values.verbose_level;

  struct sigaction action;
  memset (&action, 0, sizeof (struct sigaction));
  action.sa_handler = terminate;
  sigaction (SIGTERM, &action, nullptr);
  sigaction (SIGINT, &action, nullptr);

  try {
    // These options play a role in twaalgos.
    spot::option_map extra_options;
    extra_options.set ("simul", 0);
    extra_options.set ("ba-simul", 0);
    extra_options.set ("det-simul", 0);
    extra_options.set ("tls-impl", 1);
    extra_options.set ("wdba-minimize", 2);

    // Setup the dictionary now: BuDDy's initialization
    spot::bdd_dict_ptr dict = spot::make_bdd_dict ();
    spot::translator trans (dict, &extra_options);

    const auto start_proc = [&] (std::optional<unreal_x_t> unreal_x) {
      if (fork () == 0) {
        utils::vout.set_prefix (
            std::string {"["} +
            (not unreal_x.has_value () ? "real" : std::string {"unreal-x="} + (char) *unreal_x) +
            "] ");
        const bool res =
            run_ltl (trans, arg_values.inputs, arg_values.outputs, dict, arg_values.opt_k,
                     arg_values.opt_kmin, arg_values.opt_kinc, arg_values.formula, unreal_x);
        verb_do (1, vout << "returning " << res << "\n");

        // Diagnose unused -x options, or not?
        extra_options.report_unused_options ();

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
    // We always start a realizability check
    start_proc (std::nullopt);

    if (arg_values.opt_unreal_x.has_value ()) {
      if (*(arg_values.opt_unreal_x) == UNREAL_X_BOTH or
          *(arg_values.opt_unreal_x) == UNREAL_X_FORMULA)
        start_proc (std::make_optional<unreal_x_t> (UNREAL_X_FORMULA));
      if (*(arg_values.opt_unreal_x) == UNREAL_X_BOTH or
          *(arg_values.opt_unreal_x) == UNREAL_X_AUTOMATON)
        start_proc (std::make_optional<unreal_x_t> (UNREAL_X_AUTOMATON));
    }

    int ret;
    while (wait (&ret) != -1) {  // as long as we have children to wait for
      ret = WEXITSTATUS (ret);
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
    error (EXIT_CODE_UNKNOWN, "UNKNOWN\n");

  } catch (const std::exception& e) {
    error (EXIT_CODE_ERROR, "%s", e.what ());
  } catch (...) {
    error (EXIT_CODE_ERROR, "Unknown exception\n");
  }
}
