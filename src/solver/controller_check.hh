#pragma once

#include <spot/twaalgos/aiger.hh>

namespace acacia::synthesis {
  // Existing controller model check: emptiness of the negated-specification
  // product. The specification automaton is built independently of synthesis.
  inline bool controller_satisfies (const spot::const_twa_graph_ptr& negated,
                                    const spot::const_aig_ptr& controller) {
    if (!negated || !controller)
      return false;
    return !negated->intersects (controller->as_automaton (false));
  }
}
