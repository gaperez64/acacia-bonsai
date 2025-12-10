
#include <memory>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstring>

#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include <spot/misc/timer.hh>
#include <spot/misc/tmpfile.hh>
#include <spot/twaalgos/aiger.hh>
#include <spot/twaalgos/translate.hh>
#include <spot/misc/optionmap.hh>

#include <posets/downsets.hh>

#include "configuration.hh"
#include <utils/verbose.hh>
#include "solver/solver_invoker.hh"

#include "arg_parser.hh"
#include "error_msg.hh"

#define debug_(A...) do { if(utils::verbose > 0) { std::cout << A << std::endl; } } while (0)

using namespace std::literals;

static std::vector<std::string> input_aps;
static std::vector<std::string> output_aps;
static std::vector<int> init_state;


// static auto opt_unreal_x = DEFAULT_UNREAL_X;

static unsigned opt_K = DEFAULT_K,
  opt_Kmin = DEFAULT_KMIN, opt_Kinc = DEFAULT_KINC;

int               utils::verbose = 0;
utils::voutstream utils::vout;

size_t posets::vectors::bool_threshold = 0;
size_t posets::vectors::bitset_threshold = 0;

void terminate (int signum) {
  if (getpgid (0) == getpid ()) { // Main process
    signal (SIGTERM, SIG_IGN);
    kill (0, SIGTERM);
    while (wait (nullptr) != -1)
      /* no body */;
  }
  else
    _exit (3);
}


/**
 * Given the argument values that were parsed earlier, this will process the values and plug them into
 * the system.
 *
 * @param arg_vals The parsed argument values passed by the user.
 */
void process_args_(const arg_parse_result& arg_vals) {

  if (arg_vals.formula.empty()) {
    error(EXIT_CODE_ERROR, "Error: formula must be a non-empty string.");
  }

  init_state = arg_vals.init_state;

  for (const auto & input : arg_vals.inputs) {
    input_aps.push_back (input);
  }

  for (const auto & output : arg_vals.outputs) {
    output_aps.push_back (output);
  }

  opt_K = arg_vals.opt_kmax;
  opt_Kmin = arg_vals.opt_kstart;
  opt_Kinc = arg_vals.opt_kinc;
  utils::verbose = arg_vals.verbose_level;
}


static void sig_handler(int sig)
{
  spot::cleanup_tmpfiles();
  // Send the signal again, this time to the default handler, so that
  // we return a meaningful error code.
  raise(sig);
}

static void setup_sig_handler()
{
  struct sigaction sa;
  sa.sa_handler = sig_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESETHAND;
  // Catch termination signals, so we can clean up temporary files.
  sigaction(SIGALRM, &sa, nullptr);
  sigaction(SIGHUP, &sa, nullptr);
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGPIPE, &sa, nullptr);
  sigaction(SIGQUIT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
}


int main (int argc, char **argv) {

  // use boost to parse all arguments that were passed
  const auto arg_values = arg_parser(argc, argv);

  struct sigaction action;
  memset (&action, 0, sizeof(struct sigaction));
  action.sa_handler = terminate;
  sigaction (SIGTERM, &action, nullptr);
  sigaction (SIGINT, &action, nullptr);


  // remove all spot temporary files
  setup_sig_handler(); // in case of a signal
  atexit(spot::cleanup_tmpfiles); // in case of exit

  try {
    // These options play a role in twaalgos.
    spot::option_map extra_options;
    extra_options.set ("simul", 0);
    extra_options.set ("ba-simul", 0);
    extra_options.set ("det-simul", 0);
    extra_options.set ("tls-impl", 1);
    extra_options.set ("wdba-minimize", 2);

    process_args_(arg_values);

    // Adjust the value of K
    if (opt_Kmin == -1u)
      opt_Kmin = opt_K;
    if (opt_Kmin > opt_K or (opt_Kmin < opt_K and opt_Kinc == 0))
      error (EXIT_CODE_ERROR, "Incompatible values for K, Kmin, and Kinc.");
    if (opt_Kmin == 0)
      opt_Kmin = opt_K;

    // Setup the dictionary now, so that BuDDy's initialization is
    // not measured in our timings.
    spot::bdd_dict_ptr dict = spot::make_bdd_dict ();
    spot::translator trans (dict, &extra_options);


    // ltl_processor processor (trans, input_aps, output_aps, dict, opt_K,
      // opt_Kmin, opt_Kinc, init_state, arg_values.formula);
    // const int res = processor.run ();


    const int res = run_ltl(trans, input_aps, output_aps, dict, opt_K,
      opt_Kmin, opt_Kinc, init_state, arg_values.formula);

    // Diagnose unused -x options
    extra_options.report_unused_options ();

    switch (res) {
      case 1:
        std::cout << "REALIZABLE\n";
        break;
      case 0:
        std::cout << "UNKNOWN\n";
        break;
      default:
        error(EXIT_CODE_ERROR, "Unknown result code: '%d'", res);
        break;
    }

    if (arg_values.invert_exit_code) {
      exit((res != 0) ? EXIT_CODE_UNKNOWN : EXIT_CODE_REAL);
    } else {
      exit((res != 0) ? EXIT_CODE_REAL : EXIT_CODE_UNKNOWN);
    }
  }
  catch (const std::exception& e)
  {
    error(EXIT_CODE_ERROR, "%s", e.what());
  }
  catch (...) {
    error(EXIT_CODE_ERROR, "Unknown exception");
  }
}
