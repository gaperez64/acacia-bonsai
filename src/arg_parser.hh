#pragma once

#include "configuration.hh"
#include "error_msg.hh"
#include "portfolio_arm.hh"
#include "solver/equivariance_budget.hh"
#include "solver/game_backend.hh"
#include "solver/obligation_memory.hh"
#include "solver/oracle_layout.hh"
#include "solver/solver_invoker.hh"
#include "tlsf_frontend.hh"
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
#include <tlsf/pipeline.h>
#include <unistd.h>
#include <vector>

/**
 * Struct that will hold the parsed argument values.
 */
struct arg_parse_result {
    // One owner for the whole original objective, including frontend metadata.
    // After argument parsing, workers only borrow it; run_ltl transforms its own copy.
    acacia::tlsf_frontend::specification objective;
    VECTOR_ELT_T opt_kmin = 2;
    VECTOR_ELT_T opt_k = 99;
    VECTOR_ELT_T opt_kinc = 3;
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
    acacia::candidate_mode candidate = acacia::candidate_mode::only;
    weakening_mode weakening = weakening_mode::basic;
    acacia::unreal_witnesses::allowances weakening_allowances;
    bool obligation_explicit = false;
    acacia::obligation::memory_share obligation_memory_share =
        acacia::obligation::memory_share::equal;
    acacia::obligation::memory_share parity_memory_share =
        acacia::obligation::memory_share::headroom;
    bool dpa_allow_unproved = false;
    bool r_prepass = true;
    bool equivariance = ACACIA_ENABLE_EQUIVARIANT_SOLVER;
    std::optional<double> equivariance_budget;
    // Global 3 s optional-work allowance; explicit unbounded restores the previous unbounded
    // behaviour.
    std::optional<uint64_t> equivariance_budget_ns = 3000000000ULL;
    acacia::OracleLayout oracle_layout = acacia::OracleLayout::grouped;
    SPOT_FAST_T spot_fast = SPOT_FAST_DET;
    std::optional<std::string> synth_fname = std::nullopt;
    bool formula_specified = false;
    bool inputs_specified = false;
    bool outputs_specified = false;
    bool tlsf_specified = false;
    std::string tlsf_source;
    std::string tlsf_sha256;
    // Successful preparation of tlsf_source with the current effective-Mealy policy.
    // formula_specified also covers direct LTL and is not a preparation identity.
    bool original_tlsf_prepared = false;
    bool legacy_available = true;

    const acacia::tlsf_frontend::specification& prepare_objective (
        acacia::tlsf_frontend::specification& child_local) const {
      if (tlsf_specified && !original_tlsf_prepared) {
        child_local = acacia::tlsf_frontend::parse (tlsf_source);
        return child_local;
      }
      return objective;
    }
};

arg_parse_result arg_parser (int argc, char** argv);
