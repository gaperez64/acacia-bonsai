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

#include "counting_function.h"

/** Creates a new empty counting function (all counters map -1)
    Warning: the sum_of_counters and max_counter components of built counting_function are not initialized **/
counting_function*
new_counting_function(char player, GNode* cfinfo) {
	counting_function* cf = (counting_function*)malloc(sizeof(counting_function));
	cf->player = player;
	cf->info = cfinfo;
	cf_info *info = (cf_info*)cfinfo->data;
	cf->mapping = (int*)malloc((info->cf_size_sum)*sizeof(int));
	int i;
	for(i=0; i<info->cf_size_sum; i++) {
		cf->mapping[i] = -1;
	}

	return cf;
}

/** Builds the maximal counting_function for player and k_value (all tbUCW_states of player map k_value, others map -1)
    Note that - if k=0, all accepting tbucw_states map -1 (because they should map at least 1, which is > 0)
    		  - a trash state always maps -1
 	Optimization 1: the states which can not carry a counter k do not need counter but a boolean value (-1 -> not in the counting function, 0 -> in the counting function)
 	Warning: the sum_of_counters and max_counter components of built counting_function are not initialized **/
counting_function*
build_maximal_counting_function(char player, GNode* tree_info) {
	counting_function* cf = (counting_function*)malloc(sizeof(counting_function));
	cf->player = player;
	cf->info = tree_info;
	cf_info *info = (cf_info*)tree_info->data;
	cf->mapping = (int*)malloc((info->cf_size_sum)*sizeof(int));

	tbucw** automaton = info->automaton;
	int* cf_size = info->cf_size;
	int* k_value = info->k_value;
	int composition_size = info->composition_size;
	int i, k, global_index=0;
	for(k=0; k<info->composition_size; k++) {
		for(i=0; i<cf_size[k]; i++) {
			if(get_player_id(automaton[k], i) == player) {
				if(automaton[k]->states[i]->is_trash == FALSE) {
					if(is_tbucw_state_unbounded(automaton[k], i) == TRUE) { //Optimization 1
						if(k_value[k] != 0 || is_accepting(automaton[k], i) == FALSE) { //if s is accepting, it cannot map 0 {
							cf->mapping[global_index] = k_value[k];
						}
						else {
							cf->mapping[global_index] = -1;
						}
					}
					else {
						cf->mapping[global_index] = 0;
					}
				}
				else {
					cf->mapping[global_index] = -1;
				}
			}
			else {
				cf->mapping[global_index] = -1;
			}
			global_index++;
		}
	}

	return cf;
}

/** Builds the initial counting_function F_0 = q \in Q_0 -> -1 if q != q_0, 0 if q is non accepting, 1 if q is accepting **/
counting_function*
build_initial_counting_function(GNode* cfinfo) {
	cf_info *info = (cf_info*)cfinfo->data;
	counting_function *cf = new_counting_function(info->starting_player, cfinfo);

	int* k_value = info->k_value;
	int composition_size = info->composition_size;
	int *cf_size = info->cf_size;
	tbucw** automaton = info->automaton;
	int k;
	int global_start_index = 0;
	for(k=0; k<composition_size; k++) {
		if(is_accepting(automaton[k], automaton[k]->initial_state_index)) {
			cf->mapping[global_start_index + automaton[k]->initial_state_index] = 1;
		}
		else {
			cf->mapping[global_start_index + automaton[k]->initial_state_index] = 0;
		}
		global_start_index += cf_size[k];
	}
	set_max_counter_and_sum_of_counters_of_counting_function(cf);

	return cf;
}

/** Creates a new counting_function which is the exact copy of cf
 	Warning: the GNode info is not duplicated 					  **/
counting_function*
clone_counting_function(counting_function *cf) {
	cf_info* info = ((cf_info*)(cf->info->data));
	counting_function* copy = (counting_function*)malloc(sizeof(counting_function));
	copy->player = cf->player;
	copy->info = cf->info;
	copy->sum_of_counters = cf->sum_of_counters;
	copy->max_counter = cf->max_counter;
	copy->mapping = (int*)malloc((info->cf_size_sum)*sizeof(int));
	int i;
	for(i=0; i<info->cf_size_sum; i++) {
		copy->mapping[i] = cf->mapping[i];
	}
	return copy;
}

