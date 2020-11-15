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

#include "backward_algorithm.h"

/** Builds the initial antichain for the fix point computation where player starts **/
antichain*
build_start_antichain(char player, GNode* info, int dimension, int *max_credit) {
	antichain* a_init = (antichain*)malloc(sizeof(antichain));
	a_init->size = 1;
	GList *list = NULL;
	list = g_list_append(list, build_maximal_tuple(player, info, dimension, max_credit));
	a_init->incomparable_elements = list;

	return a_init;
}

/** Calls pre_O or pre_I function according to the player value **/
antichain*
pre(antichain* a, antichain* prev_a, char player, alphabet_info *alphabet) {
	if(player == P_O) {
		return pre_O(a, prev_a, alphabet);
	}
	else {
		return pre_I(a, prev_a, alphabet);
	}
}

/** Computes the function Pre_O(a)
 	prev_a is the last antichain of P_O computed
 	Uses a cache memory to prevent to recompute already computed antichains **/
static antichain*
pre_O(antichain* a, antichain* prev_a, alphabet_info *alphabet) {
	// If the antichain is empty, the fix point is reached
    if(a->incomparable_elements == NULL) {
		return clone_antichain(a, (void*)clone_tuple);
    }

    antichain *pre, *cur_antichain;
    tuple *cur_omega;
    GList *curlink = a->incomparable_elements;
    int j, sigma_size = alphabet->sigma_output_size;
    char first_iteration = TRUE;
    LABEL_BIT_REPRES **sigma = alphabet->sigma_output;

	while(curlink != NULL) {
		//Check whether we have already computed the antichain of predecessors for curlink->data
		cur_antichain = (antichain*)get_from_cache(cache_antichains, curlink->data, -1);
		if(cur_antichain == NULL) { //cache miss -> compute cur_antichain
			cur_antichain = new_antichain();
			for(j=0; j<sigma_size; j++) {
				cur_omega = tuple_omega((tuple*)(curlink->data), j, sigma);

				if(cur_omega != NOT_DEFINED) {
					//Add to cur_antichain
					add_element_to_antichain_and_free(cur_antichain, cur_omega, (void*)compare_tuples, (void*)free_tuple_full);
				}
			}
			add_to_cache(cache_antichains, clone_tuple(curlink->data), -1, (void*)cur_antichain); //add the computed antichain to cache
		}

		if(first_iteration == TRUE) {
			//Clone the antichain because cur_antichain (which is stored in cache) must be a constant pointer (constant = never modified)
			pre = clone_antichain(cur_antichain, (void*)clone_tuple);
			first_iteration = FALSE;
		}
		else {
			pre = compute_antichains_union(pre, clone_antichain(cur_antichain, (void*)clone_tuple), (void*)compare_tuples, (void*)free_tuple_full);
		}
		curlink = curlink->next;
	}

    antichain *inters = compute_antichains_intersection(pre, prev_a, (void*)compare_tuples, (void*)compute_tuples_intersection, (void*)clone_tuple, (void*)free_tuple_full);
    free_antichain_full(pre, (void*)free_tuple_full);

    return inters;
}

/** Computes the function Pre_I(a)
 	prev_a is the last antichain of P_I computed
 	Uses a cache memory to prevent to recompute already computed omega  **/
