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

#include "synthesis.h"

/** Returns TRUE if the initial tuple is in the safety game, i.e. if there is a winning strategy, FALSE otherwise **/
char
has_a_winning_strategy(safety_game* sg, alphabet_info* alphabet, char starting_player) {
	int sg_size = sg->positions_O->size;
	char has_winning_strategy = FALSE;
	if(sg_size > 0) {
		// Retrieve the tuples information
		GNode *cf_info = ((tuple*)(sg->positions_O->incomparable_elements->data))->cf->info;
		int dimension = ((tuple*)(sg->positions_O->incomparable_elements->data))->credits->dimension;
		int *max_credit = ((tuple*)(sg->positions_O->incomparable_elements->data))->credits->max_values;

		// Build the initial tuple
		tuple *p_0 = build_initial_tuple(cf_info, dimension, max_credit);

		if(starting_player == P_I) {
			has_winning_strategy = TRUE;
			int i;
			for(i=0; i<alphabet->sigma_input_size; i++) {
				tuple* cur_succ = tuple_succ(p_0, i, alphabet);
				if(contains_element(sg->positions_O, cur_succ, (void*)compare_tuples) == FALSE) {
					has_winning_strategy = FALSE;
					free_tuple(cur_succ);
					break;
				}
				else {
					free_tuple(cur_succ);
				}
			}
		}
		else {
			if(contains_element(sg->positions_O, p_0, (void*)compare_tuples) == TRUE) {
				has_winning_strategy = TRUE;
			}
			else {
				has_winning_strategy = FALSE;
			}
		}

		free_tuple(p_0);
	}

	return has_winning_strategy;
}


/** Computes recursively a transition system representing one ore a set of winning strategy(ies) (according to flag set_of_max_strategies) from a solved safety game (i.e. restricted to winning positions)
    starting_player: environment or system
    critical: represents whether the critical signals optimization is enabled or not
    Optimization: try to minimize the number of states in the solution -> privilege already visited states **/
