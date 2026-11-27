#pragma once

#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/aiger.hh>

namespace acacia::synthesis {
  // Existing controller model check: emptiness of the negated-specification
  // product. The specification automaton is built independently of synthesis.
  inline bool controller_satisfies (const spot::const_twa_graph_ptr& negated,
                                    const spot::const_twa_ptr& controller) {
    return negated && controller && !negated->intersects (controller);
  }

  inline bool controller_satisfies (const spot::const_twa_graph_ptr& negated,
                                    const spot::const_aig_ptr& controller) {
    return negated && controller && controller_satisfies (negated, controller->as_automaton (false));
  }
}