/** Sets the max_counter and sum_of_counters components of counting function cf **/
void
set_max_counter_and_sum_of_counters_of_counting_function(counting_function *cf) {
	cf_info* info = (cf_info*)cf->info->data;
	int counter_max = -1;
	int counters_sum = 0;
	int* start_index_other_p = info->start_index_other_p;
	int* cf_size = info->cf_size;
	tbucw** automaton = info->automaton;
	int* k_value = info->k_value;
	int composition_size = info->composition_size;
	char starting;
	if(cf->player == info->starting_player) {
		starting = TRUE;
	}
	else {
		starting = FALSE;
	}
	int i, k, cur_start_index, cur_end_index, global_index=0;
	for(k=0; k<composition_size; k++) {
		if(starting == TRUE) {
			cur_start_index = 0;
			cur_end_index = start_index_other_p[k];
		}
		else {
			cur_start_index = start_index_other_p[k];
			cur_end_index = cf_size[k];
		}
		for(i=cur_start_index; i<cur_end_index; i++) {
			counter_max = MAX(counter_max, cf->mapping[global_index+i]);
			counters_sum += cf->mapping[global_index+i];
		}
		global_index += cf_size[k];
	}
	cf->max_counter = counter_max;
	cf->sum_of_counters = counters_sum;
}

/** Builds the cf_info according to aut and K **/
GNode*
build_cf_info(tbucw *aut, int k) {
	cf_info* info = (cf_info*)malloc(sizeof(cf_info));
	info->starting_player = get_player_id(aut, aut->initial_state_index);
	info->composition_size = 1;
	int i, nb_starting_states = 0, nb_other_states = 0;
	for(i=0; i<aut->nb_states; i++) {
		if(aut->states[i]->player == info->starting_player) {
			nb_starting_states++;
		}
		else {
			nb_other_states++;
		}
	}
	info->k_value = (int*)malloc(sizeof(int));
	info->cf_size = (int*)malloc(sizeof(int));
	info->start_index_other_p = (int*)malloc(sizeof(int));
	info->automaton = (tbucw**)malloc(sizeof(tbucw*));
	info->k_value[0] = k;
	info->cf_size[0] = aut->nb_states;
	info->start_index_other_p[0] = nb_starting_states;
	info->automaton[0] = aut;
	info->cf_size_sum = info->cf_size[0];

	return g_node_new(info);
}

/** Returns TRUE if cf is empty, FALSE otherwise **/
char
is_counting_function_empty(counting_function *cf) {
	cf_info* info = (cf_info*)(cf->info)->data;
	int* cf_size = info->cf_size;
	int* start_index_other_p = info->start_index_other_p;
	int composition_size = info->composition_size;
	char starting;
	if(cf->player == info->starting_player) {
		starting = TRUE;
	}
	else {
		starting = FALSE;
	}
	int i, k, cur_start_index, cur_end_index, global_index=0;
	for(k=0; k<composition_size; k++) {
		if(starting == TRUE) {
			cur_start_index = 0;
			cur_end_index = start_index_other_p[k];
		}
		else {
			cur_start_index = start_index_other_p[k];
			cur_end_index = cf_size[k];
		}
		for(i=cur_start_index; i<cur_end_index; i++) {
			if(cf->mapping[global_index+i] != -1) {
				return FALSE;
			}
		}
		global_index += cf_size[k];
	}
	return TRUE;
}

/** Returns TRUE if cf1 <= cf2, FALSE otherwise **/
char
compare_counting_functions(counting_function *cf1, counting_function *cf2) {
/*	if(cf1->player != cf2->player) { //Impossible?
		printf("Comparison of incompatible counting functions!\n");
		exit(0);
	}*/
	// Check if sum of counters cf1 is strictly greater than sum of counters of cf2 or if max counter of cf1 is strictly greater than max counter of cf2.
	// If so, we know that cf1 is not smaller than cf2
	if(compare_counters_sum_of_counting_functions(cf1, cf2) == FALSE || compare_max_counter_of_counting_functions(cf1, cf2) == FALSE) {
		return FALSE;
	}

	cf_info* info = (cf_info*)(cf1->info)->data;
	int* start_index_other_p = info->start_index_other_p;
	int* cf_size = info->cf_size;
	int composition_size = info->composition_size;
	char starting;
	if(cf1->player == info->starting_player) {
		starting = TRUE;
	}
	else {
		starting = FALSE;
	}
	int i, k, cur_start_index, cur_end_index, global_index=0;
	for(k=0; k<composition_size; k++) {
		if(starting == TRUE) {
			cur_start_index = 0;
			cur_end_index = start_index_other_p[k];
		}
		else {
			cur_start_index = start_index_other_p[k];
			cur_end_index = cf_size[k];
		}
		for(i=cur_start_index; i<cur_end_index; i++) {
			if((cf1->mapping[global_index+i] > cf2->mapping[global_index+i])) {
				return FALSE;
			}
		}
		global_index += cf_size[k];
	}
	return TRUE;
}


