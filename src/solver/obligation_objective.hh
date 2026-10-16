#pragma once

#include "solver/realizability_simplify.hh"

#include <spot/tl/parse.hh>
#include <spot/twa/bdddict.hh>
#include <stdexcept>
#include <unordered_set>

namespace acacia::obligation {
  inline spot::formula prepare_objective (const std::string& text,
                                          const std::vector<std::string>& inputs,
                                          bool simplify) {
    auto parsed = spot::parse_infix_psl (text, spot::default_environment::instance (),
                                        false, false);
    if (!parsed.f || !parsed.errors.empty ())
      throw std::runtime_error ("unable to parse original obligation objective");
    auto objective = parsed.f;
    if (simplify)
      acacia::realizability::apply_simplifier (objective, inputs);
    return objective;
  }

  inline void register_first_block (spot::formula objective, const spot::bdd_dict_ptr& dict,
                                    const std::vector<std::string>& first, const void* owner) {
    const std::unordered_set<std::string> first_names (first.begin (), first.end ());
    objective.traverse ([&] (spot::formula ap) {
      if (ap.is (spot::op::ap) && first_names.contains (ap.ap_name ()))
        dict->register_proposition (ap, owner);
      return false;
    });
  }
}
