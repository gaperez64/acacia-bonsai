#pragma once

#include "error_msg.hh"

#include <regex>
#include <string>

namespace acacia::synthesis {
  inline void validate_aiger_mode (const std::string& value) {
    // Intentionally exclude empty components that Spot's trailing separators allow.
    static const std::regex mode (
        "(ite|isop|both|optim)(\\+(dc|ud|sub[0-2]))*"
        "(,(ite|isop|both|optim)(\\+(dc|ud|sub[0-2]))*)*");
    if (!std::regex_match (value, mode))
      error (EXIT_CODE_ERROR,
             "Error: invalid --aiger-mode '%s'; expected ite|isop|both|optim followed by "
             "+ud or +sub0|+sub1|+sub2, or comma-separated alternatives.\n",
             value.c_str ());
    if (value.find ("+dc") != std::string::npos)
      error (EXIT_CODE_ERROR,
             "Error: --aiger-mode '%s': +dc is disabled because it can produce incorrect "
             "controllers with Acacia's original BDD dictionary; see issue #466.\n",
             value.c_str ());
  }
}
