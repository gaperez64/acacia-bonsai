#pragma once

#include "solver/solver_invoker.hh"

#include <string>
#include <string_view>
#include <vector>

namespace acacia::tlsf_frontend {

  enum class normalization_policy { off, match_safe, route_safe };
  normalization_policy parse_normalization_policy (std::string_view policy);
  void report_normalization (const specification_metadata& metadata, const char* route);

  struct specification {
    std::string formula;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    specification_metadata metadata;
  };

  specification parse (std::string_view text, normalization_policy policy = normalization_policy::off);
  specification load (const std::string& path, normalization_policy policy = normalization_policy::off);

}  // namespace acacia::tlsf_frontend
