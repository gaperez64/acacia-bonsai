#pragma once


#include "safety_game.hh"

#include <bddx.h>

bool solve_game (safety_game& game, unsigned kmax, unsigned kmin, unsigned kinc, bdd all_inputs,
                 bdd all_outputs);
