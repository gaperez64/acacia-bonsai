#include <config.h>
#include <memory>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <algorithm>

#include <signal.h>
#include <sys/wait.h>

#include <boost/algorithm/string.hpp>

#include "argmatch.h"

#include "common_aoutput.hh"
#include "common_finput.hh"
#include "common_setup.hh"
#include "common_sys.hh"

#include "aut_preprocessors.hh"

#include "k-bounded_safety_aut.hh"

#include "vectors.hh"
#include "downsets.hh"
#include "utils/static_switch.hh"
#include "boolean_states.hh"

#include <utils/verbose.hh>
#include <utils/cache.hh>

#include "configuration.hh"
#include "composition/composition_mt.hh"

#include <spot/misc/bddlt.hh>
#include <spot/misc/escape.hh>
#include <spot/misc/timer.hh>
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
  OPT_WORKERS = 'j'
} ;

/*
enum unreal_x_t {
  UNREAL_X_FORMULA = 'f',
  UNREAL_X_AUTOMATON = 'a',
  UNREAL_X_BOTH
};
*/

static const argp_option options[] = {
  /**************************************************/
  { nullptr, 0, nullptr, 0, "Input options:", 1 },
  {
    "ins", OPT_INPUT, "PROPS", 0,
    "comma-separated list of uncontrollable (a.k.a. input) atomic"
    " propositions", 0
  },
  {
    "outs", OPT_OUTPUT, "PROPS", 0,
    "comma-separated list of controllable (a.k.a. output) atomic"
    " propositions", 0
  },
  {
    "synth", OPT_SYNTH, "FNAME", 0,
    "enable synthesis, pass .aag filename, or - to print gates", 0
  },
    {
    "workers", OPT_WORKERS, "VAL", 0,
    "Number of parallel workers for composition", 0
  },
  /**************************************************/
  { nullptr, 0, nullptr, 0, "Fine tuning:", 10 },
  {
    "K", OPT_K, "VAL", 0,
    "final value of K, or unique value if Kmin is not specified", 0
  },
  {
    "Kmin", OPT_Kmin, "VAL", 0,
    "starting value of K; Kinc MUST be set when using this option", 0
  },
  {
    "Kinc", OPT_Kinc, "VAL", 0,
    "increment value for K, used when Kmin < K", 0
  },
  {
    "unreal-x", OPT_UNREAL_X, "[formula|automaton|both]", 0,
    "for unrealizability, either add X's to outputs in the"
    " input formula, or push outputs one transition forward in"
    " the automaton; with 'both', two processes are started,"
    " one for each option (default: "
#if DEFAULT_UNREAL_X == UNREAL_X_FORMULA
    "formula"
#elif DEFAULT_UNREAL_X == UNREAL_X_AUTOMATON
    "automaton"
#else
    "both"
#endif
    ").", 0
  },

  /**************************************************/
  { nullptr, 0, nullptr, 0, "Output options:", 20 },
  {
    "check", OPT_CHECK, "[real|unreal|both]", 0,
    "either check for real, unreal, or both", 0
  },
  {
    "verbose", OPT_VERBOSE, nullptr, 0,
    "verbose mode, can be repeated for more verbosity", -1
  },
  { nullptr, 0, nullptr, 0, nullptr, 0 },
};

static const struct argp_child children[] = {
  // Input format
  { &finput_argp_headless, 0, nullptr, 0 },
  //{ &aoutput_o_format_argp, 0, nullptr, 0 },
  { &misc_argp, 0, nullptr, 0 },
  { nullptr, 0, nullptr, 0 }
};

const char argp_program_doc[] = "\
Verify realizability for LTL specification.\v\
Exit status:\n\
  0   if the input problem is realizable\n\
  1   if the input problem is not realizable\n\
  2   if this could not be decided\n\
  3   if any error has been reported";

static std::vector<std::string> input_aps;
static std::vector<std::string> output_aps;
static std::string synth_fname;
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


namespace {

  class ltl_processor final : public job_processor {
    private:
      spot::translator &trans_;
      std::vector<std::string> input_aps_;
      std::vector<std::string> output_aps_;
      std::vector<spot::formula> formulas;

      spot::bdd_dict_ptr dict;

    public:

      ltl_processor (spot::translator &trans,
                     std::vector<std::string> input_aps_,
                     std::vector<std::string> output_aps_,
                     spot::bdd_dict_ptr dict_)
        : trans_ (trans), input_aps_ (input_aps_), output_aps_ (output_aps_), dict (dict_) {
      }

      int process_formula (spot::formula f, const char *, int) override {
        formulas.push_back (f);
        return 0;
      }

      int run () override {
        // call base class ::run which adds the formulas passed with -f to the vector
        job_processor::run ();

        if (formulas.empty ()) {
          utils::vout << "Pass a formula!\n";
          return 0;
        }

        // manually register inputs/outputs
        bdd all_inputs = bddtrue;
        bdd all_outputs = bddtrue;

        for(std::string ap: input_aps) {
          unsigned v = dict->register_proposition (spot::formula::ap (ap), this);
          all_inputs &= bdd_ithvar (v);
        }
        for(std::string ap: output_aps) {
          unsigned v = dict->register_proposition (spot::formula::ap (ap), this);
          all_outputs &= bdd_ithvar (v);
        }

        composition_mt composer (opt_K, opt_Kmin, opt_Kinc, dict, trans_, all_inputs, all_outputs, input_aps_, output_aps_);

        if (formulas.size () == 1) {
          // one formula: don't make subprocesses, do everything here by calling the functions directly
          return composer.run_one (formulas[0], synth_fname, check_real, opt_unreal_x);
        }


        if (!check_real) {
          utils::vout << "Error: can't do composition for unrealizability!\n";
          return 0;
        }

        if (opt_Kinc != 0) {
          utils::vout << "Error: can't do composition with incrementing K values!\n";
          return 0;
        }

        for(spot::formula& f: formulas) {
          composer.add_formula (f);
        }

        return composer.run (workers, synth_fname);
      }

