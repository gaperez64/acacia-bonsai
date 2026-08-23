#pragma once

#include "configuration.hh"
#include "error_msg.hh"
#include "solver/solver_invoker.hh"
#if ACACIA_ENABLE_TLSF_FRONTEND
# include "tlsf_frontend.hh"
#endif
#include "utils/verbose.hh"
#include "version.hh"
#include <string_view>

#include <algorithm>
#include <cctype>
#include <errno.h>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <stdio.h>
#include <string>
#include <unistd.h>
#include <vector>

/**
 * Struct that will hold the parsed argument values.
 */
struct arg_parse_result {
    std::string formula;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    VECTOR_ELT_T opt_kmin = DEFAULT_KMIN;
    VECTOR_ELT_T opt_k = DEFAULT_K;
    VECTOR_ELT_T opt_kinc = DEFAULT_KINC;
    std::optional<std::vector<TRANSLATION_PREF_T>> real_strategies = std::nullopt;
    std::optional<std::vector<UNREAL_X_T>> unreal_strategies = std::nullopt;
    TRANSLATION_PREF_T primary_translation_pref = ACACIA_TRANSLATION_PREF;
    unsigned verbose_level = 0;
    SPOT_FAST_T spot_fast = DEFAULT_SPOT_FAST;
    std::optional<std::string> synth_fname = std::nullopt;
    specification_metadata metadata;
    bool formula_specified = false;
    bool inputs_specified = false;
    bool outputs_specified = false;
    bool tlsf_specified = false;
};

/**
 * Process the specified input (-i) argument. This is a comma-separated list uncontrollable
 * atomic propositions.
 * @param arg The comma-separated list of inputs.
 * @param result The struct that will contain the parsed and processed argument values.
 */
void process_arg_input (const std::string& arg, arg_parse_result& result) {
  result.inputs_specified = true;
  // very simple, we just split on comma "," and add every thing to the vector of inputs
  std::istringstream props (arg);
  std::string prop;
  while (std::getline (props, prop, ',')) {
    prop.erase (std::remove_if (prop.begin (), prop.end (), isspace), prop.end ());
    if (not prop.empty ())
      result.inputs.push_back (prop);
  }
}

/**
 * Process the specified output (-o) argument. This is a comma-separated list controllable atomic
 * propositions.
 * @param arg The comma-separated list of outputs.
 * @param result The struct that will contain the parsed and processed argument values.
 */
void process_arg_output (const std::string& arg, arg_parse_result& result) {
  result.outputs_specified = true;
  // very simple, we just split on comma "," and add every thing to the vector of outputs
  std::istringstream props (arg);
  std::string prop;
  while (std::getline (props, prop, ',')) {
    prop.erase (std::remove_if (prop.begin (), prop.end (), isspace), prop.end ());
    if (not prop.empty ())
      result.outputs.push_back (prop);
  }
}

/**
 * Print the help menu for the specified program name.
 */
void show_help (const char* program_name) {
  std::cout
      << "Usage: " << program_name << " [OPTIONS]\n"
      << "Check realizability for LTL specifications.\n\n"
      << "Allowed options:\n"
      << "  -h                print this help message\n"
      << "  -s FILE           synthesize controller and store in FILE\n"
      << "  -V                print program version\n"
      << "  -f STRING         process the formula STRING\n"
      << "  -F VAL            process formula in file VAL\n"
#if ACACIA_ENABLE_TLSF_FRONTEND
      << "  -T, --tlsf FILE   process a TLSF specification natively\n"
#endif
      << "  -i PROPS          comma-separated list of uncontrollable (a.k.a. input) "
         "atomic propositions\n"
      << "  -o PROPS          comma-separated list of controllable (a.k.a. output) atomic "
         "propositions\n"
      << "  -K VAL            final value of K, or unique value if M is not specified\n"
      << "  -M VAL            starting value of K\n"
      << "  -I VAL            increment value for K, used when M < K\n"
      << "  -r LIST           check realizability using comma-separated [small|any]\n"
      << "  -u LIST           check unrealizability using comma-separated\n"
      << "                    [automaton|formula]; both is an alias for the pair\n"
      << "                    mentioning a check selects it; mentioning neither runs\n"
      << "                    both checks with their build defaults\n"
      << "                    the head of -r is the primary translator preference used\n"
      << "                    by unrealizability checks; -s truncates -r to its head\n"
      << "                    migration: legacy bare -r and -U now fail; bare -u LIST\n"
      << "                    now selects unrealizability only\n"
      << "  --spot-fast VAL   use Spot NBA fast path from [off|det|det-and-gfg]\n"
      << "  -v                verbose mode, can be repeated for more verbosity\n"
      << "Exit status:\n"
      << "\t" << (int)EXIT_CODE_REAL << "   if the input problem is realizable\n"
      << "\t" << (int)EXIT_CODE_UNREAL << "   if it is unrealizable\n"
      << "\t" << (int)EXIT_CODE_UNKNOWN << "   if this could not be decided\n"
      << "\t" << (int)EXIT_CODE_ERROR << "   if any error has been reported" << '\n';
  print_version (std::cout);
}

