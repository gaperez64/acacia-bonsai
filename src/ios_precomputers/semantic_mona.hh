#pragma once

// Sprint A stage A1: the MONA precomputer with a pre-decoding semantic action
// quotient.
//
// `ios_precomputers::mona` decodes a transition set for every output path that
// reaches the state-variable frontier of R(i,o,p,q).  Many paths reach the same
// residual BDD node.  BuDDy nodes are canonical, so those paths denote the same
// endpoint relation and decode to identical transition sets; the controller
// predecessor unions over outputs and union is idempotent, so keeping one copy
// cannot change the fixed point.  This selects that behaviour on the shared
// traversal, so there is one implementation of the descent and not two.
//
// The quotient keeps the FIRST occurrence of each residual root, which leaves a
// subsequence of the order `mona` already produces.  That matters: the input
// pickers scan an input's actions in order, stop at the first action that keeps
// the state in the region, and move it to the front, so re-sorting the actions
// would be a different algorithm rather than the same one with less work.

#include "ios_precomputers/mona.hh"

namespace ios_precomputers {

  struct semantic_mona {
      template <typename Aut, typename TransSet = std::vector<std::pair<unsigned, unsigned>>>
      static auto make (Aut aut, bdd input_support, bdd output_support) {
        return detail::mona<Aut, TransSet, true> (aut, input_support, output_support);
      }
  };
}
