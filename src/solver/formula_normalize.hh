#pragma once

#include <spot/tl/formula.hh>

namespace acacia {
  // Split G over conjunctions and implication consequents, or fold nested G.
  inline bool split_globally (spot::formula g, auto emit) {
    using spot::formula;
    using spot::op;
    if (g.is (op::And))
      for (auto c : g)
        emit (formula::G (c));
    else if (g.is (op::Implies) and g[1].is (op::And))
      for (auto c : g[1])
        emit (formula::G (formula::Implies (g[0], c)));
    else if (g.is (op::G))
      emit (g);
    else
      return false;
    return true;
  }
}  // namespace acacia
