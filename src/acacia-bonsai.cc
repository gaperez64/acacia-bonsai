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

#include "aut_preprocessors.hh"

#include "k-bounded_safety_aut.hh"

#include "vectors.hh"
#include "downsets.hh"
#include "utils/static_switch.hh"
#include "boolean_states.hh"

#include "configuration.hh"

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

enum {
  OPT_K = 'K',
  OPT_Kmin = 'M',
  OPT_Kinc = 'I',
  OPT_INPUT = 'i',
  OPT_OUTPUT = 'o',
  OPT_STRAT = 's',
  OPT_VERBOSE = 'v'
} ;

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
static unsigned opt_K = DEFAULT_K,
  opt_Kmin = -1u, opt_Kinc = 0;
static spot::option_map extra_options;

static double trans_time = 0.0;
static double merge_time = 0.0;
static double boolean_states_time = 0.0;
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
                        //spot::postprocessor::Complete | // TODO: We did not need that originally; do we now?
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
        // Preprocess automaton

        if (want_time)
          sw.start();

        auto aut_preprocessors_maker = aut_preprocessors::surely_losing ();
        (aut_preprocessors_maker.make (aut, all_inputs, all_outputs, opt_K, verbose)) ();

        if (want_time)
          merge_time = sw.stop();

        if (verbose) {
          std::cerr << "preprocessing done in " << merge_time
                    << " seconds\nDPA has " << aut->num_states()
                    << " states\n";
          spot::print_hoa(std::cerr, aut, nullptr) << std::endl;
        }

        ////////////////////////////////////////////////////////////////////////
        // Boolean states

        if (want_time)
          sw.start ();

        auto boolean_states_maker = boolean_states::forward_saturation ();
        vectors::bool_threshold = (boolean_states_maker.make (aut, opt_K, verbose)) ();

        if (want_time)
          boolean_states_time = sw.stop ();

        if (verbose) {
          std::cerr  << "computation of boolean states in " << boolean_states_time
                     << ", found " << vectors::bool_threshold << " boolean states.\n";
        }

        // Special case: only boolean states, so... no useful accepting state.
        if (vectors::bool_threshold == 0) {
          std::cout << "REALIZABLE\n";
          return 0;
        }

        ////////////////////////////////////////////////////////////////////////
        // Build S^K_N game, solve it.

        bool realizable = false;

        if (want_time)
          sw.start ();

        // Compute how many boolean states will actually be put in bitsets.
        constexpr auto max_bools_in_bitsets = vectors::nbitsets_to_nbools (STATIC_MAX_BITSETS);
        auto nbitsetbools = aut->num_states () - vectors::bool_threshold;
        if (nbitsetbools > max_bools_in_bitsets) {
          if (verbose)
            std::cerr << "Warning: bitsets not large enough, using regular vectors for some Boolean states.\n"
                      << "\tTotal # of Boolean-for-bitset states: " << nbitsetbools
                      << ", max: " << max_bools_in_bitsets << std::endl;
          nbitsetbools = max_bools_in_bitsets;
        }

        // Maximize usage of the nonbool implementation
        auto nonbools = aut->num_states () - nbitsetbools;
        size_t actual_nonbools = (nonbools <= STATIC_ARRAY_CAP_MAX) ?
          vectors::traits<vectors::ARRAY_IMPL, VECTOR_ELT_T>::capacity_for (nonbools) :
          vectors::traits<vectors::VECTOR_IMPL, VECTOR_ELT_T>::capacity_for (nonbools);
        if (actual_nonbools >= aut->num_states ())
          nbitsetbools = 0;
        else
          nbitsetbools -= (actual_nonbools - nonbools);

        vectors::bitset_threshold = aut->num_states () - nbitsetbools;

#define UNREACHABLE [] (int x) { assert (false); }

        if (actual_nonbools <= STATIC_ARRAY_CAP_MAX) { // Array & Bitsets
          static_switch_t<STATIC_ARRAY_CAP_MAX> {} (
            [&] (auto vnonbools) {
              static_switch_t<STATIC_MAX_BITSETS> {} (
                [&] (auto vbitsets) {
                  auto skn = K_BOUNDED_SAFETY_AUT_IMPL<
                    downsets::ARRAY_AND_BITSET_DOWNSET_IMPL<
                      vectors::X_and_bitset<
                        vectors::ARRAY_IMPL<VECTOR_ELT_T, vnonbools.value>,
                        vbitsets.value>>>
                    (aut, opt_Kmin, opt_K, opt_Kinc, all_inputs, all_outputs, verbose);
                  realizable = skn.solve ();
                },
                UNREACHABLE,
                vectors::nbools_to_nbitsets (nbitsetbools));
            },
            UNREACHABLE,
            actual_nonbools);
        }
        else {                                  // Vectors & Bitsets
          static_switch_t<STATIC_MAX_BITSETS> {} (
            [&] (auto vbitsets) {
              auto skn = K_BOUNDED_SAFETY_AUT_IMPL<
                downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<
                  vectors::X_and_bitset<
                    vectors::VECTOR_IMPL<VECTOR_ELT_T>,
                    vbitsets.value>>>
                (aut, opt_Kmin, opt_K, opt_Kinc, all_inputs, all_outputs, verbose);
              realizable = skn.solve ();
            },
            UNREACHABLE,
            vectors::nbools_to_nbitsets (nbitsetbools));
        }

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

    case OPT_K:
      opt_K = atoi (arg);
      if (opt_K == 0)
        error (3, 0, "K cannot be 0 or not a number.");
      break;

    case OPT_Kmin:
      opt_Kmin = atoi (arg);
      if (opt_Kmin == 0)
        error (3, 0, "Kmin cannot be 0 or not a number.");
      break;

    case OPT_Kinc:
      opt_Kinc = atoi (arg);
      if (opt_Kinc == 0)
        error (3, 0, "Kinc cannot be 0 or not a number.");
      break;

    case OPT_VERBOSE:
      ++verbose;
      break;

    case 'x':
    {
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

    // Adjust the value of K
    if (opt_Kmin == -1u)
      opt_Kmin = opt_K;
    if (opt_Kmin > opt_K or (opt_Kmin < opt_K and opt_Kinc == 0))
      error (3, 0, "Incompatible values for K, Kmin, and Kinc.");
    if (opt_Kmin == 0)
      opt_Kmin = opt_K;

    return processor.run ();
  });
}
