#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <unistd.h>
#include "configuration.hh"

/**
 * Struct that will hold the parsed argument values.
 */
struct ArgParseResult {
  std::string formula;
  bool is_file = false;
  bool lbt_input = false;
  bool lenient = false;
  bool moore_mode = false;
  std::vector<int> init_state = {};
  std::vector<std::string> inputs = {};
  std::vector<std::string> outputs = {};
  std::string synth_fname = "";
  unsigned int opt_Kstart = DEFAULT_KMIN;
  unsigned int opt_Kmax = DEFAULT_K;
  unsigned int opt_Kinc = DEFAULT_KINC;
  unsigned int verbose_level = 0;
  std::string extra_opts = "";
};

/**
 * Function that parses the arguments into an ArgParseResult object.
 *
 * @param argc The value of 'argc', as passed to main().
 * @param argv The value of 'argv', as passed to main().
 *
 * @return ArgParseResult object with the argument values.
 */
ArgParseResult arg_parser(int argc, char **argv);
