#pragma once

#include "configuration.hh"
#include "solver/symmetry_certificate.hh"

#include <bddx.h>
#include <optional>
#include <spot/twa/fwd.hh>
#include <vector>

namespace acacia::solver_detail {

  std::optional<spot::twa_graph_ptr> solve_game_zero_tail (
      spot::twa_graph_ptr aut, const VECTOR_ELT_T& kmax, const VECTOR_ELT_T& kmin,
      const VECTOR_ELT_T& kinc, const bdd& all_inputs, const bdd& all_outputs, bool do_synthesis,
      const std::vector<symmetry::indexed_family_hint>& hints);

}  // namespace acacia::solver_detail
