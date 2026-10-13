#pragma once

#include "solver/solver_invoker.hh"

#include <string>
#include <string_view>
#include <vector>

namespace acacia::tlsf_frontend {

  struct specification {
    std::string formula;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    specification_metadata metadata;
  };

  specification parse (std::string_view text);
  specification load (const std::string& path);

}  // namespace acacia::tlsf_frontend
