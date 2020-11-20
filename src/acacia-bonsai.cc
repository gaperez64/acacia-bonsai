#include <config.h>

#include <memory>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "argmatch.h"

#include "common_aoutput.hh"
#include "common_finput.hh"
#include "common_setup.hh"
#include "common_sys.hh"

#include "k-bounded_safety_aut.hh"
#include "set_of_vectors.hh"
#include "basic_antichain.hh"

#include <spot/misc/bddlt.hh>
#include <spot/misc/escape.hh>
#include <spot/misc/game.hh>
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

enum {
  OPT_LOGK = 'k',
  OPT_INPUT = 'i',
  OPT_OUTPUT = 'o',
  OPT_STRAT = 's',
  OPT_VERBOSE = 'v'
};

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
  /**************************************************/
  { nullptr, 0, nullptr, 0, "Fine tuning:", 10 },
  {
    "logk", OPT_LOGK, "VAL", 0,
    "starting value of K, expressed as its logarithm", 0
  },
  /**************************************************/
  { nullptr, 0, nullptr, 0, "Output options:", 20 },
  {
    "strategy", OPT_STRAT, nullptr, 0,
    "compute a winning strategy when the input is satisfiable", 0
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
Synthesize a controller from its LTL specification.\v\
Exit status:\n\
  0   if the input problem is realizable\n\
  1   if the input problem is not realizable\n\
  2   if any error has been reported";

static std::vector<std::string> input_aps;
static std::vector<std::string> output_aps;

static bool opt_strat = false;
static unsigned opt_K = 5;
static spot::option_map extra_options;

static double trans_time = 0.0;
static double merge_time = 0.0;
static double solve_time = 0.0;

static int verbose = 0;

namespace {

  class ltl_processor final : public job_processor {
    private:
      spot::translator &trans_;
      std::vector<std::string> input_aps_;
      std::vector<std::string> output_aps_;

    public:

      ltl_processor (spot::translator &trans,
                     std::vector<std::string> input_aps_,
                     std::vector<std::string> output_aps_)
        : trans_ (trans), input_aps_ (input_aps_), output_aps_ (output_aps_) {
      }

      int solve_formula (spot::formula f) {
        spot::process_timer timer;
        timer.start ();
        spot::stopwatch sw;
        bool want_time = (verbose > 0);

        // To Universal co-Büchi Automaton
        trans_.set_type(spot::postprocessor::BA);
        // "Desired characteristics": Small and state-based acceptance (implied by BA).
        trans_.set_pref(spot::postprocessor::Small |
                        spot::postprocessor::SBAcc);

        if (want_time)
          sw.start ();

        ////////////////////////////////////////////////////////////////////////
        // Translate the formula to a UcB (Universal co-Büchi)
        // To do so, negate formula, and convert to a normal Büchi.
        f = spot::formula::Not (f);
        auto aut = trans_.run (&f);

        // Create BDDs for the input and output AP.
        bdd all_inputs = bddtrue;
        bdd all_outputs = bddtrue;
        for (const auto& ap_i : input_aps_)
        {
          unsigned v = aut->register_ap (spot::formula::ap(ap_i));
          all_inputs &= bdd_ithvar(v);
        }
        for (const auto& ap_i : output_aps_)
        {
          unsigned v = aut->register_ap (spot::formula::ap(ap_i));
          all_outputs &= bdd_ithvar(v);
        }

        if (want_time)
          trans_time = sw.stop ();

        if (verbose) {
          std::cerr << "translating formula done in "
                    << trans_time << " seconds\n";
          std::cerr << "automaton has " << aut->num_states ()
                    << " states and " << aut->num_sets () << " colors\n";
        }


        ////////////////////////////////////////////////////////////////////////
        // Simplify by merging (TODO: check if useful)

        if (want_time)
          sw.start();

        // Merges states that have the exact same outgoing edges. This method
        // compares the successors of states in the order in which they are
        // stored. Therefore, it is better to call it when you know that the
        // edges are stored, e.g. after a call to merge_edges().
        aut->merge_states();
        if (want_time)
          merge_time = sw.stop();

        if (verbose)
          std::cerr << "simplification done in " << merge_time
                    << " seconds\nDPA has " << aut->num_states()
                    << " states\n";

        ////////////////////////////////////////////////////////////////////////
        // Build S^K_N game, solve it.

        if (want_time)
          sw.start ();

        spot::print_hoa(std::cout, aut, nullptr) << '\n';
        // auto&& skn = k_bounded_safety_aut<set_of_vectors::vector,
        //                                   set_of_vectors::set> (aut, opt_K, all_inputs, all_outputs);

        auto&& skn = k_bounded_safety_aut<basic_antichain::vector,
                                          basic_antichain::set> (aut, opt_K, all_inputs, all_outputs);
        bool realizable = skn.solve (verbose);

        if (want_time)
          solve_time = sw.stop ();

        if (verbose)
          std::cerr << "safety game solved in " << solve_time << " seconds\n";

        timer.stop ();

        if (realizable) {
          std::cout << "REALIZABLE\n";

          if (opt_strat) {
            // TODO.
          }

          return 0;
        } else {
          std::cout << "UNREALIZABLE\n";
          return 1;
        }
      }

      int process_formula (spot::formula f, const char *, int) override {
        unsigned res = solve_formula (f);
        return res;
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

    case OPT_STRAT:
      opt_strat = true;
      break;

    case OPT_LOGK:
      opt_K = atoi (arg); // TODO: Better this.
      break;

    case OPT_VERBOSE:
      ++verbose;
      break;

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

int
main (int argc, char **argv) {
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
    ltl_processor processor (trans, input_aps, output_aps);

    // Diagnose unused -x options
    extra_options.report_unused_options ();
    return processor.run ();
  });
}
