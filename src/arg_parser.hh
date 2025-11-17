#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <unistd.h>
#include "configuration.hh"

/**
 * Struct that will hold the parsed argument values.
 */
struct arg_parse_result {
  std::string formula;
  bool moore_mode = false;
  std::vector<int> init_state;
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  std::string synth_fname;
  unsigned int opt_kstart = DEFAULT_KMIN;
  unsigned int opt_kmax = DEFAULT_K;
  unsigned int opt_kinc = DEFAULT_KINC;
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
arg_parse_result arg_parser(int argc, char **argv);