bool case_insensitive_char_equals (char a, char b) {
  return std::tolower (static_cast<unsigned char> (a)) ==
         std::tolower (static_cast<unsigned char> (b));
}

bool case_insensitive_equals (std::string_view lhs, std::string_view rhs) {
  return std::ranges::equal (lhs, rhs, case_insensitive_char_equals);
}

template <typename Strategy, typename AddValue>
std::vector<Strategy> process_strategy_list (const std::string& arg,
                                             const char* option,
                                             AddValue add_value) {
  std::vector<Strategy> strategies;
  std::istringstream values (arg);
  std::string value;
  while (std::getline (values, value, ',')) {
    const auto first = std::find_if_not (
        value.begin (), value.end (), [] (unsigned char c) { return std::isspace (c); });
    const auto last = std::find_if_not (
                          value.rbegin (), value.rend (),
                          [] (unsigned char c) { return std::isspace (c); })
                          .base ();
    if (first >= last)
      error (EXIT_CODE_ERROR, "Error: empty strategy in -%s list.\n", option);
    value = std::string (first, last);
    add_value (value, strategies);
  }
  if (strategies.empty () or arg.back () == ',')
    error (EXIT_CODE_ERROR, "Error: empty strategy in -%s list.\n", option);
  return strategies;
}

template <typename Strategy>
void append_strategy (Strategy strategy, std::vector<Strategy>& strategies,
                      const char* option, const std::string& name) {
  if (std::ranges::find (strategies, strategy) != strategies.end ())
    error (EXIT_CODE_ERROR, "Error: duplicate strategy %s in -%s list.\n",
           name.c_str (), option);
  strategies.push_back (strategy);
}

void process_arg_real (const std::string& arg, arg_parse_result& result) {
  result.real_strategies = process_strategy_list<TRANSLATION_PREF_T> (
      arg, "r", [] (const std::string& value, auto& strategies) {
        if (case_insensitive_equals (value, "small"))
          append_strategy<TRANSLATION_PREF_T> (spot::postprocessor::Small, strategies,
                                               "r", value);
        else if (case_insensitive_equals (value, "any"))
          append_strategy<TRANSLATION_PREF_T> (spot::postprocessor::Any, strategies,
                                               "r", value);
        else
          error (EXIT_CODE_ERROR, "Error: unexpected realizability strategy %s.\n",
                 value.c_str ());
      });
  result.primary_translation_pref = result.real_strategies->front ();
}

void process_arg_unreal (const std::string& arg, arg_parse_result& result) {
  result.unreal_strategies = process_strategy_list<UNREAL_X_T> (
      arg, "u", [] (const std::string& value, auto& strategies) {
        if (case_insensitive_equals (value, "automaton"))
          append_strategy (UNREAL_X_AUTOMATON, strategies, "u", value);
        else if (case_insensitive_equals (value, "formula"))
          append_strategy (UNREAL_X_FORMULA, strategies, "u", value);
        else if (case_insensitive_equals (value, "both")) {
          append_strategy (UNREAL_X_FORMULA, strategies, "u", value);
          append_strategy (UNREAL_X_AUTOMATON, strategies, "u", value);
        }
        else
          error (EXIT_CODE_ERROR, "Error: unexpected unrealizability strategy %s.\n",
                 value.c_str ());
      });
}

std::vector<TRANSLATION_PREF_T> default_real_strategies () {
  return {ACACIA_TRANSLATION_PREFS};
}

