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

#ifndef FIX_POINT_H_
#define FIX_POINT_H_

#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include "antichain.h"
#include "counting_function.h"
#include "tuple.h"

/** Structures **/
typedef struct {
	antichain* positions_O;
	antichain* positions_I;
	char first_to_play;
} safety_game;

typedef struct {
	tuple* from;
	tuple* to;
	int label_index;
} safety_game_edge;

/** Functions prototypes **/
safety_game* new_safety_game(antichain*, antichain*, char);
safety_game* add_credits_to_safety_game(safety_game*, int, int*);
safety_game_edge* clone_safety_game_edge(safety_game_edge*);

char compare_safety_game_edges(safety_game_edge*, safety_game_edge*);
char compare_safety_game_edges_reverse(safety_game_edge*, safety_game_edge*);
char compare_safety_game_edges_max_counter(safety_game_edge*, safety_game_edge*);
char compare_safety_game_edges_max_counter_reverse(safety_game_edge*, safety_game_edge*);
char compare_safety_game_edges_counters_sum(safety_game_edge*, safety_game_edge*);
char compare_safety_game_edges_counters_sum_reverse(safety_game_edge*, safety_game_edge*);

void print_safety_game(safety_game*);
void print_safety_game_edge(safety_game_edge*);

void free_safety_game_full(safety_game*);
void free_safety_game_edge(safety_game_edge*);

#endif /* FIX_POINT_H_ */