/** Returns TRUE if cf1 >= cf2, FALSE otherwise **/
char
compare_counting_functions_reverse(counting_function *cf1, counting_function *cf2) {
	return compare_counting_functions(cf2, cf1);
}

/** Returns TRUE if the maximum value of counter of cf1 is smaller or equal than the maximum value of counter of cf2, FALSE otherwise **/
char
compare_max_counter_of_counting_functions(counting_function *cf1, counting_function *cf2) {
	return cf1->max_counter <= cf2->max_counter;
}

/** Returns TRUE if the maximum value of counter of cf1 is greater or equal than the maximum value of counter of cf2, FALSE otherwise **/
char
compare_max_counter_of_counting_functions_reverse(counting_function *cf1, counting_function *cf2) {
	return cf1->max_counter >= cf2->max_counter;
}

/** Returns TRUE if the sum of counters of cf1 is smaller or equal than the sum of counters of cf2, FALSE otherwise **/
char
compare_counters_sum_of_counting_functions(counting_function *cf1, counting_function *cf2) {
	return cf1->sum_of_counters <= cf2->sum_of_counters;
}

/** Returns TRUE if the sum of counters of cf1 is greater or equal than the sum of counters of cf2, FALSE otherwise **/
char compare_counters_sum_of_counting_functions_reverse(counting_function *cf1, counting_function *cf2) {
	return cf1->sum_of_counters >= cf2->sum_of_counters;
}

/** Returns TRUE if cf1 and cf2 are equal, FALSE otherwise **/
char
are_counting_functions_equal(counting_function *cf1, counting_function *cf2) {
	/*	if(cf1->player != cf2->player) { //Impossible?
		printf("Comparison of incompatible counting functions!\n");
		exit(0);
	}*/
	// To be equal, cf1 and cf2 must have the same sum of counters. If not, we know that they are not equal
	if(cf1->sum_of_counters != cf2->sum_of_counters || cf1->max_counter != cf2->max_counter) {
		return FALSE;
	}

	cf_info* info = (cf_info*)(cf1->info)->data;
	int* start_index_other_p = info->start_index_other_p;
	int* cf_size = info->cf_size;
	int composition_size = info->composition_size;
	char starting;
	if(cf1->player == info->starting_player) {
		starting = TRUE;
	}
	else {
		starting = FALSE;
	}
	int i, k, cur_start_index, cur_end_index, global_index=0;
	for(k=0; k<composition_size; k++) {
		if(starting == TRUE) {
			cur_start_index = 0;
			cur_end_index = start_index_other_p[k];
		}
		else {
			cur_start_index = start_index_other_p[k];
			cur_end_index = cf_size[k];
		}
		for(i=cur_start_index; i<cur_end_index; i++) {
			if((cf1->mapping[global_index+i] != cf2->mapping[global_index+i])) {
				return FALSE;
			}
		}
		global_index += cf_size[k];
	}
	return TRUE;
}


