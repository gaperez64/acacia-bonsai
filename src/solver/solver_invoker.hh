#pragma once

#include <spot/tl/formula.hh>
#include <spot/twaalgos/translate.hh>
#include <string>
#include <vector>

spot::formula parse_ltl_string (const std::string& input);

int run_ltl (spot::translator& trans, std::vector<std::string> input_aps,
             std::vector<std::string> output_aps, spot::bdd_dict_ptr dict, unsigned opt_k,
             unsigned opt_kmin, unsigned opt_kinc, std::vector<int> init_state,
             std::string formula);