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
  OPT_VERBOSE = 'v'
} ;

enum unreal_x_t {
  UNREAL_X_FORMULA = 'f',
  UNREAL_X_AUTOMATON = 'a',
  UNREAL_X_BOTH
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

static double trans_time = 0.0;
static double merge_time = 0.0;
static double boolean_states_time = 0.0;
static double solve_time = 0.0;

int               utils::verbose = 0;
utils::voutstream utils::vout;


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

      using aut_t = decltype (trans_.run (spot::formula::ff ()));

      // Changes q -> <i', o'> -> q' with saved o to
      // q -> <i', o> -> {q' saved o}
      aut_t push_outputs (const aut_t& aut, bdd all_inputs, bdd all_outputs) {
        auto ret = spot::make_twa_graph (aut->get_dict ());
        ret->copy_acceptance_of (aut);
        ret->copy_ap_of (aut);
        ret->prop_copy (aut, spot::twa::prop_set::all());
        ret->prop_universal (spot::trival::maybe ());

        static auto cache = utils::make_cache<unsigned> (0u, 0u);
        const auto build_aut = [&] (unsigned state, bdd saved_o,
                                    const auto& recurse) {
          auto cached = cache.get (state, saved_o.id ());
          if (cached) return *cached;
          auto ret_state = ret->new_state ();
          cache (ret_state, state, saved_o.id ());
          for (auto& e : aut->out (state)) {
            auto cond = e.cond;
            // e.cond = i1 & o1 || !i1 & !o1

            while (cond != bddfalse) {
              // Pick one satisfying assignment where outputs all have values
              bdd one_sat = bdd_satoneset (cond, all_outputs, bddtrue);
              // Get the corresponding input bdd
              bdd one_input_bdd =
                bdd_exist (cond & bdd_exist (one_sat, all_inputs),
                           all_outputs);
              ret->new_edge (ret_state,
                             recurse (e.dst,
                                      bdd_exist (cond & one_input_bdd,
                                                all_inputs),
                                      recurse),
                             saved_o & one_input_bdd,
                             e.acc);
              cond -= one_input_bdd;
            }
          }
          return ret_state;
        };
        build_aut (aut->get_init_state_number (), bddtrue, build_aut);
        return ret;
      }

      bool solve_formula (spot::formula f) {
        spot::process_timer timer;
        timer.start ();

        spot::stopwatch sw, sw_nospot;
        bool want_time = true; // Hardcoded

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
        if (check_real)
          f = spot::formula::Not (f);
        else if (opt_unreal_x == UNREAL_X_FORMULA) {
          // Add X at the outputs
          auto rec = [this] (auto&& self, spot::formula m) {
            if (m.is (spot::op::ap) and
                (std::ranges::find (output_aps_,
                                    m.ap_name ()) != output_aps_.end ()))
              return spot::formula::X (m);
            return m.map ([&] (spot::formula t) { return self (self, t); });
          };
          f = f.map ([&] (spot::formula t) { return rec (rec, t); });
          // Swap I and O.
          input_aps_.swap (output_aps_);
        }

	verb_do (1, vout << "Formula: " << f << std::endl);

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
        // If unreal but we haven't pushed outputs yet using X on formula
        if (not check_real and opt_unreal_x == UNREAL_X_AUTOMATON) {
          aut = push_outputs (aut, all_inputs, all_outputs);
          input_aps_.swap (output_aps_);
          std::swap (all_inputs, all_outputs);
        }

        if (want_time) {
          trans_time = sw.stop ();
          utils::vout << "Translating formula done in "
                      << trans_time << " seconds\n";
          utils::vout << "Automaton has " << aut->num_states ()
                      << " states and " << aut->num_sets () << " colors\n";
        }

        ////////////////////////////////////////////////////////////////////////
        // Preprocess automaton

        if (want_time) {
          sw.start();
          sw_nospot.start ();
        }

        auto aut_preprocessors_maker = AUT_PREPROCESSOR ();
        (aut_preprocessors_maker.make (aut, all_inputs, all_outputs, opt_K)) ();

        if (want_time) {
          merge_time = sw.stop();
          utils::vout << "Preprocessing done in " << merge_time
                      << " seconds\nDPA has " << aut->num_states()
                      << " states\n";
        }
        verb_do (2, spot::print_hoa(utils::vout, aut, nullptr));

        ////////////////////////////////////////////////////////////////////////
        // Boolean states

        if (want_time)
          sw.start ();

        auto boolean_states_maker = BOOLEAN_STATES ();
        vectors::bool_threshold = (boolean_states_maker.make (aut, opt_K)) ();

        if (want_time) {
          boolean_states_time = sw.stop ();
          utils::vout << "Computation of boolean states in " << boolean_states_time
            /*     */ << "seconds , found " << vectors::bool_threshold << " boolean states.\n";
        }


        // Special case: only boolean states, so... no useful accepting state.
        if (vectors::bool_threshold == 0) {
          return true;
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
          verb_do (1, vout << "Warning: bitsets not large enough, using regular vectors for some Boolean states.\n"
                   /*   */ << "\tTotal # of Boolean-for-bitset states: " << nbitsetbools
                   /*   */ << ", max: " << max_bools_in_bitsets << std::endl);
          nbitsetbools = max_bools_in_bitsets;
        }

        constexpr auto STATIC_ARRAY_CAP_MAX =
          vectors::traits<vectors::ARRAY_IMPL, VECTOR_ELT_T>::capacity_for (STATIC_ARRAY_MAX);

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
                    (aut, opt_Kmin, opt_K, opt_Kinc, all_inputs, all_outputs);
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
                (aut, opt_Kmin, opt_K, opt_Kinc, all_inputs, all_outputs);
              realizable = skn.solve ();
            },
            UNREACHABLE,
            vectors::nbools_to_nbitsets (nbitsetbools));
        }

        if (want_time) {
          solve_time = sw.stop ();
          utils::vout << "Safety game solved in " << solve_time << " seconds, returning " << realizable << "\n";
          utils::vout << "Time disregarding Spot translation: " << sw_nospot.stop () << " seconds\n";
        }

        timer.stop ();

        return realizable;
      }

      int process_formula (spot::formula f, const char *, int) override {
        return solve_formula (f);
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

    const auto start_proc = [&] (bool real, unreal_x_t unreal_x) {
      if (fork () == 0) {
        utils::vout.set_prefix (std::string {"["}
                                + (real ?
                                   "real" :
                                   std::string {"unreal-x="} + (char) unreal_x)
                                + "] ");
        check_real = real;
        opt_unreal_x = unreal_x;
        int res = processor.run ();
        verb_do (1, vout << "returning " << res << "\n");
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
