#pragma once

#include "configuration.hh"
#include "error_msg.hh"
#include "solver/solver_invoker.hh"
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
    std::optional<UNREAL_X_T> opt_unreal_x = std::make_optional<UNREAL_X_T> (DEFAULT_UNREAL_X);
    unsigned verbose_level = 0;
    bool check_real = true;
    SPOT_FAST_T spot_fast = DEFAULT_SPOT_FAST;
    std::optional<std::string> synth_fname = std::nullopt;
    std::optional<int> mem_limit = std::nullopt;
};

/**
 * Process the specified input (-i) argument. This is a comma-separated list uncontrollable
 * atomic propositions.
 * @param arg The comma-separated list of inputs.
 * @param result The struct that will contain the parsed and processed argument values.
 */
void process_arg_input (const std::string& arg, arg_parse_result& result) {
  // very simple, we just split on comma "," and add every thing to the vector of inputs
  std::istringstream props (arg);
  std::string prop;
  while (std::getline (props, prop, ',')) {
    prop.erase (std::remove_if (prop.begin (), prop.end (), isspace), prop.end ());
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
  // very simple, we just split on comma "," and add every thing to the vector of outputs
  std::istringstream props (arg);
  std::string prop;
  while (std::getline (props, prop, ',')) {
    prop.erase (std::remove_if (prop.begin (), prop.end (), isspace), prop.end ());
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
      << "  -u VAL            use VAL from [automaton|formula|both] to check unrealizability\n"
      << "                    by default, unrealizability is checked with "
#if DEFAULT_UNREAL_X == UNREAL_X_AUTOMATON
      << "VAL = automaton"
#elif DEFAULT_UNREAL_X == UNREAL_X_FORMULA
      << "VAL = formula"
#else
      << "VAL = both"
#endif
      << std::endl
      << "  -l VAL            set the virtual memory limit to VAL GiBs\n"
      << "  --spot-fast VAL   use Spot NBA fast path from [off|det|det-and-gfg]\n"
      << "  -r                do NOT check for unrealizability\n"
      << "  -U                do NOT check for realizability\n"
      << "  -v                verbose mode, can be repeated for more verbosity\n"
      << "Exit status:\n"
      << "\t" << (int)EXIT_CODE_REAL << "   if the input problem is realizable\n"
      << "\t" << (int)EXIT_CODE_UNREAL << "   if it is unrealizable\n"
      << "\t" << (int)EXIT_CODE_UNKNOWN << "   if this could not be decided\n"
      << "\t" << (int)EXIT_CODE_ERROR << "   if any error has been reported" << '\n'
      << "Version: " << VERSION << '\n';
}

bool case_insensitive_char_equals (char a, char b) {
  return std::tolower (static_cast<unsigned char> (a)) ==
         std::tolower (static_cast<unsigned char> (b));
}

bool case_insensitive_equals (std::string_view lhs, std::string_view rhs) {
  return std::ranges::equal (lhs, rhs, case_insensitive_char_equals);
}

void process_arg_unreal (const std::string& arg, arg_parse_result& result) {
  if (case_insensitive_equals (arg, "automaton"))
    result.opt_unreal_x = std::make_optional<UNREAL_X_T> (UNREAL_X_AUTOMATON);
  else if (case_insensitive_equals (arg, "formula"))
    result.opt_unreal_x = std::make_optional<UNREAL_X_T> (UNREAL_X_FORMULA);
  else if (case_insensitive_equals (arg, "both"))
    result.opt_unreal_x = std::make_optional<UNREAL_X_T> (UNREAL_X_BOTH);
  else
    error (EXIT_CODE_ERROR, "Error: unexpected unrealizble option %s\n", arg.c_str ());
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
  while ((opt = getopt_long (argc, argv, "hUrVvf:F:i:o:I:K:M:u:s:l:", long_options,
                             nullptr)) != -1) {
    switch (opt) {
      case 'h': show_help (argv[0]); exit (EXIT_CODE_UNKNOWN);
      case 'V': std::cout << "Version: " << VERSION << '\n'; exit (EXIT_CODE_UNKNOWN);
      case 'f': retval.formula = optarg; break;
      case 'r': retval.opt_unreal_x = std::nullopt; break;
      case 'U': retval.check_real = false; break;
      case 'F': process_formula_file (optarg, retval); break;
      case 'i': process_arg_input (optarg, retval); break;
      case 'o': process_arg_output (optarg, retval); break;
      case 'I': retval.opt_kinc = std::stoi (optarg); break;
      case 'K': retval.opt_k = std::stoi (optarg); break;
      case 'M': sgn_kmin = std::make_optional<int> (std::stoi (optarg)); break;
      case 'v': retval.verbose_level++; break;
      case 'u': process_arg_unreal (optarg, retval); break;
      case 's': retval.synth_fname = optarg; break;
      case 'l': retval.mem_limit = std::stoi (optarg); break;
      case OPT_SPOT_FAST: process_arg_spot_fast (optarg, retval); break;
      default: show_help (argv[0]); exit (EXIT_CODE_ERROR);
    }
  }

  if (retval.formula.empty ())
    error (EXIT_CODE_ERROR, "Error: a formula must be specified (-f or -F).\n");
  if (retval.inputs.empty ())
    error (EXIT_CODE_ERROR, "Error: inputs must be specified (-i).\n");
  if (retval.outputs.empty ())
    error (EXIT_CODE_ERROR, "Error: outputs must be specified (-o).\n");

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
