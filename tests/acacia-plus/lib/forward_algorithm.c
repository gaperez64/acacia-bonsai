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

#include "forward_algorithm.h"

/** OTFUR algorithm **/
otfur_result*
otfur(antichain *safety_game_PO, antichain *safety_game_PI, GNode* cfinfo, alphabet_info *alphabet, char starting_player, int dimension, int* max_credit) {
	clock_t start_time = clock();

	// Structures initialization
	GHashTable *succ_to_visit = g_hash_table_new_full(hash_key, compare_keys, free, NULL);
	GHashTable *passed = g_hash_table_new_full(hash_key, compare_keys, (GDestroyNotify)free_hash_table_key, free);
	GHashTable *depend = g_hash_table_new_full(hash_key, compare_keys, free, NULL);
	//GList *trash = NULL;

	GList *waiting = NULL;
	antichain *losing_PO = new_antichain();
	antichain *losing_PI = new_antichain();

	// Build initial tuple
	tuple *s_ini = build_initial_tuple(cfinfo, dimension, max_credit);

	// if s_ini belongs to the system and all its successors are unsafe, s_ini is losing
	if((starting_player == P_O && has_successor_in_safety_game(s_ini, alphabet, safety_game_PI) == FALSE)/* ||
			(starting_player == P_I && has_successor_not_in_safety_game(s_ini, alphabet, safety_game_PO) == TRUE)*/) { // To avoid k differences between forward and backward algorithms
		add_to_losing(s_ini, losing_PO, losing_PI);
	} // else, explore its successors
	else {
		waiting = add_to_waiting(s_ini, alphabet, safety_game_PO, safety_game_PI, losing_PO, losing_PI, waiting, succ_to_visit, passed);
		add_to_passed(s_ini, passed);
	}

	safety_game_edge *cur_edge;
	char cur_losing;
	int nb_cf_passed = 1;
	int nb_iter = 0;
	antichain* cur_safety_game;
	while(waiting != NULL && is_losing(s_ini, losing_PO, losing_PI) == FALSE) {
		nb_iter++;
		GList *last_link = g_list_last(waiting);
		cur_edge = (safety_game_edge*)last_link->data;
		waiting = remove_last(waiting);
		if(is_losing(cur_edge->from, losing_PO, losing_PI) == FALSE) { // If s is not losing
			if(is_passed(cur_edge->to, passed) == FALSE) { // If s' is not passed
				add_to_passed(cur_edge->to, passed);
				nb_cf_passed++;
				if(is_losing(cur_edge->to, losing_PO, losing_PI) == TRUE) { // If s' is losing -> add e for reevaluation
					//waiting = g_list_append(waiting, clone_safety_game_edge(cur_edge));
					waiting = g_list_append(waiting, cur_edge);
				}
				// Basic case: if s' belongs to P_O and has no successor safe non losing, add it to losing
				// Put the same test for P_I to avoid k differences between forward and backward algorithms
				else if(cur_edge->to->cf->player == P_O && has_successor_non_losing_in_safety_game(cur_edge->to, alphabet, losing_PO, losing_PI, safety_game_PI) == FALSE) {
					//waiting = g_list_append(waiting, clone_safety_game_edge(cur_edge));
					waiting = g_list_append(waiting, cur_edge);
					add_to_losing(cur_edge->to, losing_PO, losing_PI);
				}
				else { // else, explore its successors
					add_to_depend(cur_edge->to, cur_edge, depend);
					waiting = add_to_waiting(cur_edge->to, alphabet, safety_game_PO, safety_game_PI, losing_PO, losing_PI, waiting, succ_to_visit, passed);
				}
			}
			else { // s' is passed
				if(cur_edge->from->cf->player == P_O) { // s belongs to P_O
					if(is_losing(cur_edge->to, losing_PO, losing_PI) == FALSE) { // if s' is not losing, add e to the dependencies of s'
						add_to_depend(cur_edge->to, cur_edge, depend);
					}
					else { // if s' is losing, and if there is no more successor of s non losing to visit, s is losing -> add its dependencies to waiting
						if(has_one_succ_to_visit_non_losing(cur_edge->from, succ_to_visit, losing_PO, losing_PI) == FALSE) {
							add_to_losing(cur_edge->from, losing_PO, losing_PI);
							waiting = g_list_concat(waiting, get_dependencies(cur_edge->from, depend));
						}
						else { // if there is still at least a successor non passed non losing, add it to waiting
							waiting = g_list_append(waiting, get_first_successor_passed_non_losing(cur_edge->from, succ_to_visit, passed, losing_PO, losing_PI));
						}
					}
				}
				else { // s belongs to P_I
					// reevaluation
					if(is_losing(cur_edge->to, losing_PO, losing_PI) == TRUE) {
						cur_losing = TRUE;
					}
					else {
						cur_losing = reevaluation(cur_edge->from, alphabet, losing_PO, losing_PI);
					}

					// if s is losing, add its dependencies to waiting
					if(cur_losing == TRUE) {
						add_to_losing(cur_edge->from, losing_PO, losing_PI);
						waiting = g_list_concat(waiting, get_dependencies(cur_edge->from, depend));
					}

					// if s' is not losing, add e to its dependencies list
					if(is_losing(cur_edge->to, losing_PO, losing_PI) == FALSE) {
						add_to_depend(cur_edge->to, cur_edge, depend);
					}
				}
				//trash = g_list_append(trash, cur_edge->to);
			}
		}
		else { // if s is losing, add its dependencies to waiting
			//free_tuple_full(cur_edge->to);
			waiting = g_list_concat(waiting, get_dependencies(cur_edge->from, depend));
		}
	}
	float otfur_time = (clock() - start_time) * 1e-6;

	// Extract the solution from passed and losing
	start_time = clock();
	safety_game* sg = compute_winning_positions(losing_PO, losing_PI, passed, build_initial_tuple(cfinfo, dimension, max_credit), alphabet, nb_cf_passed);
	float winning_positions_computation_time = (clock() - start_time) * 1e-6;

	// Free memory
	GList *curlink = waiting;
	//while(curlink != NULL) {
	//	free_safety_game_edge(curlink->data);
	//	curlink = curlink->next;
	//}
	g_list_free(waiting);
	/*curlink = trash;
	while(curlink != NULL) {
		free_tuple_full(curlink->data);
		curlink = curlink->next;
	}
	g_list_free(trash);*/
	//g_hash_table_destroy(passed); // this one is not correct !
	g_hash_table_foreach(depend, free_depend, NULL);
	g_hash_table_destroy(depend);
	g_hash_table_foreach(succ_to_visit, free_succ_to_visit, NULL);
	g_hash_table_destroy(succ_to_visit);

	// Build result
	otfur_result *res = (otfur_result*)malloc(sizeof(otfur_result));
	res->winning_positions = sg;
	res->otfur_time = otfur_time;
	res->winning_positions_computation_time = winning_positions_computation_time;
	res->nb_cf_passed = nb_cf_passed;
	res->nb_iter = nb_iter;

	return res;
}