std::vector<UNREAL_X_T> default_unreal_strategies () {
  if (DEFAULT_UNREAL_X == UNREAL_X_AUTOMATON)
    return {UNREAL_X_AUTOMATON};
  if (DEFAULT_UNREAL_X == UNREAL_X_FORMULA)
    return {UNREAL_X_FORMULA};
  return {UNREAL_X_FORMULA, UNREAL_X_AUTOMATON};
}

void process_arg_spot_fast (const std::string& arg, arg_parse_result& result) {
  if (case_insensitive_equals (arg, "off"))
    result.spot_fast = SPOT_FAST_OFF;
  else if (case_insensitive_equals (arg, "det"))
    result.spot_fast = SPOT_FAST_DET;
  else if (case_insensitive_equals (arg, "det-and-gfg") or
           case_insensitive_equals (arg, "det_and_gfg") or
           case_insensitive_equals (arg, "gfg-decision") or
           case_insensitive_equals (arg, "gfg"))
    result.spot_fast = SPOT_FAST_DET_AND_GFG;
  else
    error (EXIT_CODE_ERROR, "Error: unexpected Spot fast-path option %s\n", arg.c_str ());
}

void process_formula_file (const std::string& arg, arg_parse_result& result) {
  std::ifstream file (arg.c_str ());
  if (not file)
    error (EXIT_CODE_ERROR, "Error: unable to open file %s\n", arg.c_str ());
  std::stringstream buffer;
  buffer << file.rdbuf ();
  result.formula = buffer.str ();
}

#if ACACIA_ENABLE_TLSF_FRONTEND
void process_tlsf_file (const std::string& arg, arg_parse_result& result) {
  try {
    auto spec = acacia::tlsf_frontend::load (arg);
    result.formula = std::move (spec.formula);
    result.inputs = std::move (spec.inputs);
    result.outputs = std::move (spec.outputs);
    result.metadata = std::move (spec.metadata);
    result.formula_specified = true;
    result.inputs_specified = true;
    result.outputs_specified = true;
    result.tlsf_specified = true;
  }
  catch (const std::exception& exception) {
    error (EXIT_CODE_ERROR, "Error: %s\n", exception.what ());
  }
}
#endif

/**
 * Function that parses the arguments into an ArgParseResult object.
 *
 * @param argc The value of 'argc', as passed to main().
 * @param argv The value of 'argv', as passed to main().
 *
 * @return ArgParseResult object with the argument values.
 */
