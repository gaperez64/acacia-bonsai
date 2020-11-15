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

#ifndef TBUCW_H
#define TBUCW_H

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <glib.h>
#include "bits.h"

#define P_I 1 /** Environment player (Input) **/
#define P_O 2 /** System player (Output) **/

/** Structures **/
/** Alphabet information **/
typedef struct {
	int input_size; //number of input propositions
	int output_size; //number of output propositions
	char **input; //input propositions
	char **output; //output propositions
	int sigma_input_size; //size of the input alphabet
	int sigma_output_size; //size of the output alphabet
	LABEL_BIT_REPRES **sigma_input; //input alphabet
	LABEL_BIT_REPRES **sigma_output; //output alphabet
} alphabet_info;

/** Transition label (DNF formula) **/
typedef struct {
	int disjunction_size;
	LABEL_BIT_REPRES **disjunction; /** disjunction of conjunctions **/
} label;

/** Individual transition of a tbUCW **/
struct tbucw_tran {
	struct tbucw_state *state_from;
	struct tbucw_state *state_to;
	label *label;
};

/** Individual state of a tbUCW **/
struct tbucw_state {
	int state_label;
	int nb_in_tran;
	int nb_out_tran;
	struct tbucw_tran **in_tran;
	struct tbucw_tran **out_tran;
	char is_accepting;
	char player;
	char unbounded;
	char is_complete;
	char is_trash;
};

/** tbUCW **/
typedef struct {	
	int nb_states;
	int initial_state_index;
	alphabet_info *alphabet;
	int **v_I; // weight function defined over sigma_I
	int **v_O; // weight function defined over sigma_O
	int dimension;
	struct tbucw_state *states[];
} tbucw;


/** Function prototypes **/
tbucw* init_tbucw(int);
void add_state(tbucw*, int, int, int, char, char, char, char);
void add_tran(tbucw*, char*, int, int, int);
void set_initial_state(tbucw*, int);
void set_is_accepting(tbucw*, int, char);
tbucw* set_alphabet(tbucw*, alphabet_info*);
void report_accepting_states_c(tbucw*);
void set_is_complete(tbucw*);
void duplicate_all_tran(tbucw*);  // Adds the incoming transitions to all states, based on the outgoing transitions
label* compute_label(tbucw*, char*, int, char);

char is_accepting(tbucw*, int);
char is_complete(tbucw*, int);
char get_player_id(tbucw*, int);
char* get_formula(alphabet_info*, char, int);
int get_tbucw_size(tbucw*);

alphabet_info* init_alphabet(int, int);
void add_input_prop(alphabet_info*, char*);
void add_output_prop(alphabet_info*, char*);
void compute_alphabets(alphabet_info*);

char has_succ(tbucw*, struct tbucw_state*, LABEL_BIT_REPRES*, char);
int* get_pred(tbucw*, int, LABEL_BIT_REPRES*);
int* get_succ(tbucw*, int, LABEL_BIT_REPRES*);
int* get_succ_from_sigma_index(tbucw*, int, int);
int* get_all_succ(tbucw*, int);

char switch_player(char);
void print_tbucw_stats(tbucw*);
void print_tbucw(tbucw*);
void print_label(tbucw*, label*, char);
void print_formula(LABEL_BIT_REPRES*, int, char**);

void free_tbucw(tbucw*);
static void free_tbucw_state(struct tbucw_state*);
static void free_label(label*);

/** Optimization 1 **/
char is_tbucw_state_unbounded(tbucw*, int);

/** Optimization 2 **/
tbucw* optimize_tbucw(tbucw*, char*);
struct tbucw_state* get_copy_of_state(struct tbucw_state*);
char* detect_reachable_states(tbucw*, struct tbucw_state*, char*, char*);
void reset_tbucw_states_labels(tbucw*);

/** Energy **/
tbucw* set_dimension(tbucw*, int);
tbucw* set_weight_function(tbucw*, char, int**);

#endif
