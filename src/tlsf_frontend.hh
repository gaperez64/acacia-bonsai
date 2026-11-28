#pragma once

#include "solver/solver_invoker.hh"
#include <string_view>

#include <memory>
#include <string>
#include <tlsf/convert.hpp>
#include <vector>

namespace acacia::tlsf_frontend {

  struct specification {
      std::string formula;
      std::vector<std::string> inputs;
      std::vector<std::string> outputs;
      specification_metadata metadata;
      std::shared_ptr<const TlsfAst> ast;
      const TlsfValueOrigins* value_origins = nullptr;  // Owned by ast.
  };

  specification parse (std::string_view text);
  specification parse_with_value_origins (std::string_view text);
  specification load (const std::string& path);
  specification load_with_value_origins (const std::string& path);

}  // namespace acacia::tlsf_frontend