/** Computes the intersection of two counting_functions (by applying the logical-AND operator on these counting functions) **/
counting_function*
compute_counting_functions_intersection(counting_function *cf1, counting_function *cf2) {
	cf_info* info = (cf_info*)(cf1->info)->data;

	counting_function *inters = (counting_function*)malloc(sizeof(counting_function));
	inters->player = cf1->player;
	inters->info = cf1->info;
	inters->mapping = (int*)malloc((info->cf_size_sum)*sizeof(int));

	int* start_index_other_p = info->start_index_other_p;
	int* cf_size = info->cf_size;
	int composition_size = info->composition_size;
	char starting;
	if(cf1->player == info->starting_player) {
		starting = TRUE;
	}
	else {
		starting = FALSE;
	}
	int i, k, cur_start_index, cur_end_index, global_index=0;
	for(k=0; k<composition_size; k++) {
		if(starting == TRUE) {
			cur_start_index = 0;
			cur_end_index = start_index_other_p[k];
		}
		else {
			cur_start_index = start_index_other_p[k];
			cur_end_index = cf_size[k];
		}
		for(i=cur_start_index; i<cur_end_index; i++) {
			inters->mapping[global_index+i] = MIN(cf1->mapping[global_index+i], cf2->mapping[global_index+i]);
		}
		global_index += cf_size[k];
	}
	set_max_counter_and_sum_of_counters_of_counting_function(inters);

	return inters;
}


/** Computes the predecessor of cf for label indexed label_index in sigma **/
counting_function*
counting_function_omega(counting_function *cf, int label_index, LABEL_BIT_REPRES **sigma) {
	cf_info* info = (cf_info*)cf->info->data;
	counting_function* omega = build_maximal_counting_function(switch_player(cf->player), cf->info);
	int* start_index_other_p = info->start_index_other_p;
	int* cf_size = info->cf_size;
	tbucw** automaton = info->automaton;
	int* k_value = info->k_value;
	int composition_size = info->composition_size;
	char starting;
	if(cf->player == info->starting_player) {
		starting = TRUE;
	}
	else {
		starting = FALSE;
	}
	int i, k, cur_start_index, cur_end_index, global_index=0;
	for(k=0; k<composition_size; k++) {
		if(starting == TRUE) {
			cur_start_index = 0;
			cur_end_index = start_index_other_p[k];
		}
		else {
			cur_start_index = start_index_other_p[k];
			cur_end_index = cf_size[k];
		}
		for(i=cur_start_index; i<cur_end_index; i++) {
			if(is_accepting(automaton[k], i)) {
				update_counting_function_omega(k, i, global_index, MAX(-1, cf->mapping[global_index+i]-1), sigma[label_index], omega, info);
			}
			else {
				update_counting_function_omega(k, i, global_index, cf->mapping[global_index+i], sigma[label_index], omega, info);
			}

		}
		global_index += cf_size[k];
	}
	set_max_counter_and_sum_of_counters_of_counting_function(omega);

	return omega;
}


/** Computes and adds the predecessors of s to omega (bits level) **/
static counting_function*
update_counting_function_omega(int comp_index, int state_index, int global_index, int k, LABEL_BIT_REPRES *label, counting_function *omega, cf_info *info) {
	tbucw* automaton = info->automaton[comp_index];

	int *pred_list = get_pred(automaton, state_index, label);
	int i;
	for(i=1; i<=pred_list[0]; i++) {
		if(is_accepting(automaton, pred_list[i]) == TRUE && k == 0) {//Optimization 1: if tbucw_state is final, it cannot map 0, so it maps -1
			omega->mapping[global_index+pred_list[i]] = -1;
		}
		else {
			omega->mapping[global_index+pred_list[i]] = MIN(omega->mapping[global_index+pred_list[i]] ,k);
		}
	}

	free(pred_list);

	return omega;
}

/** Computes the successor of a counting function for the label indexed label_index **/
counting_function*
counting_function_succ(counting_function *cf, int label_index, alphabet_info *alphabet) {
	cf_info* info = (cf_info*)cf->info->data;
	counting_function* succ = new_counting_function(switch_player(cf->player), cf->info);
	int* start_index_other_p = info->start_index_other_p;
	int* cf_size = info->cf_size;
	tbucw** automaton = info->automaton;
	int composition_size = info->composition_size;
	char starting;
	if(cf->player == info->starting_player) {
		starting = TRUE;
	}
	else {
		starting = FALSE;
	}
	LABEL_BIT_REPRES **sigma;
	if(cf->player == P_O) {
		sigma = alphabet->sigma_output;
	}
	else {
		sigma = alphabet->sigma_input;
	}
	int i, k, cur_start_index, cur_end_index, global_index=0;
	for(k=0; k<composition_size; k++) {
		if(starting == TRUE) {
			cur_start_index = 0;
			cur_end_index = start_index_other_p[k];
		}
		else {
			cur_start_index = start_index_other_p[k];
			cur_end_index = cf_size[k];
		}
		for(i=cur_start_index; i<cur_end_index; i++) {
			if(cf->mapping[global_index+i] > -1) {
				update_counting_function_succ(k, i, global_index, cf->mapping[global_index+i], sigma[label_index], succ, info);
			}
		}
		global_index += cf_size[k];
	}
	set_max_counter_and_sum_of_counters_of_counting_function(succ);

	return succ;
}