static antichain*
pre_I(antichain* a, antichain* prev_a, alphabet_info *alphabet) {
	// If the antichain is empty, the fix point is reached
	if(a->incomparable_elements == NULL) {
		return clone_antichain(a, (void*)clone_tuple);
	}

	antichain *pre, *cur_antichain;
	tuple *cur_omega;
	GList *antichains_list = NULL;
	GList *curlink;
	int i, sigma_size = alphabet->sigma_input_size;
	LABEL_BIT_REPRES **sigma = alphabet->sigma_input;

	for(i=0; i<sigma_size; i++) {
		cur_antichain = new_antichain();
		curlink = a->incomparable_elements;
		while(curlink != NULL) {
			//Check whether we have already computed omega for curlink->data and sigma i
			cur_omega = (tuple*)get_from_cache(cache_tuples, curlink->data, i);
			if(cur_omega == NULL) { //cache miss
				cur_omega = tuple_omega(curlink->data, i, sigma);
				add_to_cache(cache_tuples, clone_tuple(curlink->data), i, (void*)cur_omega); //add the computed tuple to cache
			}
			if(cur_omega != NOT_DEFINED) {
				//Add to cur_antichain
				add_element_to_antichain(cur_antichain, cur_omega, (void*)compare_tuples); // no need to free as all omega computed are stored in cache
			}
			curlink = curlink->next;
		}
		antichains_list = scan_add_or_remove_and_sort_and_free(antichains_list, cur_antichain, (void*)compare_antichains, (void*)compare_tuples, (void*)compare_antichains_size, (void*)free_antichain);
	}
	if(antichains_list != NULL) {
		antichain* prev_pre;
		pre = clone_antichain(antichains_list->data, (void*)clone_tuple);
		curlink = antichains_list->next;
		while(curlink != NULL) {
			prev_pre = pre;
			pre = compute_antichains_intersection(prev_pre, curlink->data, (void*)compare_tuples, (void*)compute_tuples_intersection, (void*)clone_tuple, (void*)free_tuple_full);
			free_antichain(curlink->data);
			free_antichain_full(prev_pre, (void*)free_tuple_full);
			curlink = curlink->next;
		}
		g_list_free(antichains_list);
	}
	else { // impossible (?)
		printf("antichains_list empty!!!");
	}

	return pre;
}

/** Computes the function Pre_crit(a) (critical signals optimization) where critical_signals_index is the set of critical
    signals for a (the first element of the array is the number of critical signals)
    prev_a is the last antichain of the input computed
 	Uses a cache memory to prevent to recompute already computed omega                                                    **/
antichain*
pre_crit(antichain* a, antichain* prev_a, int *critical_signals_index, alphabet_info *alphabet) {
	int nb_critical_signals = critical_signals_index[0];

    // If the set of critical signal or the antichain is empty, the fixpoint is reached
    if(nb_critical_signals == 0 || a->incomparable_elements == NULL) {
		 return clone_antichain(prev_a, (void*)clone_tuple);
    }

    antichain *pre, *cur_antichain;
    tuple *cur_omega;
    GList *antichains_list = NULL;
    GList *curlink;
    int i;
    LABEL_BIT_REPRES **sigma = alphabet->sigma_input;

	for(i=1; i<=nb_critical_signals; i++) {
		cur_antichain = new_antichain();
		curlink = a->incomparable_elements;
		while(curlink != NULL) {
			//Check whether we have already computed omega for curlink->data and sigma i
			cur_omega = (tuple*)get_from_cache(cache_tuples, curlink->data, critical_signals_index[i]);
			if(cur_omega == NULL) { //cache miss
				cur_omega = tuple_omega(curlink->data, critical_signals_index[i], sigma);
				add_to_cache(cache_tuples, clone_tuple(curlink->data), critical_signals_index[i], (void*)cur_omega); //add the computed tuple to cache
			}
			if(cur_omega != NOT_DEFINED) {
				//Add to antichain
				add_element_to_antichain(cur_antichain, cur_omega, (void*)compare_tuples); // no need to free as all omega computed are stored in cache
			}
			curlink = curlink->next;
		}
		antichains_list = scan_add_or_remove_and_sort_and_free(antichains_list, cur_antichain, (void*)compare_antichains, (void*)compare_tuples, (void*)compare_antichains_size, (void*)free_antichain);
	}

	if(antichains_list != NULL) {
		antichain* prev_pre;
		pre = clone_antichain(antichains_list->data, (void*)clone_tuple);
		curlink = antichains_list->next;
		while(curlink != NULL) {
			prev_pre = pre;
			pre = compute_antichains_intersection(prev_pre, curlink->data, (void*)compare_tuples, (void*)compute_tuples_intersection, (void*)clone_tuple, (void*)free_tuple_full);
			free_antichain(curlink->data);
			free_antichain_full(prev_pre, (void*)free_tuple_full);
			curlink = curlink->next;
		}
		g_list_free(antichains_list);
	}
	else { // impossible (?)
		printf("antichains_list empty!!!");
	}

	return pre;
}

