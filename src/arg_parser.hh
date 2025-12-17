#pragma once

#include "configuration.hh"

#include <string>
#include <vector>

enum unreal_x_t {
  UNREAL_X_FORMULA,
  UNREAL_X_AUTOMATON,
  UNREAL_X_BOTH
};

/**
 * Struct that will hold the parsed argument values.
 */
struct arg_parse_result {
    std::string formula;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    unsigned int opt_kstart = DEFAULT_KMIN;
    unsigned int opt_kmax = DEFAULT_K;
    unsigned int opt_kinc = DEFAULT_KINC;
    unreal_x_t opt_unreal_x = DEFAULT_UNREAL_X;
    unsigned int verbose_level = 0;
    bool invert_exit_code = false;
};

/**
 * Function that parses the arguments into an ArgParseResult object.
 *
 * @param argc The value of 'argc', as passed to main().
 * @param argv The value of 'argv', as passed to main().
 *
 * @return ArgParseResult object with the argument values.
 */
arg_parse_result arg_parser (int argc, char** argv);
