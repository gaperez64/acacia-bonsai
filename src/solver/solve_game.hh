#pragma once

#include <bddx.h>

bool solve_game (spot::twa_graph_ptr aut, unsigned kmax, unsigned kmin, unsigned kinc, bdd all_inputs,
                 bdd all_outputs);
