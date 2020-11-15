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

#include "transition_system.h"

/** Initializes a transition system with size states **/
transition_system*
initialize_transition_system(int nb_states_PI, int nb_states_PO) {
	int size = nb_states_PI + nb_states_PO;
	transition_system* ts = (transition_system*)malloc(sizeof(transition_system)+(size)*(sizeof(ts_state*)));
	ts->nb_states_PO = 0;
	ts->nb_states_PI = 0;
	ts->size_states_PO = nb_states_PO;
	ts->size_states_PI = nb_states_PI;
	int i;
	for(i=0; i<size; i++) {
		ts->states[i] = NULL;
	}
	return ts;
}

/** Creates a new ts_state **/
ts_state*
//new_ts_state(int nb_tr, char player) {
new_ts_state(char player) {
    /* PREV_STRG_CODE:
	 * ts_state *s = (ts_state*)malloc(sizeof(ts_state));
     */
    ts_state* s = malloc(sizeof(ts_state));
	s->nb_tr = 0;
	s->player = player;
	s->transitions = NULL;

	return s;
}

/** Creates and adds a new ts_transition to a ts_state**/
transition_system*
add_new_ts_transition(transition_system *ts, int from, int to, char *label) {
	ts_transition *e = (ts_transition*)malloc(sizeof(ts_transition));
	e->from = from;
	e->to = to;
	e->label = label;

	ts->states[from]->transitions = g_list_prepend(ts->states[from]->transitions, e);
	ts->states[from]->nb_tr++;

	return ts;
}

/** Returns a state of the transition system ts (according to state_index) **/
ts_state*
get_ts_state(transition_system* ts, int state_index) {
	return ts->states[state_index];
}

/** Returns the ts_transition of a_link **/
ts_transition*
get_ts_transition_from_link(GList *a_link) {
	return a_link->data;
}

/** Returns TRUE if s is NULL, FALSE otherwise **/
char is_ts_state_null(ts_state* s) {
	if(s == NULL) {
		return TRUE;
	}
	return FALSE;
}


/** Frees a transition system **/
void
free_transition_system(transition_system* ts) {
	int i;
	for(i=0; i<ts->size_states_PI+ts->size_states_PO; i++) {
		if(ts->states[i] != NULL) {
			free_ts_state(ts->states[i]);
		}
	}
	free(ts);
}

/** Frees a transition system state **/
void
free_ts_state(ts_state* s) {
/*	int i;
	for(i=0; i<s->nb_tr; i++) {
		free_ts_transition(s->transitions[i]);
	}*/
	g_list_foreach(s->transitions, (GFunc)free_ts_transition, NULL);
	free(s);
}

/** Frees a transition system transition **/
static void
free_ts_transition(ts_transition* t) {
	free(t->label);
	free(t);
}

/** Prints a transition system **/
void
print_transition_system(transition_system* ts) {
	printf("Transition system {\n");
	int i, j;
	for(i=0; i<ts->size_states_PO+ts->size_states_PI; i++) {
		if(ts->states[i] != NULL) {
			printf("   State %d", i);
			for(j=0; j<ts->nb_initial_states; j++) {
				if(ts->initial_states[j] == i) {
					printf(", initial");
					break;
				}
			}
			printf(", belongs to Player %d :\n", ts->states[i]->player);
			printf("      Outgoing transitions:\n");
			GList *curtran = ts->states[i]->transitions;
			while(curtran != NULL) {
				printf("         to %d\n", ((ts_transition*)curtran->data)->to);
				curtran = curtran->next;
			}
			/*for(j=0; j<ts->states[i]->nb_tr; j++) {
				printf("         to %d\n", ts->states[i]->transitions[j]->to);
			}*/
		}
	}
	printf("}\n");
}
