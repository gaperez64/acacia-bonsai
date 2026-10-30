#pragma once

#include "solver/solver_invoker.hh"
#include <string_view>
#include <tlsf/ast_api.h>

#include <memory>
#include <string>
#include <vector>

namespace acacia::tlsf_frontend {

  struct specification {
      std::string formula;
      std::vector<std::string> inputs;
      std::vector<std::string> outputs;
      specification_metadata metadata;
      std::shared_ptr<TlsfAst> ast;
  };

  specification parse (std::string_view text);
  specification load (const std::string& path);

}  // namespace acacia::tlsf_frontend