transition_system*
extract_strategies_from_safety_game(safety_game* sg, alphabet_info* alphabet, char starting_player, char critical, char set_of_max_strategies) {
	transition_system *strategy;
	int sg_size = sg->positions_O->size;
	if(sg_size > 0) {
		// Retrieve the tuples information
		GNode *cf_info = ((tuple*)(sg->positions_O->incomparable_elements->data))->cf->info;
		int dimension = ((tuple*)(sg->positions_O->incomparable_elements->data))->credits->dimension;
		int *max_credit = ((tuple*)(sg->positions_O->incomparable_elements->data))->credits->max_values;

		// Build the initial tuple
		tuple *p_0 = build_initial_tuple(cf_info, dimension, max_credit);

		// Array of explored tuples (of the system) used by the optimization (goal: optimize the size of the strategy)
		/* PREV_STRG_CODE:
         * char *explored = (char*)malloc(sg_size * sizeof(char));
         */
        char explored[sg_size];// = malloc(sg_size * sizeof(char));
		int i;
		for(i=0; i<sg_size; i++) {
			explored[i] = FALSE;
		}

		// Extraction
		if(starting_player == P_I) { // if the environment starts, the initial state will be a fake state (the real states of the strategy are maximal elements of the output fix point)
			if(is_antichain_empty(sg->positions_O, (void*)is_tuple_empty) == TRUE) {
				strategy = initialize_transition_system(0, 0);
			}
			else {
				strategy = initialize_transition_system(sg_size*alphabet->sigma_output_size + 1, sg_size);
				strategy->states[strategy->size_states_PO] = new_ts_state(P_I);
				strategy->nb_states_PI++;
				strategy->nb_initial_states = 1;
                /* PREV_STRG_CODE:
				* strategy->initial_states = (int*)malloc(sizeof(int));
                */
                strategy->initial_states = malloc(sizeof(int));
				strategy->initial_states[0] = sg_size; // First state of P_I

				// Deal it (explore its successors)
				char has_a_winning_strategy = deal_state_PI(sg, alphabet, strategy, sg_size, p_0, critical, set_of_max_strategies, explored);
				if(has_a_winning_strategy == FALSE) {
					strategy->nb_states_PO = strategy->nb_states_PO = 0;
				}
			}
		}
		else { // if the system starts, find a maximal element of the fix point greater than the initial tuple
			GList *curlink = sg->positions_O->incomparable_elements;
			GList *initial_states_index = NULL;
			GList *initial_states = NULL;
			int i=0, nb_initial_states=0;
			while(curlink != NULL) {
				if(compare_tuples(p_0, curlink->data) == TRUE) { // check whether the initial tuple is under current maximal element of antichain
					nb_initial_states++; // the initial tuple is in the fix point -> there is a winning strategy
					initial_states_index = g_list_prepend(initial_states_index, GINT_TO_POINTER(i));
					initial_states = g_list_prepend(initial_states, curlink->data);

					if(set_of_max_strategies == FALSE) {
						break;
					}
				}
				i++;
				curlink = curlink->next;
			}
			if(nb_initial_states > 0) {
				strategy = initialize_transition_system(sg_size*alphabet->sigma_output_size, sg_size);
				strategy->nb_initial_states = nb_initial_states;
				strategy->initial_states = (int*)malloc(nb_initial_states*sizeof(int));

				GList *cur_initial_state_index = initial_states_index;
				GList *cur_initial_state = initial_states;
				i=0;
				int initial_state_index;
				while(cur_initial_state_index != NULL) {
					initial_state_index = GPOINTER_TO_INT(cur_initial_state_index->data);
					strategy->nb_states_PO++;
					strategy->initial_states[i] = initial_state_index;
					if(explored[initial_state_index] == FALSE) {
						strategy->states[initial_state_index] = new_ts_state(P_O);
						explored[initial_state_index] = TRUE; // optimization: set the i-th maximal element as visited (if we can go back here later, we will)
						if(critical == TRUE) {
							deal_state_PO_crit(sg, alphabet, strategy, initial_state_index, cur_initial_state->data, set_of_max_strategies, explored); // deal it (explore its successors)
						}
						else {
							deal_state_PO(sg, alphabet, strategy, initial_state_index, cur_initial_state->data, set_of_max_strategies, explored); // deal it (explore its successors)
						}
					}

					i++;
					cur_initial_state_index = cur_initial_state_index->next;
					cur_initial_state = cur_initial_state->next;
				}
			}
			else {
				strategy = initialize_transition_system(0, 0);
			}
			g_list_free(initial_states_index);
			g_list_free(initial_states);
		}
		free_tuple_full(p_0);
	}
	else { // empty fix point -> there is no winning strategy
		strategy = initialize_transition_system(0, 0);
	}

	return strategy;
}

/** Part of the extract_strategies_from_safety_game function: computes the strategies from the initial tuple (which belongs to the environment)
    i.e. compute, for all input symbol, the elements of the output fix_point where it leads
    Called only once, at the beginning of the strategy computation (when starting player = environment) **/
static char
deal_state_PI(safety_game* sg, alphabet_info* alphabet, transition_system* strategy, int state_index, tuple* state, char critical, char set_of_max_strategies, char* explored) {
	int i, k, sg_O_index;
	tuple *cur_succ;
	GList *curlink;
	for(i=0; i<alphabet->sigma_input_size; i++) {
		cur_succ = tuple_succ(state, i, alphabet); // compute cur_succ for all input symbol
		curlink = sg->positions_O->incomparable_elements;
		GList *goodlinks = NULL, *sg_O_indexes = NULL;
		int nb_goodlinks = 0;
		k = 0;
		while(curlink != NULL) { // find a maximal element of the output fix point which is greater than cur_succ (optimization: try to find an already explored one)
			if(compare_tuples(cur_succ, curlink->data) == TRUE) {
				nb_goodlinks++;
				goodlinks = g_list_prepend(goodlinks, curlink->data);
				sg_O_indexes = g_list_prepend(sg_O_indexes, GINT_TO_POINTER(k));
				if(explored[k] == TRUE && set_of_max_strategies == FALSE) { // the k-th maximal element has already been visited and is greater than cur_succ -> we choose this one
					break;
				}
			}
			k++;
			curlink = curlink->next;
		}
		free_tuple_full(cur_succ);

		// Check whether cur_succ is in the output fix point (if not, it means that there is no solution because there exists a sigma_I which leads out of the fix point)
		if(nb_goodlinks == 0) {
			return FALSE;
		}

		GList *cur_goodlinks = goodlinks;
		GList *cur_sg_O_indexes = sg_O_indexes;
		int sg_O_index;
		while(cur_goodlinks != NULL) {
			sg_O_index = GPOINTER_TO_INT(cur_sg_O_indexes->data);
			// If the state hasn't been explored yet, add it and deal it
			if(explored[sg_O_index] == FALSE) {
				explored[sg_O_index] = TRUE;

				// Add the state
				strategy->states[sg_O_index] = new_ts_state(P_O);
				strategy->nb_states_PO++;

				if(critical == TRUE) {
					deal_state_PO_crit(sg, alphabet, strategy, sg_O_index, cur_goodlinks->data, set_of_max_strategies, explored); // deal it (explore its successors)
				}
				else {
					deal_state_PO(sg, alphabet, strategy, sg_O_index, cur_goodlinks->data, set_of_max_strategies, explored); // deal it (explore its successors)
				}
			}
			// Add the transition
			strategy = add_new_ts_transition(strategy, state_index, sg_O_index, get_formula(alphabet, P_I, i));

			if(set_of_max_strategies == FALSE) {
				break;
			}

			cur_goodlinks = cur_goodlinks->next;
			cur_sg_O_indexes = cur_sg_O_indexes->next;
		}

		g_list_free(goodlinks);
		g_list_free(sg_O_indexes);
	}

	return TRUE;
}

