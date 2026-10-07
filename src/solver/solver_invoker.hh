#pragma once

#include "configuration.hh"
#include "solver/game_backend.hh"
#include "solver/spot_fast_mode.hh"
#include "solver/symmetry_certificate.hh"
#include "solver/unreal_weakening_budget.hh"

#include <cstdint>
#include <optional>
#include <spot/twaalgos/postproc.hh>
#include <string>
#include <vector>

// These are the valid ways of treating unrealizability.
enum class weakening_mode { incumbent, extended, off };

enum UNREAL_X_T : char { UNREAL_X_FORMULA = 'f', UNREAL_X_AUTOMATON = 'a', UNREAL_X_BOTH };
using TRANSLATION_PREF_T = spot::postprocessor::output_pref;

struct specification_metadata {
    std::string source_format = "ltl";
    std::string tlsf_semantics = "-";
    std::string tlsf_target = "-";
    std::string tlsf_effective_target = "-";
    int tlsf_gr_level = -1;
    // Exact linked-frontend normalization, before any realizability simplification.
    std::string tlsf_normalized_objective;
    std::vector<symmetry::indexed_family_hint> tlsf_indexed_families;
};

inline const char* translation_pref_name (TRANSLATION_PREF_T preference) {
  switch (preference) {
    case spot::postprocessor::Any: return "any";
    case spot::postprocessor::Small: return "small";
    case spot::postprocessor::Deterministic: return "deterministic";
    default: return "unknown";
  }
}

bool run_ltl (std::vector<std::string> input_aps, std::vector<std::string> output_aps,
              VECTOR_ELT_T opt_k, VECTOR_ELT_T opt_kmin, VECTOR_ELT_T opt_kinc,
              std::string formula, std::optional<UNREAL_X_T> check_unreal,
              TRANSLATION_PREF_T translation_pref, SPOT_FAST_T spot_fast,
              acacia::game_backend backend, const std::optional<std::string>& synth_fname,
              const specification_metadata& metadata = {},
              acacia::automaton_provider provider = acacia::automaton_provider::frozen_graph,
              acacia::candidate_mode candidate = acacia::candidate_mode::only,
              uint64_t diagnostic_deadline_ns = 0,
              const std::string& diagnostic_source_sha256 = {},
              weakening_mode weakening = weakening_mode::incumbent,
              const acacia::unreal_witnesses::allowances& weakening_allowances = {});
