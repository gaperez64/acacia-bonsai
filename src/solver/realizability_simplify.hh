#pragma once

#include "solver/diagnostics.hh"

#include <spot/tl/apcollect.hh>
#include <spot/tl/formula.hh>
#include <string>
#include <vector>

namespace acacia::realizability {

  inline bool apply_simplifier (spot::formula& formula,
                                const std::vector<std::string>& input_aps) {
    spot::formula before_simplification = formula;
    {
#if ACACIA_ENABLE_DIAGNOSTICS
      auto* diag = acacia::diagnostics::current ();
      acacia::diagnostics::scoped_timer timer (diag ? &diag->rsimp_ms : nullptr);
#endif
      spot::realizability_simplifier rsimp (formula, input_aps);
      formula = rsimp.simplified_formula ();
    }
    return before_simplification != formula;
  }

}  // namespace acacia::realizability