/** Adds t to losing **/
static void
add_to_losing(tuple* t, antichain* losing_PO, antichain* losing_PI) {
	if(t->cf->player == P_O) {
		add_element_to_antichain(losing_PO, t, (void*)compare_tuples_reverse);
	}
	else {
		add_element_to_antichain(losing_PI, t, (void*)compare_tuples_reverse);
	}
}

/** Returns TRUE if t is losing **/
static char
is_losing(tuple* t, antichain* losing_PO, antichain* losing_PI) {
	char to_return;
	if(t->cf->player == P_O) {
		to_return = contains_element(losing_PO, t, (void*)compare_tuples_reverse);
	}
	else {
		to_return = contains_element(losing_PI, t, (void*)compare_tuples_reverse);
	}
	return to_return;
}

/** Returns TRUE if t has a successor non losing in safety game, FALSE otherwise
 	Note that t is supposed to be an antichain of P_O -> no test is done here    **/
static char
has_successor_non_losing_in_safety_game(tuple* t, alphabet_info* alphabet, antichain* losing_PO, antichain* losing_PI, antichain* safety_game_PI) {
	int i, sigma_size = alphabet->sigma_output_size;
	tuple* cur_succ;
	for(i=0; i<sigma_size; i++) {
		cur_succ = tuple_succ(t, i, alphabet);
		if((contains_element(safety_game_PI, cur_succ, (void*)compare_tuples) == TRUE) && (is_losing(cur_succ, losing_PO, losing_PI) == FALSE)) {
			free_tuple_full(cur_succ);
			return TRUE;
		}
		free_tuple_full(cur_succ);
	}
	return FALSE;
}

