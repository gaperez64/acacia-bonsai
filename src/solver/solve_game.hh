#pragma once

#include "configuration.hh"

#include <bddx.h>
#include <optional>
#include <spot/twa/fwd.hh>

std::optional<spot::twa_graph_ptr> solve_game (spot::twa_graph_ptr aut, const VECTOR_ELT_T& kmax,
                                               const VECTOR_ELT_T& kmin, const VECTOR_ELT_T& kinc,
                                               const bdd& all_inputs, const bdd& all_outputs,
                                               bool do_synthesis);
