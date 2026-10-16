#pragma once

#include <algorithm>
#include <iostream>
#include <spot/tl/parse.hh>
#include <stdexcept>
#include <string>
#include <vector>

namespace acacia::formula_input {
  inline spot::formula parse (const std::string& input) {
    auto parsed = spot::parse_infix_psl (input, spot::default_environment::instance (), false, false);
    if (not parsed.f || not parsed.errors.empty ()) {
      parsed.format_errors (std::cerr);
      throw std::runtime_error ("Error parsing LTL formula");
    }
    return parsed.f;
  }

  inline void shift_inputs (spot::formula& formula, const std::vector<std::string>& inputs) {
    auto rec = [&inputs] (auto&& self, spot::formula part) -> spot::formula {
      if (part.is (spot::op::ap) && std::ranges::find (inputs, part.ap_name ()) != inputs.end ())
        return spot::formula::X (part);
      return part.map ([&] (spot::formula child) { return self (self, child); });
    };
    formula = formula.map ([&] (spot::formula child) { return rec (rec, child); });
  }
}  // namespace acacia::formula_input
