#pragma once

#include <bddx.h>

namespace acacia {

  struct ap_partition {
      bdd inputs, outputs;
  };

  inline ap_partition project_ap_partition (const bdd& ap_vars, const bdd& all_inputs,
                                            const bdd& all_outputs) {
    // Project the opposite interface cube out of the automaton's AP cube.
    // Call after worker transformations, which may remove atomic propositions.
    return {bdd_exist (ap_vars, all_outputs), bdd_exist (ap_vars, all_inputs)};
  }

}  // namespace acacia