/** Part of the extract_strategies_from_safety_game function: computes the strategies from state state_index (which belongs to the output fix point)
    i.e. find an or all output symbols (according to set_of_max_strategies parameter (an if FALSE, several if TRUE)) which leads to an element of the input fix_point
         and from this element, for all input symbol, find a maximal element of the output fix_point where it comes back (might be several, choose only one -> determinism)
    Called recursively while new states have to be explored (and when the critical signals optimization is off) **/
static void
deal_state_PO(safety_game* sg, alphabet_info* alphabet, transition_system* strategy, int state_index, tuple* state, char set_of_max_strategies, char *explored) {
	// First find sigma_F, an output signal which leads from state_index to an element of the input fix point (signal safe to play)
	int i, j, k, sg_O_index, sigma_F_index = -1;
	tuple *cur_succ, *good_succ, *cur_succ_tran;
	GList *curlink_tran, *goodlink_tran;
	int cur_PI_state_index;
	for(i=0; i<alphabet->sigma_output_size; i++) { // find all output signals sigma_F s.t. succ(state, sigma_F) is in the input fix point
		cur_succ = tuple_succ(state, i, alphabet);
		if(contains_element(sg->positions_I, cur_succ, (void*)compare_tuples) == TRUE) { // if belongs to input fix point
			cur_PI_state_index = strategy->size_states_PO+strategy->nb_states_PI;
			strategy->states[cur_PI_state_index] = new_ts_state(P_I);
			strategy->nb_states_PI++;
			strategy = add_new_ts_transition(strategy, state_index, cur_PI_state_index, get_formula(alphabet, P_O, i));

			// Second, if there exists a sigma_F (if the fix point is correctly computed, there is at least one),
			// compute the successors of cur_succ for all sigma_I of the input alphabet
			// For each successor, find a maximal element of the output fix point which covers it,
			// and add this maximal element as a state to the solution (optimization: privilege already explored maximal elements)
			for(j=0; j<alphabet->sigma_input_size; j++) { // for all input signals, find an element of the output fix point where it leads
				cur_succ_tran = tuple_succ(cur_succ, j, alphabet); // compute current successor
				// Traverse the output fix point to find a maximal element which covers cur_succ_tran
				curlink_tran = sg->positions_O->incomparable_elements;
				k = 0;
				GList *goodlinks_tran = NULL, *sg_O_indexes = NULL;
				int nb_goodlinks_tran = 0;
				while(curlink_tran != NULL) { // find a maximal element of the output fix point greater than cur_succ_tran
					if(compare_tuples(cur_succ_tran, curlink_tran->data) == TRUE) { // maximal element found
						nb_goodlinks_tran++;
						goodlinks_tran = g_list_prepend(goodlinks_tran, curlink_tran->data); // good_link_tran contains the good maximal element
						sg_O_indexes = g_list_prepend(sg_O_indexes, GINT_TO_POINTER(k)); // index of the good maximal element in the output fix point
						if(explored[k] == TRUE && set_of_max_strategies == FALSE) { // optimization: we stop here only if this state has already been visited, otherwise we'll try to find an already visited state (if not, we'll keep that one)
							break;
						}
					}
					k++;
					curlink_tran = curlink_tran->next;
				}
				free_tuple_full(cur_succ_tran);

				if(nb_goodlinks_tran == 0) { // Impossible?
					printf("deal state PO: No succ in SG_O\n");
					exit(0);
				}

				GList *cur_goodlinks_tran = goodlinks_tran;
				GList *cur_sg_O_indexes = sg_O_indexes;
				int sg_O_index;
				while(cur_goodlinks_tran != NULL) {
					sg_O_index = GPOINTER_TO_INT(cur_sg_O_indexes->data);

					// Add the new state to the solution, deal it and set the transition function
					update_solution(sg, alphabet, strategy, cur_PI_state_index, i, j, sg_O_index, cur_goodlinks_tran->data, FALSE, set_of_max_strategies, explored);

					if(set_of_max_strategies == FALSE) {
						break;
					}

					cur_goodlinks_tran = cur_goodlinks_tran->next;
					cur_sg_O_indexes = cur_sg_O_indexes->next;
				}

				g_list_free(goodlinks_tran);
				g_list_free(sg_O_indexes);
			}
			if(set_of_max_strategies == FALSE) {
				break;
			}
		}
		free_tuple_full(cur_succ);
	}
}

