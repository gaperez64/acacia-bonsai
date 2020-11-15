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

#include "safety_game.h"

/** Builds a new safety_game structure **/
safety_game*
new_safety_game(antichain* positions_I, antichain* positions_O, char player) {
	safety_game *sg = (safety_game*)malloc(sizeof(safety_game));
	sg->positions_O = positions_O;
	sg->positions_I = positions_I;
	sg->first_to_play = player;

	return sg;
}

/** Adds credit vector to each element of the safety game **/
safety_game*
add_credits_to_safety_game(safety_game *sg, int dimension, int *max_credit) {
	GList *curlink = sg->positions_I->incomparable_elements;
	while(curlink != NULL) {
		curlink->data = add_maximal_vector_to_tuple(curlink->data, dimension, max_credit);
		curlink = curlink->next;
	}

	curlink = sg->positions_O->incomparable_elements;
	while(curlink != NULL) {
		curlink->data = add_maximal_vector_to_tuple(curlink->data, dimension, max_credit);
		curlink = curlink->next;
	}

	return sg;
}

/** Clones a safety game edge
    Warning: it only clones the to tuple of the edge **/
safety_game_edge*
clone_safety_game_edge(safety_game_edge *edge) {
	safety_game_edge *clone = (safety_game_edge*)malloc(sizeof(safety_game_edge));
	clone->from = edge->from;
	clone->to = clone_tuple(edge->to);
	clone->label_index = edge->label_index;
	return clone;
}

/** Returns TRUE if e1->to <= e2->to, FALSE otherwise, where <= is the tuples comparison operator **/
char
compare_safety_game_edges(safety_game_edge *e1, safety_game_edge *e2) {
	return compare_tuples(e1->to, e2->to);
}

/** Returns TRUE if e1->to => e2->to, FALSE otherwise, where <= is the tuples comparison operator **/
char
compare_safety_game_edges_reverse(safety_game_edge *e1, safety_game_edge *e2) {
	return compare_tuples_reverse(e1->to, e2->to);
}

/** Returns TRUE if e1->to <= e2->to according to the maximum counter comparison of counting_functions, FALSE otherwise **/
char
compare_safety_game_edges_max_counter(safety_game_edge *e1, safety_game_edge *e2) {
	return compare_max_counter_of_counting_functions(e1->to->cf, e2->to->cf);
}

/** Returns TRUE if e1->to >= e2->to according to the maximum counter comparison of counting_functions, FALSE otherwise **/
char
compare_safety_game_edges_max_counter_reverse(safety_game_edge *e1, safety_game_edge *e2) {
	return compare_max_counter_of_counting_functions_reverse(e1->to->cf, e2->to->cf);
}

/** Returns TRUE if e1->to <= e2->to according to the counters sum comparison of counting_functions, FALSE otherwise **/
char
compare_safety_game_edges_counters_sum(safety_game_edge *e1, safety_game_edge *e2) {
	return compare_counters_sum_of_counting_functions(e1->to->cf, e2->to->cf);
}

/** Returns TRUE if e1->to >= e2->to according to the counters sum comparison of counting_functions, FALSE otherwise **/
char
compare_safety_game_edges_counters_sum_reverse(safety_game_edge *e1, safety_game_edge *e2) {
	return compare_counters_sum_of_counting_functions_reverse(e1->to->cf, e2->to->cf);
}

/** Frees a safety game, and all data it contains **/
void
free_safety_game(safety_game *sg) {
	free_antichain_full(sg->positions_I, (void*)free_tuple);
	free_antichain_full(sg->positions_O, (void*)free_tuple);
}

/** Frees a safety game edge
 	Warning: it does free the to tuple of the edge but not the from tuple **/
void
free_safety_game_edge(safety_game_edge *edge) {
	free_tuple_full(edge->to);
	free(edge);
}

/** Prints the safety game **/
void
print_safety_game(safety_game *sg) {
	printf("Safety game: \nPO positions: ");
	print_antichain(sg->positions_O, (void*)print_tuple);
	printf("PI positions: ");
	print_antichain(sg->positions_I, (void*)print_tuple);
	printf("Starting player: %d\n", sg->first_to_play);
}

/** Prints a safety game edge **/
void
print_safety_game_edge(safety_game_edge *edge) {
	printf("[");
	print_tuple(edge->from);
	printf(" -> ");
	print_tuple(edge->to);
	printf("]\n");
}