/** Returns TRUE if t has a successor in the safety game, FALSE otherwise
  	Note that t is supposed to be an antichain of P_O -> no test is done here **/
static char
has_successor_in_safety_game(tuple* t, alphabet_info* alphabet, antichain* safety_game_PI) {
	int i, sigma_size = alphabet->sigma_output_size;
	tuple* cur_succ;
	for(i=0; i<sigma_size; i++) {
		cur_succ = tuple_succ(t, i, alphabet);
		if(contains_element(safety_game_PI, cur_succ, (void*)compare_tuples) == TRUE) {
			free_tuple_full(cur_succ);
			return TRUE;
		}
		free_tuple_full(cur_succ);
	}
	return FALSE;
}

/** Method used to free the elements stored in succ_to_visit hash_table **/
void
free_succ_to_visit(gpointer key, gpointer data, gpointer user_data) {
	GList* curlink = data;
	while(curlink != NULL) {
		free_safety_game_edge(curlink->data);
		curlink = curlink->next;
	}
	g_list_free(data);
}

/** Reevaluates the losing status of a state according to information of its successors
 	Note that t is supposed to be an antichain of P_I -> no test is done here          **/
static char
reevaluation(tuple *t, alphabet_info *alphabet, antichain *losing_PO, antichain *losing_PI) {
	int i, sigma_size = alphabet->sigma_input_size;
	tuple* cur_succ;
	for(i=0; i<sigma_size; i++) { //check if there is a successor losing of t
		cur_succ = tuple_succ(t, i, alphabet);
		if(is_losing(cur_succ, losing_PO, losing_PI) == TRUE) { //losing?
			free_tuple_full(cur_succ);
			return TRUE;
		}
		free_tuple_full(cur_succ);
	}
	return FALSE;
}

/** Adds a tuple to the passed GHashTable **/
static void
add_to_passed(tuple *t, GHashTable *passed) {
	hash_table_key* key = new_hash_table_key(t, -1);

	char *value = (char*)malloc(sizeof(char));
	value[0] = TRUE;

	g_hash_table_insert(passed, (gconstpointer*)key, (gconstpointer*)value);
}

/** Returns TRUE if t has already an entry in passed, FALSE otherwise **/
static char
is_passed(tuple* t, GHashTable *passed) {
	hash_table_key* key = new_hash_table_key(t, -1);

	char result;
	if(g_hash_table_lookup(passed, key) == NULL) {
		result = FALSE;
	}
	else {
		result = TRUE;
	}
	free(key);
	return result;
}

/** Adds e to the dependencies of t if not present yet **/
static void
add_to_depend(tuple* t, safety_game_edge* e, GHashTable *depend) {
	hash_table_key* key = new_hash_table_key(t, -1);

	// Try to retrieve the dependencies list of t in depend
	GList *value = (GList*)g_hash_table_lookup(depend, key);
	char already_in_depend = FALSE;
	GList *curlink = value;
	// Check if e is already in the dependencies list
	while(curlink != NULL) {
		if(are_tuples_equal(((safety_game_edge*)curlink->data)->from, e->from) == TRUE) {
			already_in_depend = TRUE;
			break;
		}
		curlink = curlink->next;
	}

	// Add t to the list and insert it to depend if not in yet
	if(already_in_depend == FALSE) {
		//value = g_list_append(value, clone_safety_game_edge(e)); // add e to the dependencies linked_list
		value = g_list_append(value, e); // add e to the dependencies linked_list
		g_hash_table_insert(depend, (gconstpointer*)key, (gconstpointer*)value);
	}
	else {
		free(key);
	}
}