      ~ltl_processor () override {
        dict->unregister_all_my_variables (this);
      }
  };
}

static int
parse_opt (int key, char *arg, struct argp_state *) {
  // Called from C code, so should not raise any exception.
  BEGIN_EXCEPTION_PROTECT;

  switch (key) {
    case OPT_INPUT: {
      std::istringstream aps (arg);
      std::string ap;

      while (std::getline (aps, ap, ',')) {
        ap.erase (remove_if (ap.begin (), ap.end (), isspace), ap.end ());
        input_aps.push_back (ap);
      }

      break;
    }

    case OPT_OUTPUT: {
      std::istringstream aps (arg);
      std::string ap;

      while (std::getline (aps, ap, ',')) {
        ap.erase (remove_if (ap.begin (), ap.end (), isspace), ap.end ());
        output_aps.push_back (ap);
      }

      break;
    }

    case OPT_SYNTH: {
      synth_fname = arg;
      break;
    }

    case OPT_WORKERS: {
      workers = atoi (arg);
      break;
    }

    case OPT_UNREAL_X: {
      boost::algorithm::to_lower (arg);
      if (arg == "formula"sv)
        opt_unreal_x = UNREAL_X_FORMULA;
      else if (arg == "automaton"sv)
        opt_unreal_x = UNREAL_X_AUTOMATON;
      else if (arg == "both"sv)
        opt_unreal_x = UNREAL_X_BOTH;
      else
        error (3, 0, "Should specify formula, automaton, or both.");
      break;
    }

    case OPT_CHECK: {
      boost::algorithm::to_lower (arg);
      if (arg == "real"sv)
        opt_check = CHECK_REAL;
      else if (arg == "unreal"sv)
        opt_check = CHECK_UNREAL;
      else if (arg == "both"sv)
        opt_check = CHECK_BOTH;
      else
        error (3, 0, "Should specify real, unreal, or both.");
      break;
    }

    case OPT_K: {
      opt_K = atoi (arg);
      if (opt_K == 0)
        error (3, 0, "K cannot be 0 or not a number.");
      break;
    }

    case OPT_Kmin: {
      opt_Kmin = atoi (arg);
      if (opt_Kmin == 0)
        error (3, 0, "Kmin cannot be 0 or not a number.");
      break;
    }

    case OPT_Kinc: {
      opt_Kinc = atoi (arg);
      if (opt_Kinc == 0)
        error (3, 0, "Kinc cannot be 0 or not a number.");
      break;
    }

    case OPT_VERBOSE: {
      ++utils::verbose;
      break;
    }

    case 'x': {
      const char *opt = extra_options.parse_options (arg);

      if (opt)
        error (2, 0, "failed to parse --options near '%s'", opt);
    }
    break;
  }

  END_EXCEPTION_PROTECT;
  return 0;
}

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

int main (int argc, char **argv) {
  struct sigaction action;
  memset (&action, 0, sizeof(struct sigaction));
  action.sa_handler = terminate;
  sigaction (SIGTERM, &action, NULL);

  return protected_main (argv, [&] {
    // These options play a role in twaalgos.
    extra_options.set ("simul", 0);
    extra_options.set ("ba-simul", 0);
    extra_options.set ("det-simul", 0);
    extra_options.set ("tls-impl", 1);
    extra_options.set ("wdba-minimize", 2);
    const argp ap = {
      options, parse_opt, nullptr,
      argp_program_doc, children, nullptr, nullptr
    };

    if (int err = argp_parse (&ap, argc, argv, ARGP_NO_HELP, nullptr, nullptr))
      exit (err);
    check_no_formula ();

    // Setup the dictionary now, so that BuDDy's initialization is
    // not measured in our timings.
    spot::bdd_dict_ptr dict = spot::make_bdd_dict ();
    spot::translator trans (dict, &extra_options);
    ltl_processor processor (trans, input_aps, output_aps, dict);

    // Diagnose unused -x options
    extra_options.report_unused_options ();

    // Adjust the value of K
    if (opt_Kmin == -1u)
      opt_Kmin = opt_K;
    if (opt_Kmin > opt_K or (opt_Kmin < opt_K and opt_Kinc == 0))
      error (3, 0, "Incompatible values for K, Kmin, and Kinc.");
    if (opt_Kmin == 0)
      opt_Kmin = opt_K;

    const auto start_proc = [&] (bool real, unreal_x_t unreal_x) {
      if (fork () == 0) {
        utils::vout.set_prefix (std::string {"["}
                                + (real ?
                                   "real" :
                                   std::string {"unreal-x="} + (char) unreal_x)
                                + "] ");
        check_real = real;
        if (!real)
          synth_fname = ""; // no synthesis for the environment if the formula is unrealizable
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
  });
}