/** Computes and adds the successors of s to succ (bits level) **/
static counting_function*
update_counting_function_succ(int comp_index, int state_index, int global_index, int pred_k, LABEL_BIT_REPRES *label, counting_function *succ, cf_info* info) {
	tbucw* automaton = info->automaton[comp_index];
	int max_k = info->k_value[comp_index];
	int *succ_list = get_succ(automaton, state_index, label);
	int i;
	for(i=1; i<=succ_list[0]; i++) {
		if(is_accepting(automaton, succ_list[i])) {
			succ->mapping[global_index+succ_list[i]] = MAX(succ->mapping[global_index+succ_list[i]], MIN(max_k+1, pred_k+1));
		}
		else {
			succ->mapping[global_index+succ_list[i]] = MAX(succ->mapping[global_index+succ_list[i]], pred_k);
		}
	}
	free(succ_list);
	return succ;
}

/** Computes the antichain of minimal successor of cf for all sigma in the player alphabet **/
antichain*
minimal_counting_function_succ(counting_function *cf, alphabet_info *alphabet) {
	GList *minimal_elements = NULL;
	int sigma_size, i, result_size = 0;
	if(cf->player == P_O) {
		sigma_size = alphabet->sigma_output_size;
	}
	else {
		sigma_size = alphabet->sigma_input_size;
	}

	counting_function *cur_succ;
	for(i=0; i<sigma_size; i++) {
		cur_succ = counting_function_succ(cf, i, alphabet);
		minimal_elements = scan_add_or_remove_and_free(minimal_elements, cur_succ, (void*)compare_counting_functions_reverse, (void*)free_counting_function);
	}

	antichain* minimal_succ = (antichain*)malloc(sizeof(antichain));
	minimal_succ->size = g_list_length(minimal_elements);
	minimal_succ->incomparable_elements = minimal_elements;

	return minimal_succ;
}

/** Computes the antichain of maximal successor of cf for all sigma in the player alphabet **/
antichain*
maximal_counting_function_succ(counting_function *cf, alphabet_info *alphabet) {
	GList *maximal_elements = NULL;
	int sigma_size, i, result_size = 0;
	if(cf->player == P_O) {
		sigma_size = alphabet->sigma_output_size;
	}
	else {
		sigma_size = alphabet->sigma_input_size;
	}

	counting_function *cur_succ;
	for(i=0; i<sigma_size; i++) {
		cur_succ = counting_function_succ(cf, i, alphabet);
		maximal_elements = scan_add_or_remove_and_free(maximal_elements, cur_succ, (void*)compare_counting_functions, (void*)free_counting_function);
	}

	antichain* maximal_succ = (antichain*)malloc(sizeof(antichain));
	maximal_succ->size = g_list_length(maximal_elements);
	maximal_succ->incomparable_elements = maximal_elements;

	return maximal_succ;
}

/** Frees a counting_function
 	Warning: it does not free the GNode info of cf, as it is shared by several counting functions **/
void
free_counting_function(counting_function *cf) {
	free_memory(cf->mapping);
	free_memory(cf);
}

/** Frees a cf_info struct **/
void
free_cf_info(cf_info *info) {
	free(info->k_value);
	free(info->cf_size);
	free(info->start_index_other_p);
	int i;
	for(i=0; i<info->composition_size; i++) {
		free_tbucw(info->automaton[i]);
	}
	free(info->automaton);
	free(info);
}

