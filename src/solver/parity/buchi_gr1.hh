#pragma once

#include "solver/parity/route.hh"
#include <tlsf/gr1_oxidd.h>

namespace acacia::parity_route {
  using gr1_game = std::unique_ptr<Aig, decltype (&aig_free)>;

  // The completed DBA recognizes the forbidden language, not the objective.
  gr1_game encode_buchi_gr1 (const spot::twa_graph_ptr& forbidden,
                             const std::vector<std::string>& inputs,
                             const std::vector<std::string>& outputs, bool moore);
  buchi_solution solve_buchi_gr1 (const spot::twa_graph_ptr& forbidden,
                                  const std::vector<std::string>& inputs,
                                  const std::vector<std::string>& outputs, bool moore,
                                  bool synthesis);
}
