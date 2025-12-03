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

#define debug_(A...) do {  std::cout << A << "\n"; } while (0)


namespace {
    /**
     * Process the specified input (-i) argument. This is a comma-separated list uncontrollable atomic propositions.
     * @param arg The comma-separated list of inputs.
     * @param result The struct that will contain the parsed and processed argument values.
     */
    void process_arg_input(const std::string& arg, arg_parse_result& result);

    /**
     * Process the specified output (-o) argument. This is a comma-separated list controllable atomic propositions.
     * @param arg The comma-separated list of outputs.
     * @param result The struct that will contain the parsed and processed argument values.
     */
    void process_arg_output(const std::string& arg, arg_parse_result& result);

    /**
     * Print the help menu for the specified program name.
     */
    void show_help(const char* program_name);
}

arg_parse_result arg_parser(int argc, char **argv) {
    arg_parse_result retval;
    int opt;

    // this goes over all provided arguments and returns the argument value.
    while ((opt = getopt(argc, argv, "hEVf:i:o:I:K:M:v!")) != -1) {
        switch (opt) {
            case 'h':
                show_help(argv[0]);
                exit(0);
            case 'E':
                retval.moore_mode = true;
                break;
            case 'V':
                std::cout << "Version: " << VERSION << '\n';
                exit(0);
            case 'f':
                retval.formula = optarg;
                break;
            case 'i':
                process_arg_input(optarg, retval);
                break;
            case 'o':
                process_arg_output(optarg, retval);
                break;
            case 'I':
                retval.opt_kinc = std::stoi(optarg);
                break;
            case 'K':
                retval.opt_kmax = std::stoi(optarg);
                break;
            case 'M':
                retval.opt_kstart = std::stoi(optarg);
                break;
            case 'v':
                retval.verbose_level++;
                break;
            case '!':
                retval.invert_exit_code = true;
                break;
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
    if (retval.opt_kstart != DEFAULT_KMIN and retval.opt_kinc == DEFAULT_KINC) {
        error(3, 0, "Error: if 'Kstart' (-M) is specified, then 'Kinc' (-I) also must be provided.");
    }

    return retval;
}



namespace {
    void show_help(const char* program_name) {
        std::cout << "Usage: " << program_name << " [OPTIONS]\n"
                  << "Verify realizability for LTL specifications.\n\n"
                  << "Allowed options:\n"
                  << "  -h                print this help\n"
                  << "  -E                (Edward) Moore mode for the controller\n"
                  << "  -V                print program version\n"
                  << "  -f STRING         process the formula STRING\n"
                  << "  -i PROPS          comma-separated list of uncontrollable (a.k.a. input) atomic propositions\n"
                  << "  -o PROPS          comma-separated list of controllable (a.k.a. output) atomic propositions\n"
                  << "  -I VAL            increment value for K, used when Kmin < Kmax\n"
                  << "  -K VAL            final value of K, or unique value if Kmin is not specified\n"
                  << "  -M VAL            starting value of K; Kinc MUST be set when using this option\n"
                  << "  -v                verbose mode, can be repeated for more verbosity\n"
                  << "  -!                invert the exit code: " << EXIT_CODE_REAL << " for UNKNOWN and " << EXIT_CODE_UNKNOWN << " for REALIZABLE\n"
                  << "Exit status:\n"
                  << "\t" << EXIT_CODE_REAL << "   if the input problem is realizable\n"
                  << "\t" << EXIT_CODE_UNKNOWN << "   if this could not be decided\n"
                  << "\t" << EXIT_CODE_ERROR << "   if any error has been reported" << '\n'
                  << "Version: " << VERSION << '\n';
    }

    void process_arg_input(const std::string& arg, arg_parse_result& result) {
        // very simple, we just split on comma "," and add every thing to the vector of inputs
        std::istringstream props (arg);
        std::string prop;
        while (std::getline (props, prop, ',')) {
            prop.erase (std::remove_if (prop.begin (), prop.end (), isspace), prop.end ());
            result.inputs.push_back (prop);
        }
    }

    void process_arg_output(const std::string& arg, arg_parse_result& result) {
        // very simple, we just split on comma "," and add every thing to the vector of outputs
        std::istringstream props (arg);
        std::string prop;
        while (std::getline (props, prop, ',')) {
            prop.erase (std::remove_if (prop.begin (), prop.end (), isspace), prop.end ());
            result.outputs.push_back (prop);
        }
    }
}