/** Prints cf **/
void
print_counting_function(counting_function *cf) {
	cf_info* info = (cf_info*)cf->info->data;
	int* start_index_other_p = info->start_index_other_p;
	int* cf_size = info->cf_size;
	tbucw** automaton = info->automaton;
	int* k_value = info->k_value;
	int composition_size = info->composition_size;
	char starting;
	if(cf->player == info->starting_player) {
		starting = TRUE;
	}
	else {
		starting = FALSE;
	}

	int i, k, cur_start_index, cur_end_index, global_index=0;
	printf("Player %d, Sum %d, Max %d, (", cf->player, cf->sum_of_counters, cf->max_counter);
	for(k=0; k<composition_size; k++) {
		printf("(");
		if(starting == TRUE) {
			cur_start_index = global_index+0;
			cur_end_index = global_index+start_index_other_p[k];
		}
		else {
			cur_start_index = global_index+start_index_other_p[k];
			cur_end_index = global_index+cf_size[k];
		}
		for(i=cur_start_index; i<cur_end_index; i++) {
			printf("(%d -> %d)", i+1, cf->mapping[i]);
		}
		global_index += cf_size[k];
		printf(")");
	}
	printf(")");
}

/** Prints the information of a counting function **/
void
print_cf_info(cf_info *info) {
	printf("starting_player %d\n", info->starting_player);
	printf("cf_size_sum %d\n", info->cf_size_sum);
	printf("composition_size %d\n", info->composition_size);
	int i;
	for(i=0; i<info->composition_size; i++) {
		printf("Automaton %d\n", i+1);
		printf("	k_value %d\n", info->k_value[i]);
		printf("	cf_size %d\n", info->cf_size[i]);
		printf("	start_index_other_p %d\n", info->start_index_other_p[i]);
		print_tbucw(info->automaton[i]);
		printf("\n");
	}

}

/** Creates a new counting function corresponding to the composition of cfs **/
counting_function*
compose_counting_functions(counting_function **cfs, int nb_cfs, GNode *composition_info) {
	counting_function* comp = (counting_function*)malloc(sizeof(counting_function));
	cf_info *info = ((cf_info*)((composition_info)->data));
	comp->player = cfs[0]->player;
	comp->info = composition_info;
	comp->mapping = (int*)malloc((info->cf_size_sum)*sizeof(int));

	int i, j, cf_size_sum, comp_index=0;
	for(i=0; i<nb_cfs; i++) {
		cf_size_sum = ((cf_info*)(cfs[i]->info->data))->cf_size_sum;
		for(j=0; j<cf_size_sum; j++) {
			comp->mapping[comp_index] = cfs[i]->mapping[j];
			comp_index++;
		}
	}
	set_max_counter_and_sum_of_counters_of_counting_function(comp);

	return comp;
}


/** Creates a new GNode containing information about the composition of cfs_info **/
GNode*
compose_cf_info(GNode **cfs_info, int nb_cfs) {
	int i;
	cf_info* info = (cf_info*)malloc(sizeof(cf_info));
	info->starting_player = ((cf_info*)(cfs_info[0]->data))->starting_player;
	info->cf_size_sum = 0;
	info->composition_size = 0;
	for(i=0; i<nb_cfs; i++) {
		info->cf_size_sum += ((cf_info*)(cfs_info[i]->data))->cf_size_sum;
		info->composition_size += ((cf_info*)(cfs_info[i]->data))->composition_size;
	}
	info->k_value = (int*)malloc((info->composition_size)*sizeof(int));
	info->cf_size = (int*)malloc((info->composition_size)*sizeof(int));
	info->start_index_other_p = (int*)malloc((info->composition_size)*sizeof(int));
	info->automaton = (tbucw**)malloc((info->composition_size)*sizeof(tbucw*));

	int local_index=0, cur_cf_info=0;
	cf_info* cur_info;
	for(i=0; i<info->composition_size; i++) {
		cur_info = cfs_info[cur_cf_info]->data;

		info->k_value[i] = cur_info->k_value[local_index];
		info->cf_size[i] = cur_info->cf_size[local_index];
		info->start_index_other_p[i] = cur_info->start_index_other_p[local_index];
		info->automaton[i] = cur_info->automaton[local_index];

		local_index++;

		// Skip to the next cf_info
		if(local_index == cur_info->composition_size) {
			cur_cf_info += 1;
			local_index = 0;
		}
	}

	//Create the GNode and add its sons
	GNode *cfinfo = g_node_new(info);
	for(i=0; i<nb_cfs; i++) {
		g_node_insert(cfinfo, -1, cfs_info[i]);
	}

	return cfinfo;
}
