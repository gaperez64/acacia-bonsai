#pragma once

#include <spot/twaalgos/aiger.hh>

namespace acacia::synthesis {

  // Delay every output of a Mealy AIG by one step so the resulting outputs
  // depend only on latches.  Initial outputs use the all-false source valuation.
  // When undoing the TLSF input shift, consume that dummy initial round by
  // initializing the source latches to its successor as well.
  spot::aig_ptr mealy_to_moore (const spot::const_aig_ptr& source,
                                bool advance_initial_state = false);

}  // namespace acacia::synthesis
