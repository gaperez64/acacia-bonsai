#pragma once

#include "configuration.hh"
#include "error_msg.hh"
#include "solver/solver_invoker.hh"
#include "utils/verbose.hh"
#include "version.hh"
#include <string_view>

#include <algorithm>
#include <cctype>
#include <errno.h>
#include <fstream>
#include <getopt.h>
#include <iostream>
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
    bool inputs_specified = false;
    bool outputs_specified = false;
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
    error (EXIT_CODE_ERROR, "Error: unable to open file %s\n", arg);
  std::stringstream buffer;
  buffer << file.rdbuf ();
  result.formula = buffer.str ();
}

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
      {nullptr, 0, nullptr, 0},
  };

  // this goes over all provided arguments and returns the argument value.
  while ((opt = getopt_long (argc, argv, "hr:Vvf:F:i:o:I:K:M:u:s:", long_options,
                             nullptr)) != -1) {
    switch (opt) {
      case 'h': show_help (argv[0]); exit (EXIT_CODE_UNKNOWN);
      case 'V': print_version (std::cout); exit (EXIT_CODE_UNKNOWN);
      case 'f': retval.formula = optarg; break;
      case 'r': process_arg_real (optarg, retval); break;
      case 'F': process_formula_file (optarg, retval); break;
      case 'i': process_arg_input (optarg, retval); break;
      case 'o': process_arg_output (optarg, retval); break;
      case 'I': retval.opt_kinc = std::stoi (optarg); break;
      case 'K': retval.opt_k = std::stoi (optarg); break;
      case 'M': sgn_kmin = std::make_optional<int> (std::stoi (optarg)); break;
      case 'v': retval.verbose_level++; break;
      case 'u': process_arg_unreal (optarg, retval); break;
      case 's': retval.synth_fname = optarg; break;
      case OPT_SPOT_FAST: process_arg_spot_fast (optarg, retval); break;
      default: show_help (argv[0]); exit (EXIT_CODE_ERROR);
    }
  }

  if (retval.formula.empty ())
    error (EXIT_CODE_ERROR, "Error: a formula must be specified (-f or -F).\n");
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
