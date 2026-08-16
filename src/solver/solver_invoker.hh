#pragma once

#include "configuration.hh"
#include "solver/spot_fast_mode.hh"
#include "solver/symmetry_certificate.hh"

#include <spot/twaalgos/postproc.hh>

#include <optional>
#include <string>
#include <vector>

// These are the valid ways of treating unrealizability.
enum UNREAL_X_T : char { UNREAL_X_FORMULA = 'f', UNREAL_X_AUTOMATON = 'a', UNREAL_X_BOTH };
using TRANSLATION_PREF_T = spot::postprocessor::output_pref;

struct specification_metadata {
  std::string source_format = "ltl";
  std::string tlsf_semantics = "-";
  std::string tlsf_target = "-";
  std::string tlsf_effective_target = "-";
  int tlsf_gr_level = -1;
  std::vector<symmetry::indexed_family_certificate> tlsf_indexed_families;
};

inline const char* translation_pref_name (TRANSLATION_PREF_T preference) {
  switch (preference) {
    case spot::postprocessor::Any:
      return "any";
    case spot::postprocessor::Small:
      return "small";
    case spot::postprocessor::Deterministic:
      return "deterministic";
    default:
      return "unknown";
  }
}

bool run_ltl (std::vector<std::string> input_aps, std::vector<std::string> output_aps,
              VECTOR_ELT_T opt_k, VECTOR_ELT_T opt_kmin, VECTOR_ELT_T opt_kinc,
              std::string formula, std::optional<UNREAL_X_T> check_unreal,
              TRANSLATION_PREF_T translation_pref, SPOT_FAST_T spot_fast,
              const std::optional<std::string>& synth_fname,
              const specification_metadata& metadata = {});
