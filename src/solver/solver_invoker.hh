#pragma once

#include "configuration.hh"
#include "solver/spot_fast_mode.hh"

#include <optional>
#include <string>
#include <vector>

// These are the valid ways of treating unrealizability.
enum UNREAL_X_T : char { UNREAL_X_FORMULA = 'f', UNREAL_X_AUTOMATON = 'a', UNREAL_X_BOTH };

bool run_ltl (std::vector<std::string> input_aps, std::vector<std::string> output_aps,
              VECTOR_ELT_T opt_k, VECTOR_ELT_T opt_kmin, VECTOR_ELT_T opt_kinc,
              std::string formula, std::optional<UNREAL_X_T> check_unreal,
              SPOT_FAST_T spot_fast, const std::optional<std::string>& synth_fname);