/** Part of the extract_strategies_from_safety_game function: computes the strategies from state state_index (which belongs to PO) when the critical signals optimization is enabled and fp has been computed backwardly
    When critical signals optimization is enabled, the correctness of the input fix point is not established, so a special treatment is needed:
        find an or all output symbols (according to set_of_max_strategies parameter (an if FALSE, several if TRUE)) sigma_F such that for all input symbols sigma_I, succ(succ(state, sigma_F), sigma_I)
        is in the output fix point and for each of these successors and find the maximal element of the output fix_point where it comes back (might be several, choose only one -> determinism)
    Called recursively while new states have to be explored (and when the critical signals optimization is on) **/
static void
deal_state_PO_crit(safety_game* sg, alphabet_info* alphabet, transition_system* strategy, int state_index, tuple* state, char set_of_max_strategies, char *explored) {
	int i, j, k, sg_O_index, sigma_F_index = -1, sg_I_index = -1;
	tuple *cur_succ, *cur_succ_tran;
	char wrong_sigma_F;
	GList *sigma_I_indexes_list, *sg_O_indexes_list, *t_list, *curlink_tran, *goodlink_tran;
	for(i=0; i<alphabet->sigma_output_size; i++) { // find a sigma_F in output alphabet safe to play
		cur_succ = tuple_succ(state, i, alphabet);
		wrong_sigma_F = FALSE; // flag used to detect if the current output signal is safe to play or not
		// lists of indexes of maximal elements of output fix point where the successors of cur_succ for all input signals come back, the sigma_indexes that lead there, and the tuples they represent
		sigma_I_indexes_list = NULL;
		sg_O_indexes_list = NULL; // these maximal elements will be added as states to the solution only if sigma_F is safe to play
		t_list = NULL;
		for(j=0; j<alphabet->sigma_input_size; j++) { // for all input signals, check whether their successors come back to the output fix point
			cur_succ_tran = tuple_succ(cur_succ, j, alphabet);
			// Traverse the output fix point to find a maximal element which covers cur_succ_tran
			curlink_tran = sg->positions_O->incomparable_elements;
			k = 0;
			GList *goodlinks_tran = NULL, *sg_O_indexes = NULL, *j_indexes = NULL;
			int nb_goodlinks_tran = 0;
			while(curlink_tran != NULL) { // find a maximal element of the output fix point greater than cur_succ_tran
				if(compare_tuples(cur_succ_tran, curlink_tran->data) == TRUE) { // maximal element found
					nb_goodlinks_tran++;
					j_indexes = g_list_prepend(j_indexes, GINT_TO_POINTER(j));
					goodlinks_tran = g_list_prepend(goodlinks_tran, curlink_tran->data); // good_link_tran contains the good maximal element
					sg_O_indexes = g_list_prepend(sg_O_indexes, GINT_TO_POINTER(k)); // index of the good maximal element in the output fix point
					if(explored[k] == TRUE && set_of_max_strategies == FALSE) { // optimization: we stop here only if this state has already been visited, otherwise we'll try to find an already visited state (if not, we'll keep that one)
						break;
					}
				}
				k++;
				curlink_tran = curlink_tran->next;
			}
			free_tuple_full(cur_succ_tran);
			if(nb_goodlinks_tran == 0) { // means that there exists a sigma_I such that, for this sigma_F, succ(succ(state, sigma_F), sigma_I) is not in the output fix point -> this sigma_F is not safe to play
				g_list_free(j_indexes);
				g_list_free(goodlinks_tran);
				g_list_free(sg_O_indexes);
				wrong_sigma_F = TRUE;
				break; // we can stop and pass to another sigma_F
			}
			else { // otherwise, this sigma_F might be a good one -> add the information computed to the list
				if(set_of_max_strategies == TRUE) {
					sigma_I_indexes_list = g_list_concat(sigma_I_indexes_list, j_indexes);
					sg_O_indexes_list = g_list_concat(sg_O_indexes_list, sg_O_indexes);
					t_list = g_list_concat(t_list, goodlinks_tran);
				}
				else {
					sigma_I_indexes_list = g_list_prepend(sigma_I_indexes_list, j_indexes->data);
					sg_O_indexes_list = g_list_prepend(sg_O_indexes_list, sg_O_indexes->data);
					t_list = g_list_prepend(t_list, goodlinks_tran->data);
				}
			}
		}
		free_tuple_full(cur_succ);

		if(wrong_sigma_F == FALSE) { // if the current sigma_F is safe, we've done here (no need to test the remaining output signals)
			int cur_PI_state_index = strategy->size_states_PO+strategy->nb_states_PI;
			strategy->states[cur_PI_state_index] = new_ts_state(P_I);
			strategy->nb_states_PI++;
			strategy = add_new_ts_transition(strategy, state_index, cur_PI_state_index, get_formula(alphabet, P_O, i));

			// Add all maximal elements of the output fix point reached as states to the solution, deal then and set the transitions and the output functions
			GList *curlink1 = sigma_I_indexes_list;
			GList *curlink2 = sg_O_indexes_list;
			GList *curlink3 = t_list;
			while(curlink1 != NULL) {
				update_solution(sg, alphabet, strategy, cur_PI_state_index, i, (int)curlink1->data, (int)curlink2->data, (tuple*)curlink3->data, TRUE, set_of_max_strategies, explored);
				curlink1 = curlink1->next;
				curlink2 = curlink2->next;
				curlink3 = curlink3->next;
			}
			g_list_free(sigma_I_indexes_list);
			g_list_free(sg_O_indexes_list);
			g_list_free(t_list);
			if(set_of_max_strategies == FALSE) {
				break;
			}
		}
	}
}