/** Returns the dependencies list of t and removes all of it from depend **/
static GList*
get_dependencies(tuple *t, GHashTable *depend) {
	hash_table_key* key = new_hash_table_key(t, -1);

	// Try to retrieve the dependencies list of t in depend
	GList *value = (GList*)g_hash_table_lookup(depend, key);
	if(value != NULL) {
		// remove dependencies of t
		GList *newvalue = NULL;
		g_hash_table_insert(depend, (gconstpointer*)key, (gconstpointer*)newvalue);
	}
	else {
		free(key);
	}

	return value;
}

/** Method use to free the elements stored in depend hash_table **/
void
free_depend(gpointer key, gpointer data, gpointer user_data) {
	GList *curlink = data;
	while(curlink != NULL) {
		free(curlink->data);
		curlink = curlink->next;
	}
	g_list_free(data);
}

/** Adds to waiting list edges from t to each minimal (resp. maximal) successor of t, where t belongs to P_O (resp. P_I)
 	If t belongs to P_O: computes the minimal successors of t, sorts them and adds them to succ_to_visit[t]. The first successor passed in this list is then added to waiting and remove from succ_to_visit[t]
 	If t belongs to P_I: computes the maximal successors of t, sorts them and adds them to waiting (some of them might then be skipped if enough information is known) **/
static GList*
add_to_waiting(tuple *t, alphabet_info* alphabet, antichain* safety_game_PO, antichain* safety_game_PI, antichain* losing_PO, antichain* losing_PI, GList *waiting, GHashTable *succ_to_visit, GHashTable *passed) {
	char player = t->cf->player;
	int sigma_size;
	if(player == P_O) {
		sigma_size = alphabet->sigma_output_size;
	}
	else {
		sigma_size = alphabet->sigma_input_size;
	}

	// Compute the successors
	int i;
	GList *succ_list = NULL;
	safety_game_edge *cur_edge;
	for(i=0; i<sigma_size; i++) {
		cur_edge = (safety_game_edge*)malloc(sizeof(safety_game_edge));
		cur_edge->from = t;
		cur_edge->to = tuple_succ(t, i, alphabet);
		cur_edge->label_index = i;

		if(player == P_O) {
			if(contains_element(safety_game_PI, cur_edge->to, (void*)compare_tuples) == TRUE) { // succ safe?
				if(is_losing(cur_edge->to, losing_PO, losing_PI) == FALSE) { // succ non losing?
					succ_list = scan_add_or_remove_and_free(succ_list, cur_edge, (void*)compare_safety_game_edges_reverse, (void*)free_safety_game_edge);
				}
				else {
					free_safety_game_edge(cur_edge);
				}
			}
			else {
				free_safety_game_edge(cur_edge);
			}
		}
		else {
			succ_list = scan_add_or_remove_and_free(succ_list, cur_edge, (void*)compare_safety_game_edges, (void*)free_safety_game_edge);
		}
	}

	GList *sorted_succ_list = NULL;
	GList *curlink = succ_list;
	if(player == P_O) { //t belongs to P_O -> sort the successors and add them to succ_to_visit
		while(curlink != NULL) {
			//sorted_succ_list = g_list_prepend(sorted_succ_list, curlink->data);
			sorted_succ_list = insert_sorted(sorted_succ_list, curlink->data, (void*)compare_safety_game_edges_counters_sum_reverse); // the smallest elements will be placed at the beginning of the linked list
			curlink = curlink->next;
		}

		hash_table_key* key = (hash_table_key*)malloc(sizeof(hash_table_key));
		key->t = t;
		key->sigma_index = -1;
		g_hash_table_insert(succ_to_visit, (gconstpointer*)key, (gconstpointer*)sorted_succ_list);

		// Add the first successor passed to waiting
		waiting = g_list_append(waiting, get_first_successor_passed_non_losing(t, succ_to_visit, passed, losing_PO, losing_PI));

	}
	else { //t belongs to P_I -> sort the successors and add them to waiting
		while(curlink != NULL) {
			//sorted_succ_list = g_list_prepend(sorted_succ_list, curlink->data);
			sorted_succ_list = insert_sorted(sorted_succ_list, curlink->data, (void*)compare_safety_game_edges_counters_sum_reverse); // the largest elements will be placed at the end of the linked list -> popped first
			curlink = curlink->next;
		}

		// Add the successors to waiting
		waiting = g_list_concat(waiting, sorted_succ_list);
	}
	g_list_free(succ_list);
	return waiting;
}

