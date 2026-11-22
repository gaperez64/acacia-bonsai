#pragma once

#include "solver/solver_invoker.hh"
#include <unordered_map>

#include <functional>
#include <spot/tl/formula.hh>

namespace acacia::restriction {
  using substitution = std::unordered_map<std::string, spot::formula>;
  struct candidate {
      const char* name;
      unsigned family;
      substitution values;
  };

  std::vector<candidate> candidates (const specification_metadata& metadata,
                                     const std::vector<std::string>& outputs, bool inputs);

  // Insert replacement leaves once; ownership is always the original positive frame.
  std::optional<spot::formula> substitute (spot::formula positive, const substitution& values,
                                           const std::vector<std::string>& inputs,
                                           const std::vector<std::string>& outputs,
                                           bool restrict_inputs);
  bool accepts (bool inputs, int verdict);

  // The callback decides the restricted positive objective under the unchanged I/O map.
  bool try_prepass (spot::formula positive, const specification_metadata& metadata,
                    const std::vector<std::string>& inputs,
                    const std::vector<std::string>& outputs, bool restrict_inputs,
                    uint64_t outer_deadline, const std::function<int (spot::formula)>& solve);
}  // namespace acacia::restriction
