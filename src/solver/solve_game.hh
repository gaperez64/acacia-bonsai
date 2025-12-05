#pragma once

#include <bddx.h>
#include "safety_game.hh"


bool solve_game (safety_game& game, unsigned Kmax, unsigned Kmin, unsigned Kinc, bdd all_inputs, bdd all_outputs,
    std::vector<int> init_state, bdd invariant);
