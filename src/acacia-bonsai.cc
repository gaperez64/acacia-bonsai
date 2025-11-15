// #include <config.h>
#include <memory>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <algorithm>

#include <signal.h>
#include <sys/wait.h>

#include <boost/algorithm/string.hpp>

#include "k-bounded_safety_aut.hh"

#include "arg_parser.hh"

#include <posets/vectors.hh>
#include <posets/downsets.hh>
#include "utils/static_switch.hh"
#include "boolean_states.hh"

#include <utils/verbose.hh>
#include <utils/cache.hh>

#include "configuration.hh"
#include "composition/composition_mt.hh"
#include "processor.hh"

#include <spot/misc/bddlt.hh>
#include <spot/misc/escape.hh>
#include <spot/misc/timer.hh>
#include <spot/misc/tmpfile.hh>
#include <spot/tl/formula.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/aiger.hh>
#include <spot/twaalgos/degen.hh>
#include <spot/twaalgos/determinize.hh>
#include <spot/twaalgos/parity.hh>
#include <spot/twaalgos/sbacc.hh>
#include <spot/twaalgos/totgba.hh>
#include <spot/twaalgos/translate.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/simulation.hh>
#include <spot/twaalgos/split.hh>
#include <spot/twaalgos/toparity.hh>
#include <spot/twaalgos/hoa.hh>


using namespace std::literals;

enum {
  OPT_K = 'K',
  OPT_Kmin = 'M',
  OPT_Kinc = 'I',
  OPT_UNREAL_X = 'u',
  OPT_INPUT = 'i',
  OPT_OUTPUT = 'o',
  OPT_CHECK = 'c',
  OPT_VERBOSE = 'v',
  OPT_SYNTH = 'S',
  OPT_WINREG = 'W',
  OPT_WORKERS = 'j',
  OPT_INIT = '0'
} ;

static std::vector<std::string> input_aps;
static std::vector<std::string> output_aps;
static std::string synth_fname;
static std::string winreg_fname;
static std::vector<int> init_state;
static int workers = 0;


enum {
  CHECK_REAL,
  CHECK_UNREAL,
  CHECK_BOTH
} opt_check = CHECK_REAL;

static auto opt_unreal_x = DEFAULT_UNREAL_X;

static bool check_real = true;
static unsigned opt_K = DEFAULT_K,
  opt_Kmin = DEFAULT_KMIN, opt_Kinc = DEFAULT_KINC;
static spot::option_map extra_options;

int               utils::verbose = 0;
utils::voutstream utils::vout;

size_t posets::vectors::bool_threshold = 0;
size_t posets::vectors::bitset_threshold = 0;

void terminate (int signum) {
  if (getpgid (0) == getpid ()) { // Main process
    signal (SIGTERM, SIG_IGN);
    kill (0, SIGTERM);
    while (wait (NULL) != -1)
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
void process_args_(const ArgParseResult& arg_vals) {
  init_state = arg_vals.init_state;

  for (const auto & input : arg_vals.inputs) {
    input_aps.push_back (input);
  }

  for (const auto & output : arg_vals.outputs) {
    output_aps.push_back (output);
  }

  synth_fname = arg_vals.synth_fname;
  opt_K = arg_vals.opt_Kmax;
  opt_Kmin = arg_vals.opt_Kstart;
  opt_Kinc = arg_vals.opt_Kinc;
  utils::verbose = arg_vals.verbose_level;

  if (not arg_vals.extra_opts.empty()) {
    extra_options.parse_options (arg_vals.extra_opts.c_str());
  }

  lbt_input = arg_vals.lbt_input;
  lenient = arg_vals.lenient;
  if (arg_vals.is_file) {
    jobs.emplace_back(arg_vals.formula.c_str(), true);
  } else {
    jobs.emplace_back(arg_vals.formula.c_str(), false);
  }
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
  // Catch termination signals, so we can cleanup temporary files.
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
  sigaction (SIGTERM, &action, NULL);
  sigaction (SIGINT, &action, NULL);


  // remove all spot temporary files
  setup_sig_handler(); // in case of a signal
  atexit(spot::cleanup_tmpfiles); // in case of exit

  try {
    // These options play a role in twaalgos.
    extra_options.set ("simul", 0);
    extra_options.set ("ba-simul", 0);
    extra_options.set ("det-simul", 0);
    extra_options.set ("tls-impl", 1);
    extra_options.set ("wdba-minimize", 2);

    process_args_(arg_values);

    check_no_formula ();

    // Adjust the value of K
    // TODO: moved this upwards. By afterwards adjusting the KMIN global variable, this influenced the behaviour of various async code.
    if (opt_Kmin == -1u)
      opt_Kmin = opt_K;
    if (opt_Kmin > opt_K or (opt_Kmin < opt_K and opt_Kinc == 0))
      error (3, 0, "Incompatible values for K, Kmin, and Kinc.");
    if (opt_Kmin == 0)
      opt_Kmin = opt_K;

    // Setup the dictionary now, so that BuDDy's initialization is
    // not measured in our timings.
    spot::bdd_dict_ptr dict = spot::make_bdd_dict ();
    spot::translator trans (dict, &extra_options);
    ltl_processor processor (trans, input_aps, output_aps, dict, synth_fname, winreg_fname, check_real,
      opt_unreal_x, workers, opt_K, opt_Kmin, opt_Kinc, init_state);

    // Diagnose unused -x options
    extra_options.report_unused_options ();



    const auto start_proc = [&] (bool real, unreal_x_t unreal_x) {
      if (fork () == 0) {
        utils::vout.set_prefix (std::string {"["}
                                + (real ?
                                   "real" :
                                   std::string {"unreal-x="} + (char) unreal_x)
                                + "] ");
        check_real = real;
        if (!real) {
          synth_fname = ""; // no synthesis for the environment if the formula is unrealizable
        }
        opt_unreal_x = unreal_x;
        int res = processor.run ();
        verb_do (1, vout << "returning " << (res ? 1 - real : 3) << "\n");
        exit (res ? 1 - real : 3);  // 0 if real, 1 if unreal, 3 if unknown
      }
    };

    setpgid (0, 0);
    assert (getpgid (0) == getpid ());
    if (opt_check == CHECK_BOTH or opt_check == CHECK_REAL)
      start_proc (true, UNREAL_X_BOTH);
    if (opt_check == CHECK_BOTH or opt_check == CHECK_UNREAL) {
      if (opt_unreal_x == UNREAL_X_BOTH or opt_unreal_x == UNREAL_X_FORMULA)
        start_proc (false, UNREAL_X_FORMULA);
      if (opt_unreal_x == UNREAL_X_BOTH or opt_unreal_x == UNREAL_X_AUTOMATON)
        start_proc (false, UNREAL_X_AUTOMATON);
    }

    int ret;
    while (wait (&ret) != -1) { // as long as we have children to wait for
      if (not WIFEXITED (ret)) {
        std::cout << "ERROR: A child died unexepectedly";
        if (WIFSIGNALED (ret))
          std::cout << " with signal " << WTERMSIG (ret);
        std::cout << std::endl;
        terminate (0);
        abort ();
      }

      ret = WEXITSTATUS (ret);
      if (ret < 3) {
        terminate (0);
        if (ret == 0)
          std::cout << "REALIZABLE\n";
        else
          std::cout << "UNREALIZABLE\n";
        return ret;
      }
    }
    std::cout << "UNKNOWN\n";
    return 3;
  }
  catch (const std::exception& e)
  {
    error(2, 0, "%s", e.what());
  }
  catch (...) {
    error(2, 0, "Unknown exception");
  }
}
