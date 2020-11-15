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

#include "tuple.h"

tuple *NOT_DEFINED;
/** Sets the "not defined" tuple that might be returned by omega_tuple **/
void
set_not_defined_tuple() {
	NOT_DEFINED = new_tuple(NULL, NULL);
}

/** Creates a new tuple composed by cf and credits **/
tuple*
new_tuple(counting_function *cf, vector *credits) {
	tuple *t = (tuple*)malloc(sizeof(tuple));
	t->cf = cf;
	t->credits = credits;

	return t;
}

/** Builds the maximal tuple, i.e. the tuple composed by the maximal counting_function and the credits vector where each component maps 0 **/
tuple*
build_maximal_tuple(char player, GNode* cfinfo, int dimension, int *max_credit) {
	int *values = (int*)malloc(dimension*sizeof(int));
	int i;
	for(i=0; i<dimension; i++) {
		values[i] = 0;
	}
	tuple *t = new_tuple(build_maximal_counting_function(player, cfinfo), new_vector(dimension, values, max_credit));
	set_max_counter_and_sum_of_counters_of_counting_function(t->cf);
	free(values);

	return t;
}

/** Builds the initial tuple, i.e. the tuple composed by the initial counting_function and the credits vector where each component maps max_credit **/
tuple*
build_initial_tuple(GNode *cfinfo, int dimension, int *max_credit) {
	tuple *t = new_tuple(build_initial_counting_function(cfinfo), new_vector(dimension, max_credit, max_credit));

	return t;
}

/** Builds a new tuple containing the cf of t, and maximal credits vector of dimension d and max credit c
 	Frees the former tuple **/
tuple*
add_maximal_vector_to_tuple(tuple* t, int d, int *c) {
	tuple *new_t = (tuple*)malloc(sizeof(tuple));
	new_t->cf = t->cf;

	int *values = (int*)malloc(d*sizeof(int));
	int i;
	for(i=0; i<d; i++) {
		values[i] = 0;
	}
	new_t->credits = new_vector(d, values, c);

	free_vector(t->credits);
	free(t);
	free(values);

	return new_t;
}

/** Creates a new tuple which is the exact copy of t **/
tuple*
clone_tuple(tuple *t) {
	tuple *copy = (tuple*)malloc(sizeof(tuple));
	copy->cf = clone_counting_function(t->cf);
	copy->credits = clone_vector(t->credits);
	return copy;
}

/** Returns TRUE if t->cf is empty, FALSE otherwise **/
char
is_tuple_empty(tuple *t) {
	return is_counting_function_empty(t->cf);
}

/** Returns TRUE if t1->cf <= t2->cf and t1->c_i >= t2->c_i, forall 0<=i<dimension, FALSE otherwise **/
char
compare_tuples(tuple *t1, tuple *t2) {
	if(compare_counting_functions(t1->cf, t2->cf) == TRUE) {
		if(compare_vectors(t2->credits, t1->credits) == TRUE) {
			return TRUE;
		}
	}
	return FALSE;
}

/** Returns TRUE if t2->cf <= t1->cf and t2->c_i >= t1->c_i, forall 0<=i<dimension, FALSE otherwise **/
char
compare_tuples_reverse(tuple *t1, tuple *t2) {
	return compare_tuples(t2, t1);
}

/** Returns TRUE if t1 and t2 are equal, FALSE otherwise **/
char
are_tuples_equal(tuple *t1, tuple *t2) {
	if(are_counting_functions_equal(t1->cf, t2->cf) == TRUE) {
		if(are_vectors_equal(t1->credits, t2->credits) == TRUE) {
			return TRUE;
		}
	}
	return FALSE;
}

/** Compute the intersection of two tuples
    the result is the intersection of the counting_functions and the maximal of credits vectors (component-wize) **/
tuple*
compute_tuples_intersection(tuple *t1, tuple *t2) {
	tuple *inters = (tuple*)malloc(sizeof(tuple));
	inters->cf = compute_counting_functions_intersection(t1->cf, t2->cf);
	inters->credits = compute_vectors_intersection(t1->credits, t2->credits);

	return inters;
}

/** Computes the predecessor of the tuple for label indexed label_index in sigma
 	Warning: the omega function is not total and returns the special NOT_DEFINED tuple when is not defined **/
