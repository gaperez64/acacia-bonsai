#pragma once

#include "actioners.hh"
#include "input_pickers.hh"
#include "ios_precomputers.hh"
#include "safety_game.hh"
#include "k-bounded_safety_aut.hh"

#include <bddx.h>

bool solve_game (safety_game& game, unsigned kmax, unsigned kmin, unsigned kinc, bdd all_inputs,
                 bdd all_outputs);
