#include "arg_parser.hh"
#include "error_msg.hh"

#include <string_view>
#include <algorithm>
#include <cctype>
#include <errno.h>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <stdio.h>
#include <string>
#include <unistd.h>
#include <vector>

#define debug_(A...)        \
  do {                      \
    std::cout << A << "\n"; \
  } while (0)

namespace {
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
        << "  -V                print program version\n"
        << "  -f STRING         process the formula STRING\n"
        << "  -i PROPS          comma-separated list of uncontrollable (a.k.a. input) "
           "atomic propositions\n"
        << "  -o PROPS          comma-separated list of controllable (a.k.a. output) atomic "
           "propositions\n"
        << "  -I VAL            increment value for K, used when M < K\n"
        << "  -K VAL            final value of K, or unique value if M is not specified\n"
        << "  -M VAL            starting value of K; -I MUST be set when using this option\n"
        << "  -u VAL            check unrealizability; VAL should be [automaton|formula|both]\n"
        << "  -v                verbose mode, can be repeated for more verbosity\n"
        << "Exit status:\n"
        << "\t" << EXIT_CODE_REAL << "   if the input problem is realizable\n"
        << "\t" << EXIT_CODE_UNKNOWN << "   if this could not be decided\n"
        << "\t" << EXIT_CODE_ERROR << "   if any error has been reported" << '\n'
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
      result.opt_unreal_x = std::make_optional<unreal_x_t>(UNREAL_X_AUTOMATON);
    else if (case_insensitive_equals (arg, "formula"))
      result.opt_unreal_x = std::make_optional<unreal_x_t>(UNREAL_X_FORMULA);
    else if (case_insensitive_equals (arg, "both"))
      result.opt_unreal_x = std::make_optional<unreal_x_t>(UNREAL_X_BOTH);
    else
      error (EXIT_CODE_ERROR, "Error: unexpected unrealizble option %s", arg);
  }

}

arg_parse_result arg_parser (int argc, char** argv) {
  arg_parse_result retval;
  int opt;

  // this goes over all provided arguments and returns the argument value.
  while ((opt = getopt (argc, argv, "hVf:i:o:I:K:M:u:v")) != -1) {
    switch (opt) {
      case 'h': show_help (argv[0]); exit (0);
      case 'V': std::cout << "Version: " << VERSION << '\n'; exit (0);
      case 'f': retval.formula = optarg; break;
      case 'i': process_arg_input (optarg, retval); break;
      case 'o': process_arg_output (optarg, retval); break;
      case 'I': retval.opt_kinc = std::stoi (optarg); break;
      case 'K': retval.opt_k = std::stoi (optarg); break;
      case 'M': retval.opt_kmin = std::stoi (optarg); break;
      case 'v': retval.verbose_level++; break;
      case 'u': process_arg_unreal (optarg, retval); break;
      default: show_help (argv[0]); exit (1);
    }
  }

  if (retval.formula.empty ())
    error (EXIT_CODE_ERROR, "Error: a formula must be specified (-f).");
  if (retval.inputs.empty ())
    error (EXIT_CODE_ERROR, "Error: inputs must be specified (-i).");
  if (retval.outputs.empty ())
    error (EXIT_CODE_ERROR, "Error: outputs must be specified (-o).");
  if (retval.opt_kmin != DEFAULT_KMIN and retval.opt_kinc == DEFAULT_KINC)
    error (EXIT_CODE_ERROR,
           "Error: if 'Kstart' (-M) is specified, then 'Kinc' (-I) also must be provided.");

  return retval;
}

namespace {}