/** Part of the extract_strategies_from_safety_game function:
       Adds a new state (a maximal element of the output fix point) to the solution if it is not already present
       Sets the transition to this new state and the output function of this new state
       Deals the new state (i.e. explore its successors)
    Called by deal_state_PO and deal_state_PO_crit each time a state has to be added to the solution (except for the initial state) **/
static void
update_solution(safety_game* sg, alphabet_info* alphabet, transition_system* strategy, int predecessor_index, int sigma_F_index, int sigma_I_index, int new_state_index, tuple* new_state, char critical, char set_of_max_strategies, char *explored) {
	if(explored[new_state_index] == FALSE) { // If the state hasn't been explored yet, add it and deal it
		explored[new_state_index] = TRUE; // array used for the optimization trying to minimize the size of the solution

		// Add the new node to the solution
		strategy->states[new_state_index] = new_ts_state(P_O);
		strategy->nb_states_PO++;

		// Deal the new state (explore its successors)
		if(critical == TRUE) {
			deal_state_PO_crit(sg, alphabet, strategy, new_state_index, new_state, set_of_max_strategies, explored); // deal it (explore its successors)
		}
		else {
			deal_state_PO(sg, alphabet, strategy, new_state_index, new_state, set_of_max_strategies, explored); // deal it (explore its successors)
		}
	}

	// Add the transition to this state
	strategy = add_new_ts_transition(strategy, predecessor_index, new_state_index, get_formula(alphabet, P_I, sigma_I_index));
}