tuple*
tuple_omega(tuple *t, int label_index, LABEL_BIT_REPRES **sigma) {
	int *label_value;
	if(t->cf->player == P_O) {
		label_value = ((cf_info*)(t->cf->info->data))->automaton[0]->v_I[label_index];
	}
	else {
		label_value = ((cf_info*)(t->cf->info->data))->automaton[0]->v_O[label_index];
	}

	vector *result_credits = vector_omega(t->credits, label_value);
	if(result_credits != NULL) {
		return new_tuple(counting_function_omega(t->cf, label_index, sigma), result_credits);
	}
	else {
		return NOT_DEFINED;
	}
}

/** Computes the successor of a tuple for the label indexed label_index in the sigma of alphabet corresponding to the player of t->cf **/
tuple*
tuple_succ(tuple *t, int label_index, alphabet_info *alphabet) {
	int i;
	int *label_value;
	if(t->cf->player == P_O) {
		label_value = ((cf_info*)(t->cf->info->data))->automaton[0]->v_O[label_index];
	}
	else {
		label_value = ((cf_info*)(t->cf->info->data))->automaton[0]->v_I[label_index];
	}

	tuple *succ = new_tuple(counting_function_succ(t->cf, label_index, alphabet), vector_succ(t->credits, label_value));

	return succ;
}

/** Computes the antichain of minimal successor of tuple t for all sigma in the player alphabet **/
antichain*
minimal_tuple_succ(tuple *t, alphabet_info *alphabet) {
	GList *minimal_elements = NULL;
	int sigma_size, i;
	if(t->cf->player == P_O) {
		sigma_size = alphabet->sigma_output_size;
	}
	else {
		sigma_size = alphabet->sigma_input_size;
	}

	tuple *cur_succ;
	for(i=0; i<sigma_size; i++) {
		cur_succ = tuple_succ(t, i, alphabet);
		minimal_elements = scan_add_or_remove_and_free(minimal_elements, cur_succ, (void*)compare_tuples_reverse, (void*)free_tuple_full);
	}

	antichain* minimal_succ = (antichain*)malloc(sizeof(antichain));
	minimal_succ->size = g_list_length(minimal_elements);
	minimal_succ->incomparable_elements = minimal_elements;

	return minimal_succ;
}

/** Computes the antichain of maximal successor of tuple t for all sigma in the player alphabet **/
antichain*
maximal_tuple_succ(tuple *t, alphabet_info *alphabet) {
	GList *maximal_elements = NULL;
	int sigma_size, i;
	if(t->cf->player == P_O) {
		sigma_size = alphabet->sigma_output_size;
	}
	else {
		sigma_size = alphabet->sigma_input_size;
	}

	tuple *cur_succ;
	for(i=0; i<sigma_size; i++) {
		cur_succ = tuple_succ(t, i, alphabet);
		maximal_elements = scan_add_or_remove_and_free(maximal_elements, cur_succ, (void*)compare_tuples, (void*)free_tuple_full);
	}

	antichain* maximal_succ = (antichain*)malloc(sizeof(antichain));
	maximal_succ->size = g_list_length(maximal_elements);
	maximal_succ->incomparable_elements = maximal_elements;
	return maximal_succ;
}

/** Frees the tuple **/
void
free_tuple(tuple *t) {
	free_vector(t->credits);
	free(t);
}

/** Frees the tuple and the counting_function **/
void
free_tuple_full(tuple *t) {
	free_counting_function(t->cf);
	free_tuple(t);
}

/** Frees the tuple t with the free_tuple_full function if t is not the special tuple NOT_DEFINED **/
void
free_tuple_full_protected(tuple *t) {
	if(t != NOT_DEFINED) {
		free_tuple_full(t);
	}
}

/** Frees the NOT_DEFINED tuple **/
void
free_not_defined_tuple() {
	free(NOT_DEFINED);
}

/** Prints the tuple **/
void
print_tuple(tuple *t) {
	printf("[");
	print_counting_function(t->cf);
	printf(", ");
	print_vector(t->credits);
	printf("]");
}

/** Creates a new tuple corresponding to the composition of ts **/
tuple*
compose_tuples(tuple **ts, int nb_ts, GNode *composition_info) {
	counting_function **cfs = (counting_function**)malloc(nb_ts*sizeof(counting_function*));
	vector **vs = (vector**)malloc(nb_ts*sizeof(vector*));
	int i;
	for(i=0; i<nb_ts; i++) {
		cfs[i] = ts[i]->cf;
		vs[i] = ts[i]->credits;
	}

	tuple *comp = (tuple*)malloc(sizeof(tuple));
	comp->cf = compose_counting_functions(cfs, nb_ts, composition_info);
	comp->credits = compose_vectors(vs, nb_ts);

	free(cfs);
	free(vs);

	return comp;
}