/** Critical input signals optimization: Computes a minimal critical set of inputs signals for antichain
    For each element f of antichain, for each symbol s_O of the output alphabet, find a critical input signal,
    i.e. a signal s_I s.t. succ(succ(f, s_O), s_I) does not belong to antichain (+ privilege already added signals to try to minimize the set)
    Stops when the critical signals for one one-step-loosing tuple is found															**/
int*
compute_critical_set(antichain *a, alphabet_info *alphabet) {
//  Note: local cache memories optimization disabled since it seems less efficient
//	GHashTable *cache_lvl1 = g_hash_table_new_full(hash_key, compare_keys, (GDestroyNotify)free_hash_table_key, NULL); //cache memory used to avoid redondant computation (for the successors of t)
//	GHashTable *cache_lvl2 = g_hash_table_new_full(hash_key, compare_keys, (GDestroyNotify)free_hash_table_key, NULL); //cache memory used to avoid redondant computation (for the successors of the successors of t)

	char found, inC, is_one_step_loosing, is_in_antichain;
	char *info_lvl1, *info_lvl2;
	char *true_value = malloc(sizeof(char));
	true_value[0] = TRUE;
	char *false_value = malloc(sizeof(char));
	false_value[0] = FALSE;

	int i, input_size = alphabet->sigma_input_size;
	tuple *t, *succ_t, *succ_succ_t;
	antichain *min_succ;

	int* c = (int*)malloc((input_size+1)*sizeof(int)); //array of critical signals index in the input alphabet
	char* c_bool = (char*)malloc((input_size)*sizeof(char)); //array of booleans representing the critical set on a boolean format
	int* curC = (int*)malloc((input_size+1)*sizeof(int)); //array of signals found to be critical till here and for t (will be added to c only if t is one step loosing)
	char* curC_bool = (char*)malloc((input_size)*sizeof(char)); //c_bool but on a boolean format

	// Initialize c and c_bool
	c[0] = 0;
	for(i=0; i<input_size; i++) {
		c[i+1] = -1;
		c_bool[i] = FALSE;
	}

	GList *succ_t_link, *cur_link = a->incomparable_elements;
	while(cur_link != NULL) { //for each element t of the antichain
		t = cur_link->data;

		//Copy c and c_bool in curC and curC_bool
		curC[0] = c[0];
		for(i=0; i<input_size; i++) {
			curC[i+1] = c[i+1];
			curC_bool[i] = c_bool[i];
		}

		is_one_step_loosing = TRUE;
		min_succ = (antichain*)get_from_cache(cache_min_succ, t, -1);
		if(min_succ == NULL) { //cache miss
			min_succ = minimal_tuple_succ(t, alphabet); //compute the antichain of minimal successors of t (no need of analyzing non minimal successors)
			add_to_cache(cache_min_succ, clone_tuple(t), -1, (void*)min_succ); //add the computed antichain to cache
		}
		succ_t_link = min_succ->incomparable_elements;
		while(succ_t_link != NULL) { //for all succ of t s.t. succ is minimal
			found = FALSE; //will be set to TRUE if a critical s_I is found for t
			inC = FALSE; //will be set to True if the critical signal s_I found has already been for another t or min_succ
			succ_t = clone_tuple(succ_t_link->data);

			//Seek on cache if succ_t has already been processed, i.e. if we have already checked if there exists a s_I s.t. succ(succ_t, s_I) does not belong to the antichain
			//info_lvl1 = get_from_cache(cache_lvl1, succ_t, -1); //attempt to retrieve data from cache_lvl1
			//if(info_lvl1 == NULL) { //cache miss
				for(i=1; i<=curC[0]; i++) { //for s_I in c (privilege already added signals)
					succ_succ_t = (tuple*)get_from_cache(cache_succ, succ_t, curC[i]);
					if(succ_succ_t == NULL) { // cache miss
						succ_succ_t = tuple_succ(succ_t, curC[i], alphabet);
						add_to_cache(cache_succ, clone_tuple(succ_t), curC[i], (void*)succ_succ_t); //add the computed succ to cache
					}

					//Seek on cache if succ_succ_t has already been processed
					//info_lvl2 = get_from_cache(cache_lvl2, succ_succ_t, -1); //attempt to retrieve data from cache_lvl2
					//if(info_lvl2 == NULL) { //cache miss
						is_in_antichain = contains_element(a, succ_succ_t, (void*)compare_tuples);
						/*if(is_in_antichain == TRUE) {
							add_to_cache(cache_lvl2, clone_tuple(succ_succ_t), -1, true_value); //add the computed result to cache_lvl2
						}
						else {
							add_to_cache(cache_lvl2, clone_tuple(succ_succ_t), -1, false_value); //add the computed result to cache_lvl2
						}
					}
					else {
						//free_tuple_full(succ_succ_t);
						is_in_antichain = info_lvl2[0];
					}*/

					if(is_in_antichain == FALSE) {
						inC = TRUE;
						break;
					}
				}

				if(inC == FALSE) { //if not found yet
					for(i=0; i<input_size; i++) { //for s_I in input alphabet
						if(curC_bool[i] == FALSE) { //if s_I does not belong to curC (otherwise it has already been tested)
							succ_succ_t = (tuple*)get_from_cache(cache_succ, succ_t, i);
							if(succ_succ_t == NULL) { // cache miss
								succ_succ_t = tuple_succ(succ_t, i, alphabet);
								add_to_cache(cache_succ, clone_tuple(succ_t), i, (void*)succ_succ_t); //add the computed succ to cache
							}

							//Seek on cache if succ_succ_t has already been processed
							//info_lvl2 = get_from_cache(cache_lvl2, succ_succ_t, -1); //attempt to retrieve data from cache_lvl2
							//if(info_lvl2 == NULL) { //cache miss
								is_in_antichain = contains_element(a, succ_succ_t, (void*)compare_tuples);
								/*if(is_in_antichain == TRUE) {
									add_to_cache(cache_lvl2, clone_tuple(succ_succ_t), -1, true_value); //add the computed result to cache_lvl2
								}
								else {
									add_to_cache(cache_lvl2, clone_tuple(succ_succ_t), -1, false_value); //add the computed result to cache_lvl2
								}
							}
							else {
								//free_tuple_full(succ_succ_t);
								is_in_antichain = info_lvl2[0];
							}*/

							if(is_in_antichain == FALSE) {
								found = TRUE; //there exists a s_I s.t. succ(succ(f, s_O), s_I) does not belong to antichain
								curC[0] = curC[0]+1; //increment the number of critical signals
								curC[curC[0]] = i; //add s_I to curC
								curC_bool[i] = TRUE; //set the boolean of s_I to True
								break;
							}
						}
					}
				}

				if(found == FALSE && inC == FALSE) { //there exists a s_O s.t. for all s_I succ(succ(t, s_O), s,I) belongs to antichain -> t is not one step loosing
					is_one_step_loosing = FALSE;
				}
				/*if(is_one_step_loosing == TRUE) {
					add_to_cache(cache_lvl1, clone_tuple(succ_t), -1, true_value); //add the computed result to cache_lvl1
				}
				else {
					add_to_cache(cache_lvl1, clone_tuple(succ_t), -1, false_value); //add the computed result to cache_lvl1
				}
			}
			else { //cache hit
				free_tuple_full(succ_t);
				is_one_step_loosing = info_lvl1[0];
			}*/

			if(is_one_step_loosing == FALSE) {
				break; //not one step loosing -> skip to the next t
			}

			succ_t_link = succ_t_link->next;
		}

		//free_antichain_full(min_succ, (void*)free_tuple_full); // Not freed here because min_succ is added in cache (will be freed at the end on the fix point computation)

		// If f is one step loosing, add signals in curC to c
		if(is_one_step_loosing == TRUE) {
			c[0] = curC[0];
			for(i=0; i<input_size; i++) {
				c[i+1] = curC[i+1];
				c_bool[i] = curC_bool[i];
			}
			break; //break here because we found critical signals for one one-step-loosing tuple (enough)
		}

		cur_link = cur_link->next;
	}
	free(c_bool);
	free(curC);
	free(curC_bool);
	free(true_value);
	free(false_value);

//	g_hash_table_destroy(cache_lvl1);
//	g_hash_table_destroy(cache_lvl2);

	return c;
}