arg_parse_result arg_parser (int argc, char** argv) {
  arg_parse_result retval;
  int opt;
  std::optional<int> sgn_kmin = std::nullopt;
  static constexpr int OPT_SPOT_FAST = 1000;
  static option long_options[] = {
      {"spot-fast", required_argument, nullptr, OPT_SPOT_FAST},
#if ACACIA_ENABLE_TLSF_FRONTEND
      {"tlsf", required_argument, nullptr, 'T'},
#endif
      {nullptr, 0, nullptr, 0},
  };

#if ACACIA_ENABLE_TLSF_FRONTEND
  static constexpr const char* short_options = "hr:Vvf:F:T:i:o:I:K:M:u:s:";
#else
  static constexpr const char* short_options = "hr:Vvf:F:i:o:I:K:M:u:s:";
#endif

  // this goes over all provided arguments and returns the argument value.
  while ((opt = getopt_long (argc, argv, short_options, long_options, nullptr)) != -1) {
    switch (opt) {
      case 'h': show_help (argv[0]); exit (EXIT_CODE_UNKNOWN);
      case 'V': print_version (std::cout); exit (EXIT_CODE_UNKNOWN);
      case 'f':
        if (retval.tlsf_specified)
          error (EXIT_CODE_ERROR, "Error: -f/-F and -T/--tlsf are mutually exclusive.\n");
        retval.formula = optarg;
        retval.formula_specified = true;
        break;
      case 'r': process_arg_real (optarg, retval); break;
      case 'F':
        if (retval.tlsf_specified)
          error (EXIT_CODE_ERROR, "Error: -f/-F and -T/--tlsf are mutually exclusive.\n");
        process_formula_file (optarg, retval);
        retval.formula_specified = true;
        break;
#if ACACIA_ENABLE_TLSF_FRONTEND
      case 'T':
        if (retval.formula_specified or retval.inputs_specified or retval.outputs_specified)
          error (EXIT_CODE_ERROR,
                 "Error: -T/--tlsf cannot be combined with -f/-F, -i, or -o.\n");
        process_tlsf_file (optarg, retval);
        break;
#endif
      case 'i':
        if (retval.tlsf_specified)
          error (EXIT_CODE_ERROR, "Error: -i/-o and -T/--tlsf are mutually exclusive.\n");
        process_arg_input (optarg, retval);
        break;
      case 'o':
        if (retval.tlsf_specified)
          error (EXIT_CODE_ERROR, "Error: -i/-o and -T/--tlsf are mutually exclusive.\n");
        process_arg_output (optarg, retval);
        break;
      case 'I': {
        const int value = std::stoi (optarg);
        const int min_value =
            std::max (0, static_cast<int> (std::numeric_limits<VECTOR_ELT_T>::min ()));
        const int max_value = static_cast<int> (std::numeric_limits<VECTOR_ELT_T>::max ());
        if (value < min_value or value > max_value)
          error (EXIT_CODE_ERROR,
                 "Error: -I value %d is out of range; must be between %d and %d.\n", value,
                 min_value, max_value);
        retval.opt_kinc = value;
        break;
      }
      case 'K': {
        const int value = std::stoi (optarg);
        const int min_value =
            std::max (1, static_cast<int> (std::numeric_limits<VECTOR_ELT_T>::min ()));
        const int max_value = static_cast<int> (std::numeric_limits<VECTOR_ELT_T>::max ());
        if (value < min_value or value > max_value)
          error (EXIT_CODE_ERROR,
                 "Error: -K value %d is out of range; must be between %d and %d.\n", value,
                 min_value, max_value);
        retval.opt_k = value;
        break;
      }
      case 'M': {
        const int value = std::stoi (optarg);
        const int min_value =
            std::max (1, static_cast<int> (std::numeric_limits<VECTOR_ELT_T>::min ()));
        const int max_value = static_cast<int> (std::numeric_limits<VECTOR_ELT_T>::max ());
        if (value < min_value or value > max_value)
          error (EXIT_CODE_ERROR,
                 "Error: -M value %d is out of range; must be between %d and %d.\n", value,
                 min_value, max_value);
        sgn_kmin = std::make_optional<int> (value);
        break;
      }
      case 'v': retval.verbose_level++; break;
      case 'u': process_arg_unreal (optarg, retval); break;
      case 's': retval.synth_fname = optarg; break;
      case OPT_SPOT_FAST: process_arg_spot_fast (optarg, retval); break;
      default: show_help (argv[0]); exit (EXIT_CODE_ERROR);
    }
  }

  if (retval.formula.empty ())
#if ACACIA_ENABLE_TLSF_FRONTEND
    error (EXIT_CODE_ERROR,
           "Error: a formula or TLSF specification must be specified (-f, -F, or -T).\n");
#else
    error (EXIT_CODE_ERROR, "Error: a formula must be specified (-f or -F).\n");
#endif
  if (not retval.inputs_specified)
    error (EXIT_CODE_ERROR, "Error: inputs must be specified (-i).\n");
  if (not retval.outputs_specified)
    error (EXIT_CODE_ERROR, "Error: outputs must be specified (-o).\n");

  if (not retval.real_strategies.has_value () and
      not retval.unreal_strategies.has_value ()) {
    retval.real_strategies = default_real_strategies ();
    retval.unreal_strategies = default_unreal_strategies ();
    retval.primary_translation_pref = retval.real_strategies->front ();
  }

  if (retval.synth_fname.has_value () and retval.real_strategies.has_value () and
      retval.real_strategies->size () > 1)
    retval.real_strategies->resize (1);

  if (sgn_kmin.has_value ()) {
    if (*sgn_kmin > 0) {
      retval.opt_kmin = *sgn_kmin;
    }
    else {
      verb_do (2, vout << "Kmin is being corrected since it was not positive!\n");
      retval.opt_kmin = retval.opt_k;
    }
  }

  if (retval.opt_kmin > retval.opt_k or (retval.opt_kmin <= retval.opt_k and retval.opt_kinc == 0))
    error (EXIT_CODE_ERROR, "Error: incompatible values for K (%u), Kmin (%u), and Kinc (%u).\n",
           retval.opt_k, retval.opt_kmin, retval.opt_kinc);

  return retval;
}
