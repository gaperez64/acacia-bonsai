#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include "arg_parser.hh"
#include "error_msg.hh"

#define debug_(A...) do { if (verbose_level > 0) { std::cout << A << std::endl; } } while (0)

static int verbose_level = 0;

static void process_arg_init_(const std::string& arg, ArgParseResult& result);
static void process_arg_input_(const std::string& arg, ArgParseResult& result);
static void process_arg_output_(const std::string& arg, ArgParseResult& result);
static void process_arg_synth_(const std::string& arg, ArgParseResult& result);
static void show_help(const char* program_name);

ArgParseResult arg_parser(int argc, char **argv) {
    debug_("[DEBUG] Parsing arguments.");
    ArgParseResult retval;
    int opt;

    // NOTE: Options that only had long names in the original boost::program_options
    // implementation (e.g., --lenient, --lbt-input, --moore) have been removed
    // to comply with the requirement of using getopt with short options only.
    while ((opt = getopt(argc, argv, "hVf:F:0:i:o:S:I:K:M:vx:")) != -1) {
        switch (opt) {
            case 'h':
                show_help(argv[0]);
                exit(0);
            case 'V':
                // Assuming a version string is defined elsewhere
                // std::cout << "Version: " << VERSION << std::endl;
                exit(0);
            case 'f':
                retval.formula = optarg;
                retval.is_file = false;
                break;
            case 'F':
                retval.formula = optarg;
                retval.is_file = true;
                break;
            case '0':
                process_arg_init_(optarg, retval);
                break;
            case 'i':
                process_arg_input_(optarg, retval);
                break;
            case 'o':
                process_arg_output_(optarg, retval);
                break;
            case 'S':
                process_arg_synth_(optarg, retval);
                break;
            case 'I':
                retval.opt_Kinc = std::stoi(optarg);
                break;
            case 'K':
                retval.opt_Kmax = std::stoi(optarg);
                break;
            case 'M':
                retval.opt_Kstart = std::stoi(optarg);
                break;
            case 'v':
                retval.verbose_level++;
                verbose_level++;
                break;
            case '?':
                // getopt() prints an error message automatically.
                show_help(argv[0]);
                exit(1);
            default:
                show_help(argv[0]);
                exit(1);
        }
    }

    if (retval.formula.empty()) {
        error(3, 0, "Error: a formula or file must be specified (-f).");
    }
    if (retval.inputs.empty()) {
        error(3, 0, "Error: inputs must be specified (-i).");
    }
    if (retval.outputs.empty()) {
        error(3, 0, "Error: outputs must be specified (-o).");
    }
    if (retval.opt_Kstart != DEFAULT_KMIN && retval.opt_Kinc == DEFAULT_KINC) {
        error(3, 0, "Error: if 'Kstart' (-M) is specified, then 'Kinc' (-I) also must be provided.");
    }

    debug_("[DEBUG] Finished parsing arguments.");
    return retval;
}

static void show_help(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n"
              << "Verify realizability for LTL specifications.\n\n"
              << "Allowed options:\n"
              << "  -h                print this help\n"
              << "  -V                print program version\n"
              << "  -f STRING         process the formula STRING\n"
              << "  -0 STATE          comma-separated state vector to use as initial state\n"
              << "  -i PROPS          comma-separated list of uncontrollable (a.k.a. input) atomic propositions\n"
              << "  -o PROPS          comma-separated list of controllable (a.k.a. output) atomic propositions\n"
              << "  -S FILENAME       enable synthesis, pass .aag filename, or - to print gates\n"
              << "  -I VAL            increment value for K, used when Kmin < Kmax\n"
              << "  -K VAL            final value of K, or unique value if Kmin is not specified\n"
              << "  -M VAL            starting value of K; Kinc MUST be set when using this option\n"
              << "  -v                verbose mode, can be repeated for more verbosity\n"
              << "Exit status:\n"
              << "\t0   if the input problem is realizable\n"
              << "\t1   if the input problem is not realizable\n"
              << "\t2   if this could not be decided\n"
              << "\t3   if any error has been reported" << std::endl;
}

static void process_arg_init_(const std::string& arg, ArgParseResult& result) {
  std::istringstream state (arg);
  std::string val;

  while (std::getline (state, val, ',')) {
    try {
      result.init_state.push_back (std::stoi (val));
    } catch (std::invalid_argument const& ex) {
      std::cout << "ERROR while parsing initial state: "
                << ex.what() << std::endl;
      abort ();
    } catch (std::out_of_range const& ex) {
      std:: cout << "ERROR while parsing initial state: "
                 << ex.what () << std::endl;
      abort ();
    }
  }
}

static void process_arg_input_(const std::string& arg, ArgParseResult& result) {
  std::istringstream props (arg);
  std::string prop;
  while (std::getline (props, prop, ',')) {
    prop.erase (std::remove_if (prop.begin (), prop.end (), isspace), prop.end ());
    result.inputs.push_back (prop);
  }
}

static void process_arg_output_(const std::string& arg, ArgParseResult& result) {
  std::istringstream props (arg);
  std::string prop;
  while (std::getline (props, prop, ',')) {
    prop.erase (std::remove_if (prop.begin (), prop.end (), isspace), prop.end ());
    result.outputs.push_back (prop);
  }
}

static void process_arg_synth_(const std::string& arg, ArgParseResult& result) {
    result.synth_fname = arg;
}
