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

// Definitions for some external variables. Needs to be refactored.
int utils::verbose = 0;
utils::voutstream utils::vout;

size_t posets::vectors::bool_threshold = 0;
size_t posets::vectors::bitset_threshold = 0;

namespace {
  void terminate (int signum) {
    if (getpgid (0) == getpid ()) {  // Main process
      signal (SIGTERM, SIG_IGN);
      kill (0, SIGTERM);
      while (wait (nullptr) != -1)
        /* no body */;
    }
    else
      _exit (3);
  }

  void sig_handler (int sig) {
    spot::cleanup_tmpfiles ();
    // Send the signal again, this time to the default handler, so that
    // we return a meaningful error code.
    raise (sig);
  }

  void setup_sig_handler () {
    struct sigaction sa;
    sa.sa_handler = sig_handler;
    sigemptyset (&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    // Catch termination signals, so we can clean up temporary files.
    sigaction (SIGALRM, &sa, nullptr);
    sigaction (SIGHUP, &sa, nullptr);
    sigaction (SIGINT, &sa, nullptr);
    sigaction (SIGPIPE, &sa, nullptr);
    sigaction (SIGQUIT, &sa, nullptr);
    sigaction (SIGTERM, &sa, nullptr);
  }
}

int main (int argc, char** argv) {
  // use boost to parse all arguments that were passed
  auto arg_values = arg_parser (argc, argv);

  struct sigaction action;
  memset (&action, 0, sizeof (struct sigaction));
  action.sa_handler = terminate;
  sigaction (SIGTERM, &action, nullptr);
  sigaction (SIGINT, &action, nullptr);

  // remove all spot temporary files
  setup_sig_handler ();             // in case of a signal
  atexit (spot::cleanup_tmpfiles);  // in case of exit

  try {
    // These options play a role in twaalgos.
    spot::option_map extra_options;
    extra_options.set ("simul", 0);
    extra_options.set ("ba-simul", 0);
    extra_options.set ("det-simul", 0);
    extra_options.set ("tls-impl", 1);
    extra_options.set ("wdba-minimize", 2);

    // Adjust the value of K
    if (arg_values.opt_kmin == -1U)
      arg_values.opt_kmin = arg_values.opt_k;
    if (arg_values.opt_kmin > arg_values.opt_k
        or (arg_values.opt_kmin <= arg_values.opt_k
            and arg_values.opt_kinc == 0))
      error (EXIT_CODE_ERROR,
             "Incompatible values for K (%d), Kmin (%d), and Kinc (%d).",
             arg_values.opt_k,
             arg_values.opt_kmin,
             arg_values.opt_kinc);
    if (arg_values.opt_kmin == 0)
      arg_values.opt_kmin = arg_values.opt_k;

    // Setup the dictionary now, so that BuDDy's initialization is
    // not measured in our timings.
    spot::bdd_dict_ptr dict = spot::make_bdd_dict ();
    spot::translator trans (dict, &extra_options);

    const int res = run_ltl (trans, 
                             arg_values.inputs,
                             arg_values.outputs, 
                             dict,
                             arg_values.opt_k,
                             arg_values.opt_kmin,
                             arg_values.opt_kinc,
                             arg_values.formula,
                             std::nullopt);

    // Diagnose unused -x options
    extra_options.report_unused_options ();

    switch (res) {
      case 1: std::cout << "REALIZABLE\n"; break;
      case 0: std::cout << "UNKNOWN\n"; break;
      default: error (EXIT_CODE_ERROR, "Unknown result code: '%d'", res); break;
    }

    exit ((res != 0) ? EXIT_CODE_REAL : EXIT_CODE_UNKNOWN);
  } catch (const std::exception& e) {
    error (EXIT_CODE_ERROR, "%s", e.what ());
  } catch (...) {
    error (EXIT_CODE_ERROR, "Unknown exception");
  }
}
