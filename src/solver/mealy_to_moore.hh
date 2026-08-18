#pragma once

#include <spot/twaalgos/aiger.hh>

namespace acacia::synthesis {

  // Delay every output of a Mealy AIG by one step so the resulting outputs
  // depend only on latches.  Initial output values are obtained by evaluating
  // the source circuit with all inputs and latches set to false.
  spot::aig_ptr mealy_to_moore (const spot::const_aig_ptr& source);

}  // namespace acacia::synthesis
