#pragma once

#include "configuration.hh"
#include "solver/solver_invoker.hh"

#include <string>
#include <vector>

/**
 * Struct that will hold the parsed argument values.
 */
struct arg_parse_result {
    std::string formula;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    unsigned int opt_kmin = DEFAULT_KMIN;
    unsigned int opt_k = DEFAULT_K;
    unsigned int opt_kinc = DEFAULT_KINC;
    unreal_x_t opt_unreal_x = DEFAULT_UNREAL_X;
    unsigned int verbose_level = 0;
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
