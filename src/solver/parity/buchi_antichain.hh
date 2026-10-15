#pragma once

#include "configuration.hh"
#include "solver/game_backend.hh"
#include "solver/parity/route.hh"
#include "solver/symmetry_certificate.hh"

namespace acacia::parity_route {
  struct antichain_options {
      VECTOR_ELT_T kmax = DEFAULT_K, kmin = DEFAULT_KMIN, kinc = DEFAULT_KINC;
#if ACACIA_FORWARD_SAFETY_SOLVER
      game_backend real_backend = game_backend::forward, unreal_backend = game_backend::forward;
#else
      game_backend real_backend = game_backend::backward, unreal_backend = game_backend::backward;
#endif
      candidate_mode candidate = candidate_mode::only;
      bool equivariance = ACACIA_ENABLE_EQUIVARIANT_SOLVER;
      bool synthesis = false;
      std::vector<symmetry::indexed_family_hint> hints;
  };

  // The routing boundary is the REAL worker's forbidden language (!phi).
  buchi_solution solve_buchi_antichain (const spot::twa_graph_ptr& forbidden,
                                        const std::vector<std::string>& inputs,
                                        const std::vector<std::string>& outputs, bool moore,
                                        const antichain_options& config = {});
}
