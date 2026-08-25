#pragma once

#include <algorithm>
#include <cstddef>
#include <spot/tl/formula.hh>
#include <utility>
#include <vector>

namespace acacia::unreal_witnesses {

  // A large G(a & b & ...) becomes a generalized-Buchi translation with one
  // acceptance set per liveness conjunct.  Spot is intentionally compiled
  // with a finite acceptance-set limit, so build a few sound, much smaller
  // unrealizability witnesses.  Each witness is safety_core & obligation, a
  // logical consequence of the original specification: proving one
  // unrealizable proves the original unrealizable, while an inconclusive
  // witness changes no verdict.
  inline std::vector<spot::formula> make_safety_core_witnesses (
      const spot::formula& formula, size_t max_witnesses = 8) {
    if (max_witnesses == 0 or not formula.is (spot::op::And))
      return {};

    std::vector<spot::formula> conjuncts;
    bool has_oversized_global_conjunction = false;
    for (spot::formula conjunct : formula) {
      if (conjunct.is (spot::op::G) and conjunct[0].is (spot::op::And)) {
        has_oversized_global_conjunction =
            has_oversized_global_conjunction or conjunct[0].size () > 64;
        for (spot::formula nested : conjunct[0])
          conjuncts.push_back (spot::formula::G (nested));
      }
      else {
        conjuncts.push_back (conjunct);
      }
    }
    if (not has_oversized_global_conjunction)
      return {};

    std::vector<spot::formula> safety_core;
    std::vector<spot::formula> obligations;
    for (spot::formula conjunct : conjuncts) {
      if (conjunct.is_syntactic_safety ())
        safety_core.push_back (conjunct);
      else
        obligations.push_back (conjunct);
    }
    if (safety_core.empty () or obligations.empty ())
      return {};

    std::vector<spot::formula> witnesses;
    witnesses.reserve (std::min (max_witnesses, obligations.size ()));
    for (spot::formula obligation : obligations) {
      std::vector<spot::formula> parts = safety_core;
      parts.push_back (obligation);
      witnesses.push_back (spot::formula::And (std::move (parts)));
      if (witnesses.size () == max_witnesses)
        break;
    }
    return witnesses;
  }

}  // namespace acacia::unreal_witnesses
