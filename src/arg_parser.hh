#pragma once

#include "configuration.hh"
#include "error_msg.hh"
#include "portfolio_arm.hh"
#include "solver/game_backend.hh"
#include "solver/solver_invoker.hh"
#if ACACIA_ENABLE_TLSF_FRONTEND
# include "tlsf_frontend.hh"

# include <tlsf/pipeline.h>
#endif
#include "utils/verbose.hh"
#include "version.hh"
#include <string_view>

#include <algorithm>
#include <cctype>
#include <errno.h>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <limits>
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
    std::optional<std::vector<portfolio_arm>> arms = std::nullopt;
    TRANSLATION_PREF_T primary_translation_pref = ACACIA_TRANSLATION_PREF;
    // Compiling spot-guarded never changes the default; select it explicitly.
#if ACACIA_FORWARD_SAFETY_SOLVER
    acacia::game_backend real_backend = acacia::game_backend::forward;
    acacia::game_backend unreal_backend = acacia::game_backend::forward;
#else
    acacia::game_backend real_backend = acacia::game_backend::backward;
    acacia::game_backend unreal_backend = acacia::game_backend::backward;
#endif
    unsigned verbose_level = 0;
    acacia::automaton_provider real_provider = acacia::automaton_provider::frozen_graph;
    acacia::automaton_provider unreal_provider = acacia::automaton_provider::frozen_graph;
    acacia::candidate_mode candidate = ACACIA_DEFAULT_CANDIDATE_MODE;
    SPOT_FAST_T spot_fast = DEFAULT_SPOT_FAST;
    std::optional<std::string> synth_fname = std::nullopt;
    specification_metadata metadata;
    bool formula_specified = false;
    bool inputs_specified = false;
    bool outputs_specified = false;
    bool tlsf_specified = false;
    std::string tlsf_source;
    std::string tlsf_sha256;
    bool legacy_available = true;
    double native_structure_guard_scale = 1;
};

arg_parse_result arg_parser (int argc, char** argv);
