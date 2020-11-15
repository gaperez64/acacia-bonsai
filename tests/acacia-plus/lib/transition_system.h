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

#ifndef TRANSITION_SYSTEM_H_
#define TRANSITION_SYSTEM_H_

#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include "tbucw.h"

/** Structures **/
typedef struct {
	int from;
	int to;
	char *label;
} ts_transition;

typedef struct {
	char player;
	int nb_tr;
	GList *transitions;
	//ts_transition *transitions[];
} ts_state;

typedef struct {
	int nb_states_PO;
	int nb_states_PI;
	int size_states_PO;
	int size_states_PI;
	int nb_initial_states;
	int *initial_states;
	ts_state *states[];
} transition_system;

/** Functions prototypes **/
transition_system* initialize_transition_system(int, int);
ts_state* new_ts_state(char);
transition_system* add_new_ts_transition(transition_system*, int, int, char*);

ts_state* get_ts_state(transition_system*, int);
ts_transition* get_ts_transition_from_link(GList*);

char is_ts_state_null(ts_state*);

void free_transition_system(transition_system*);
static void free_ts_state(ts_state*);
static void free_ts_transition(ts_transition*);

void print_transition_system(transition_system*);


#endif /* TRANSITION_SYSTEM_H_ */