/** Returns TRUE if t has a at least a successor not visited yet non losing, FALSE otherwise **/
static char
has_one_succ_to_visit_non_losing(tuple *t, GHashTable *succ_to_visit, antichain *losing_PO, antichain *losing_PI) {
	hash_table_key* key = new_hash_table_key(t, -1);

	// Try to retrieve the successors to visit list from succ_to_visit
	GList *succ_list = g_hash_table_lookup(succ_to_visit, key);
	safety_game_edge* first_succ_passed;
	while(succ_list != NULL) {
		if(is_losing(((safety_game_edge*)succ_list->data)->to, losing_PO, losing_PI) == FALSE) {
			free(key);
			return TRUE;
		}
		succ_list = succ_list->next;
	}
	free(key);

	return FALSE;
}

/** Removes the first edge e = (s,s') with s' in passed from the linked list associated to s in succ_to_visit if there is one, the first edge of the list otherwise
 	Returns that edge
 	Note that t belongs to P_O
 	Note that the successors list is supposed to be non empty (a test must be done before) */
static safety_game_edge*
get_first_successor_passed_non_losing(tuple *t, GHashTable *succ_to_visit, GHashTable *passed, antichain *losing_PO, antichain *losing_PI) {
	hash_table_key* key = new_hash_table_key(t, -1);

	// Try to retrieve the successors to visit list from succ_to_visit
	GList *succ_list = g_hash_table_lookup(succ_to_visit, key);
	safety_game_edge* first_succ_passed;
	GList *curlink = succ_list;
	char found = FALSE;
	int i=0;
	while(curlink != NULL) {
		if(is_passed(((safety_game_edge*)curlink->data)->to, passed) == TRUE && is_losing(((safety_game_edge*)curlink->data)->to, losing_PO, losing_PI) == FALSE) { // is passed?
			first_succ_passed = curlink->data;
			succ_list = g_list_delete_link(succ_list, curlink);
			g_hash_table_insert(succ_to_visit, (gconstpointer*)key, (gconstpointer*)succ_list);
			found = TRUE;
			break;
		}
		i++;
		curlink = curlink->next;
	}
	if(found == FALSE) { // no succ passed?
		first_succ_passed = succ_list->data;
		succ_list = remove_first(succ_list);
		g_hash_table_insert(succ_to_visit, (gconstpointer*)key, (gconstpointer*)succ_list);
	}

	return first_succ_passed;
}

/** Computes the antichains of winning positions for P_O and P_I from the passed tuples and the losing antichains **/
static safety_game*
compute_winning_positions(antichain* losing_PO, antichain* losing_PI, GHashTable *passed, tuple *s_ini, alphabet_info *alphabet, int size) {
	safety_game *winning_positions = new_safety_game(new_antichain(), new_antichain(), s_ini->cf->player);

	GHashTable *mapping_table;

	// If s_ini is not losing, there is a solution
	if(is_losing(s_ini, losing_PO, losing_PI) == FALSE) {
		mapping_table = g_hash_table_new_full(hash_key, compare_keys, free, free); // maps each counting function in the solution to an index
		update_winning_positions(s_ini, alphabet, winning_positions, mapping_table, losing_PO, losing_PI, passed);
		g_hash_table_destroy(mapping_table);
	}
	else {
		free_tuple_full(s_ini);
	}
	return winning_positions;
}

