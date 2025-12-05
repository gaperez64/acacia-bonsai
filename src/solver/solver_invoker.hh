#pragma once

#include <vector>
#include <string>

#include <spot/twaalgos/translate.hh>
#include <spot/tl/formula.hh>



spot::formula parse_ltl_string(const std::string& input);


int run_ltl(spot::translator &trans,
                   std::vector<std::string> input_aps,
                   std::vector<std::string> output_aps,
                   spot::bdd_dict_ptr dict,
                   unsigned opt_K,
                   unsigned opt_Kmin,
                   unsigned opt_Kinc,
                   std::vector<int> init_state,
                   std::string formula);