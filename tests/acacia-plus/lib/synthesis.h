/*
 * This file is part of Acacia+, a tool for synthesis of reactive systems using antichain-based techniques
 * Copyright (C) 2011-2013 UMONS-ULB
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef SYNTHESIS_H_
#define SYNTHESIS_H_

#include <stdio.h>
#include <stdlib.h>
#include "tbucw.h"
#include "antichain.h"
#include "tuple.h"
#include "safety_game.h"
#include "transition_system.h"

/** Functions prototypes **/
char has_a_winning_strategy(safety_game*, alphabet_info*, char);
transition_system* extract_strategies_from_safety_game(safety_game*, alphabet_info*, char, char, char);
static char deal_state_PI(safety_game*, alphabet_info*, transition_system*, int, tuple*, char, char, char*);
static void deal_state_PO(safety_game*, alphabet_info*, transition_system*, int, tuple*, char, char*);
static void deal_state_PO_crit(safety_game*, alphabet_info*, transition_system*, int, tuple*, char, char*);
static void update_solution(safety_game*, alphabet_info*, transition_system*, int, int, int, int, tuple*, char, char, char*);

#endif /* SYNTHESIS_H_ */
