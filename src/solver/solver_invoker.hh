#pragma once

#include "solver/solver_invoker.hh"

#include <spot/tl/formula.hh>
#include <spot/twaalgos/translate.hh>
#include <optional>
#include <string>
#include <vector>

// These are the valid ways of treating unrealizability
enum unreal_x_t {
  UNREAL_X_FORMULA,
  UNREAL_X_AUTOMATON,
  UNREAL_X_BOTH
};

spot::formula parse_ltl_string (const std::string& input);

int run_ltl (spot::translator& trans, std::vector<std::string> input_aps,
             std::vector<std::string> output_aps, spot::bdd_dict_ptr dict, unsigned opt_k,
             unsigned opt_kmin, unsigned opt_kinc, std::string formula, std::optional<unreal_x_t>);
