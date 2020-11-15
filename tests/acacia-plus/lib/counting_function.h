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

#ifndef COUNTING_FUNCTION_H
#define COUNTING_FUNCTION_H

#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include "antichain.h"
#include "linked_list.h"
#include "tbucw.h"

/** Structures **/
/** Contains all information needed by the counting_function methods **/
typedef struct {
	char starting_player; //the starting player
	int composition_size; //the number of automata represented by the counting function
	int cf_size_sum; //the sum of cf_sizes
	int* k_value; //the value of K
	int* cf_size; //the size of a counting function of det(A,K) (the number of states of A)
	int* start_index_other_p; //the index of the first state of the other player
	tbucw** automaton; //array of tbucw
} cf_info;

/** Represents a counting function (or a composition of several counting functions) **/
typedef struct {
	char player; //the player concerned by the counting_function (all the states of the other player map -1)
	int sum_of_counters;
	int max_counter;
	int *mapping;
	GNode* info;
} counting_function;

/** Function prototypes **/
counting_function* new_counting_function(char, GNode*);
counting_function* build_maximal_counting_function(char, GNode*);
counting_function* build_initial_counting_function(GNode*);
counting_function* clone_counting_function(counting_function*);
void set_max_counter_and_sum_of_counters_of_counting_function(counting_function*);
GNode* build_cf_info(tbucw*, int);

char is_counting_function_empty(counting_function*);
char compare_counting_functions(counting_function*, counting_function*);
char compare_counting_functions_reverse(counting_function*, counting_function*);
char compare_max_counter_of_counting_functions(counting_function*, counting_function*);
char compare_max_counter_of_counting_functions_reverse(counting_function*, counting_function*);
char compare_counters_sum_of_counting_functions(counting_function*, counting_function*);
char compare_counters_sum_of_counting_functions_reverse(counting_function*, counting_function*);
char are_counting_functions_equal(counting_function*, counting_function*);

counting_function* compute_counting_functions_intersection(counting_function*, counting_function*);

counting_function* counting_function_omega(counting_function*, int, LABEL_BIT_REPRES **sigma);
static counting_function* update_counting_function_omega(int, int, int, int, LABEL_BIT_REPRES*, counting_function*, cf_info*);
counting_function* counting_function_succ(counting_function*, int, alphabet_info*);
static counting_function* update_counting_function_succ(int, int, int, int, LABEL_BIT_REPRES*, counting_function*, cf_info*);
antichain* minimal_counting_function_succ(counting_function*, alphabet_info*);
antichain* maximal_counting_function_succ(counting_function*, alphabet_info*);

void free_counting_function(counting_function*);
void free_cf_info(cf_info*);

void print_counting_function(counting_function*);
void print_cf_info(cf_info*);

counting_function* compose_counting_functions(counting_function**, int, GNode*);
GNode* compose_cf_info(GNode**, int);

#endif
