#pragma once

#include "solver/closure_buchi_provider.hh"

namespace acacia::closure_recurrence {
  using closure_buchi::Failure;
  using closure_buchi::FailureKind;
  using closure_buchi::Options;
  struct Counters {
      std::size_t branches = 0, blocks = 0, watches = 0, states = 0, edges = 0;
      std::uint64_t construction_ns = 0;
  };
  // Reuse Options: max_edges_per_row caps the whole construction;
  // step/alternative/guard budgets cap each expansion.
  // Eager, transactional construction of L(bad). The dictionary's declared APs
  // are retained, in BDD-variable order. No partial graph escapes on failure.
  closure_buchi::Result<spot::twa_graph_ptr> build (spot::formula bad,
                                                    const spot::bdd_dict_ptr& dict,
                                                    Options options = {},
                                                    Counters* counters = nullptr) noexcept;
}