/** Part of the winning positions computation: adds t to the antichain of winning positions
 	If t belongs to P_O, find a successor non losing passed and add update the winning positions with it
 	If t belongs to P_I, computes all successors and update the winning positions with each of them
 	Optimization (maximum successors for P_I): if t belongs to P_I, only deal the maximum successors of t **/
static void
update_winning_positions(tuple *t, alphabet_info *alphabet, safety_game *winning_positions, GHashTable *mapping_table, antichain* losing_PO, antichain* losing_PI, GHashTable* passed) {
	hash_table_key* key = new_hash_table_key(t, -1);

	// Insert t in the mapping_table
	char *value = (char*)malloc(sizeof(char));
	value[0] = TRUE;
	g_hash_table_insert(mapping_table, (gconstpointer*)key, (gconstpointer*)value);

	int nb_outgoing_edges, sigma_size;
	antichain *max_succ;
	if(t->cf->player == P_O) {
		nb_outgoing_edges = 1;
		sigma_size = alphabet->sigma_output_size;
		add_element_to_antichain(winning_positions->positions_O, t, (void*)compare_tuples);
	}
	else {
		nb_outgoing_edges = alphabet->sigma_input_size;
		sigma_size = alphabet->sigma_input_size;
		add_element_to_antichain(winning_positions->positions_I, t, (void*)compare_tuples);
		max_succ = maximal_tuple_succ(t, alphabet);
	}

	// Compute successors of t
	tuple *cur_succ, *good_succ;
	int i;
	char *cur_value;
	hash_table_key *cur_key;
	char found = FALSE;
	for(i=0; i<sigma_size; i++) {
		cur_succ = tuple_succ(t, i, alphabet);

		if(t->cf->player == P_O) { //t belongs to P_O
			if((is_losing(cur_succ, losing_PO, losing_PI) == FALSE) && (is_passed(cur_succ, passed) == TRUE)) { // Not losing and passed?
				cur_key = new_hash_table_key(cur_succ, -1);

				// Check whether cur_succ already belongs to the solution or not
				cur_value = (char*)g_hash_table_lookup(mapping_table, cur_key);
				if(cur_value == NULL) { // new state in solution
					good_succ = cur_succ;
				}
				else { // existing state
					found = TRUE;
					free_tuple_full(cur_succ);
					break;
				}
				free(cur_key);
			}
			else {
				free_tuple_full(cur_succ);
			}
		}
		else { //t belongs to P_I
			//if(is_losing(cur_succ, losing_PO, losing_PI) == FALSE) { // cur_succ is not losing?
				GList *curlink = max_succ->incomparable_elements;
				found = FALSE;
				while(curlink != NULL) { // Among the maximal successors of t, find one which is greater than cur_succ
					if(compare_tuples(cur_succ, curlink->data) == TRUE) {
						cur_key = new_hash_table_key(curlink->data, -1);

						// Check whether cur_succ already belongs to the solution or not
						cur_value = (char*)g_hash_table_lookup(mapping_table, cur_key);
						if(cur_value == NULL) { // new state in solution
							update_winning_positions(clone_tuple(curlink->data), alphabet, winning_positions, mapping_table, losing_PO, losing_PI, passed);
						}
						free(cur_key);
						found = TRUE; //found a state to reach when P_I plays the i-th letter of its alphabet
						break;
					}
					curlink = curlink->next;
				}
				free_tuple_full(cur_succ);
				if(found == FALSE) { // Impossible?
					printf("add_state_to_solution: no succ for P_I in maximal succ\n");
					exit(0);
				}
			/*}
			else { // Impossible?
				printf("add_state_to_solution: succ losing for P_I\n");
				exit(0);
			}*/
		}
	}
	if(t->cf->player == P_I) {
		free_antichain_full(max_succ, (void*)free_tuple_full);
	}
	if(t->cf->player == P_O && found == FALSE) { // Impossible?
		update_winning_positions(good_succ, alphabet, winning_positions, mapping_table, losing_PO, losing_PI, passed);
	}
}
