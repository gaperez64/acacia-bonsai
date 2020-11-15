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

#ifndef FORWARD_ALGORITHM_H_
#define FORWARD_ALGORITHM_H_

#include <time.h>
#include <glib.h>
#include "tbucw.h"
#include "tuple.h"
#include "antichain.h"
#include "hash_table.h"
#include "linked_list.h"
#include "safety_game.h"

/** Structures **/
typedef struct {
	safety_game *winning_positions;
	float otfur_time;
	float winning_positions_computation_time;
	int nb_cf_passed;
	int nb_iter;
} otfur_result;

/** Function prototypes **/
otfur_result* otfur(antichain*, antichain*, GNode*, alphabet_info*, char, int, int*);

static void add_to_losing(tuple*, antichain*, antichain*);
static char is_losing(tuple*, antichain*, antichain*);
static char has_successor_non_losing_in_safety_game(tuple*, alphabet_info*, antichain*, antichain*, antichain*);
static char has_successor_in_safety_game(tuple*, alphabet_info*, antichain*);

static char reevaluation(tuple*, alphabet_info*, antichain*, antichain*);

static void add_to_passed(tuple*, GHashTable*);
static char is_passed(tuple*, GHashTable*);

static void add_to_depend(tuple*, safety_game_edge*, GHashTable*);
static GList* get_dependencies(tuple*, GHashTable*);
void free_depend(gpointer, gpointer, gpointer);

static GList* add_to_waiting(tuple*, alphabet_info*, antichain*, antichain*, antichain*, antichain*, GList*, GHashTable*, GHashTable*);

/** Optimization: add the successors of a state s of S_O one by one **/
static char has_one_succ_to_visit_non_losing(tuple*, GHashTable*, antichain*, antichain*);
static safety_game_edge* get_first_successor_passed_non_losing(tuple*, GHashTable*, GHashTable*, antichain*, antichain*);
void free_succ_to_visit(gpointer, gpointer, gpointer);

static safety_game* compute_winning_positions(antichain*, antichain*, GHashTable*, tuple*, alphabet_info*, int);
static void update_winning_positions(tuple*, alphabet_info*, safety_game*, GHashTable*, antichain*, antichain*, GHashTable*);

#endif /* FORWARD_ALGORITHM_H_ */
