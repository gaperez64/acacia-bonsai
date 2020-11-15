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

#include "tbucw.h"

/** Creates a new empty tbucw which will contain nb_states states **/
tbucw*
init_tbucw(int nb_states)  {
	tbucw *aut = (tbucw*)malloc(sizeof(tbucw)+nb_states*sizeof(struct tbucw_state*));
	aut->nb_states = 0; //will be incremented each time a state is added to the automaton
	struct tbucw_state *cur_s;
	int i;
	for(i=0; i<nb_states; i++) {
		cur_s = (struct tbucw_state*)malloc(sizeof(struct tbucw_state));
		cur_s->state_label = i;

		aut->states[i] = cur_s;
	}

	return aut;
}

/** Adds a state to the tbucw **/
void
add_state(tbucw *aut, int index, int nb_in_tran, int nb_out_tran, char is_accepting, char player, char unbounded, char is_trash) {
	struct tbucw_state *s = aut->states[index];
	s->in_tran = (struct tbucw_tran**)malloc(nb_in_tran*sizeof(struct tbucw_tran*));
	s->out_tran = (struct tbucw_tran**)malloc(nb_out_tran*sizeof(struct tbucw_tran*));
	s->nb_in_tran = 0;
	s->nb_out_tran = 0; // these variables will be incremented each time a transition is added to the state
	if(unbounded != FALSE) { //Optimisation1: if s is bounded, it is non accepting (if opt1 is disabled, unbounded is TRUE by default)
		s->is_accepting = is_accepting;
	}
	else {
		s->is_accepting = FALSE;
	}
	s->player = player;
	s->state_label = index;
	s->unbounded = unbounded;
	s->is_trash = is_trash;

	aut->nb_states++;
}

/** Adds a transition labeled "label" to the outgoing transitions of state from **/
void
add_tran(tbucw *aut, char *label, int disjunction_size, int from, int to) {
	struct tbucw_tran *tr = (struct tbucw_tran*)malloc(sizeof(struct tbucw_tran));
	tr->state_from = aut->states[from];
	tr->state_to = aut->states[to];
	tr->label = compute_label(aut, label, disjunction_size, aut->states[from]->player);

	aut->states[from]->out_tran[aut->states[from]->nb_out_tran] = tr;
	aut->states[from]->nb_out_tran++;
}

/** Sets the initial state index **/
void
set_initial_state(tbucw *aut, int initial_index) {
	aut->initial_state_index = initial_index;
}

/** Sets is_accepting value of the tbucw_state state_index to new_value **/
void
set_is_accepting(tbucw *aut, int state_index, char new_value) {
	aut->states[state_index]->is_accepting = new_value;
}

/** Sets the alphabet information of tbucw aut **/
tbucw*
set_alphabet(tbucw* aut, alphabet_info* alphabet) {
	aut->alphabet = alphabet;
	return aut;
}

/** Reports the accepting states to turn based states when it is possible (if a state s is accepting, we can put it non accepting
    and set all its successors s' (turn based states) accepting, only if those states s' do not have other predecessors than s)   **/
void
report_accepting_states(tbucw *aut) {
	int i, j, k;
	char possible;
	struct tbucw_state* cur_succ;
	for(i=0; i<aut->nb_states; i++) {
		if(is_accepting(aut, i)) {
			possible = TRUE;
			for(j=0; j<aut->states[i]->nb_out_tran; j++) {
				cur_succ = aut->states[i]->out_tran[j]->state_to;
				for(k=0; k<cur_succ->nb_in_tran; k++) {
					if(cur_succ->in_tran[k]->state_from->state_label != i) { // if a successor has other predecessors than i, we can not report the accepting state (possible problem)
						possible = FALSE;
						break;
					}
				}
				if(possible == FALSE) {
					break;
				}
			}
			if(possible == TRUE && aut->states[i]->nb_out_tran > 0) {
				set_is_accepting(aut, i, FALSE);
				for(j=0; j<aut->states[i]->nb_out_tran; j++) {
					set_is_accepting(aut, aut->states[i]->out_tran[j]->state_to->state_label, TRUE);
				}
			}
		}
	}
}

/** Sets is_complete variables for all states
 	is_complete represents if the state has a transition for all subset of propositions **/
void
set_is_complete(tbucw *aut) {
	LABEL_BIT_REPRES **sigma;
	struct tbucw_state *state;
	int i, j, sigma_size;
	char break_done, player;
	for(i=0; i<aut->nb_states; i++) {
		state = aut->states[i];
		break_done = FALSE;
		if(get_player_id(aut, i) == P_O) {
			sigma_size = aut->alphabet->sigma_output_size;
			sigma = aut->alphabet->sigma_output;
			player = P_O;
		}
		else {
			sigma_size = aut->alphabet->sigma_input_size;
			sigma = aut->alphabet->sigma_input;
			player = P_I;
		}
		for(j=0; j<sigma_size; j++) {
			if(has_succ(aut, state, sigma[j], player) == FALSE) {
				state->is_complete = FALSE;
				break_done = TRUE;
				break;
			}
		}
		if(break_done == FALSE) {
			state->is_complete = TRUE;
		}
	}
}

/** Duplicates all transitions in the outgoing transitions list of each state to the incoming transitions list of corresponding state **/
void
duplicate_all_tran(tbucw *aut) {
	int i, j;
	int state_index;
	struct tbucw_state *cur_state;

	for(i=0; i<aut->nb_states; i++) {
		cur_state = aut->states[i];
		for(j=0; j<cur_state->nb_out_tran; j++) {
			state_index = cur_state->out_tran[j]->state_to->state_label;

			aut->states[state_index]->in_tran[aut->states[state_index]->nb_in_tran] = cur_state->out_tran[j];
			aut->states[state_index]->nb_in_tran++;
		}
	}
}

/** Computes the label transition based on a string containing only 0s, 1s and 2s
	Ex : let Sigma = {p, q, r}. the string "112-122" represents the label (p AND q AND !r) OR (p AND !q AND !r)
		 special case : the string "T" represents the label True (T or 1)                           **/
label*
compute_label(tbucw *aut, char *prop_tab, int disjunction_size, char player) {
	int nb_prop;
	label *lab = (label*)malloc(sizeof(label));
	lab->disjunction_size = disjunction_size;
	lab->disjunction = (LABEL_BIT_REPRES**)malloc(disjunction_size*sizeof(LABEL_BIT_REPRES*));
	if(player == P_I) {
		nb_prop = aut->alphabet->input_size;
	}
	else {
		nb_prop = aut->alphabet->output_size;
	}
	int conjunction_size = ceil((nb_prop*1.0)/(LABEL_BIT_RANGE/2)); //compute the size of the formulas consisting of conjunctions of propositions (2 bits required by proposition)
	int i, comp;
	// Check whether the label is T
	if(prop_tab[0] == 'T') {
		LABEL_BIT_REPRES *conj = (LABEL_BIT_REPRES*)malloc(sizeof(LABEL_BIT_REPRES));
		if((nb_prop%(LABEL_BIT_RANGE/2)) == 0) {
			comp = LABEL_BIT_RANGE+1;
		}
		else {
			comp = 2*(nb_prop%(LABEL_BIT_RANGE/2));
		}
		conj[0] = build_int(comp-1, comp);
		comp = 2*LABEL_BIT_RANGE;
		for(i=1; i<conjunction_size; i++) {
			conj[i] = build_int(comp-1, comp);
		}
		lab->disjunction[0] = conj;

		return lab;
	}

	LABEL_BIT_REPRES *cur_conj;

	// Compute the label
	int j, k, global_i, init_value;
	for(j=0; j<disjunction_size; j++) {
		cur_conj = (LABEL_BIT_REPRES*)malloc(conjunction_size*sizeof(LABEL_BIT_REPRES));
		global_i = 0;
		if((nb_prop%(LABEL_BIT_RANGE/2)) == 0) {
			init_value = 0;
		}
		else {
			init_value = (LABEL_BIT_RANGE/2)-nb_prop%(LABEL_BIT_RANGE/2);
		}

		for(k=0; k<conjunction_size; k++) {
			cur_conj[k] = 0;

			for(i=init_value; i<(LABEL_BIT_RANGE/2); i++,global_i++) {
				if(prop_tab[(j*(nb_prop+1))+global_i] == '1') { //TODO: optimiser ce code (variables)
					cur_conj[k] += BIT(LABEL_BIT_RANGE-2*i-1);
				}
				else if(prop_tab[(j*(nb_prop+1))+global_i] == '2') {
					cur_conj[k] += BIT(LABEL_BIT_RANGE-2*i-2);
				}
				else if(prop_tab[(j*(nb_prop+1))+global_i] == '0') {
					cur_conj[k] += BIT(LABEL_BIT_RANGE-2*i-2) + BIT(LABEL_BIT_RANGE-2*i-1); //TODO: utiliser build_int
				}
			}
			init_value = 0;
		}
		lab->disjunction[j] = cur_conj;
	}

	return lab;
}

/** Returns TRUE if the tbUCW_state indexed state_index is accepting, FALSE otherwise **/
char
is_accepting(tbucw *aut, int state_index) {
	return aut->states[state_index]->is_accepting;
}

/** Returns TRUE if the tbUCW_state is complete, FALSE otherwise **/
char
is_complete(tbucw *aut, int state_index) {
	return aut->states[state_index]->is_complete;
}

/** Returns the id of the player owning the tbUCW_state indexed state_index **/
char
get_player_id(tbucw *aut, int state_index) {
	return aut->states[state_index]->player;
}

/** Returns the element i of the alphabet array of player **/
char*
get_formula(alphabet_info *alphabet, char player, int sigma_index) {
	LABEL_BIT_REPRES *formula;
	int nb_prop;
	char **prop;
	if(player == P_I) {
		formula = alphabet->sigma_input[sigma_index];
		nb_prop = alphabet->input_size;
		prop = alphabet->input;
	}
	else {
		formula = alphabet->sigma_output[sigma_index];
		nb_prop = alphabet->output_size;
		prop = alphabet->output;
	}

	char* prop_tab = (char*)malloc(nb_prop*sizeof(char));

	int i, k, global_i, init_value, conjunction_size;
	LABEL_BIT_REPRES mask;
	char is_true;

	conjunction_size = ceil((nb_prop*1.0)/(LABEL_BIT_RANGE/2));
	if((nb_prop%(LABEL_BIT_RANGE/2)) == 0) {
		init_value = 0;
	}
	else {
		init_value = (LABEL_BIT_RANGE/2)-nb_prop%(LABEL_BIT_RANGE/2);
	}
	global_i = 0;
	is_true = FALSE;

	char label_true = TRUE;
	int comp;
	if((nb_prop%(LABEL_BIT_RANGE/2)) == 0) {
		comp = LABEL_BIT_RANGE+1;
	}
	else {
		comp = 2*(nb_prop%(LABEL_BIT_RANGE/2));
	}
	mask = build_int(comp-1, comp);

	if((mask & formula[0]) == mask) {
		comp = 2*LABEL_BIT_RANGE;
		for(i=1; i<conjunction_size; i++) {
			mask = build_int(comp-1, comp);
			if((mask & formula[i]) != mask) {
				label_true = FALSE;
				break;
			}
		}
		if(label_true == TRUE) {
			prop_tab[0] = 'T';
			return prop_tab;
		}
	}

	for(i=0; i<nb_prop; i++) {
		prop_tab[i] = 0;
	}

	for(k=0; k<conjunction_size && is_true == FALSE; k++) {
		for(i=init_value; i<(LABEL_BIT_RANGE/2); i++, global_i++) {
			mask = BIT(2*((LABEL_BIT_RANGE/2)-i)-1);
			if((mask & formula[k]) == mask) {
				mask = BIT(2*((LABEL_BIT_RANGE/2)-i)-2);
				if((mask & formula[k]) != mask) {
					prop_tab[global_i] = '1';
				}
			}
			else {
				prop_tab[global_i] = '2';
			}
		}
		init_value=0;
	}

	return prop_tab;
}

/** Returns the size of a **/
int
get_tbucw_size(tbucw* a) {
	return a->nb_states;
}

/** Initialize alphabet information structure **/
alphabet_info*
init_alphabet(int input_size, int output_size) {
	alphabet_info *alphabet = (alphabet_info*)malloc(sizeof(alphabet_info));
	alphabet->input_size = 0; //will be incremented each time a proposition is added
	alphabet->output_size = 0; //will be incremented each time a proposition is added
	alphabet->input = (char**)malloc(input_size*sizeof(char*));
	alphabet->output = (char**)malloc(output_size*sizeof(char*));

	return alphabet;
}

/** Adds the proposition s to the input propositions list **/
void
add_input_prop(alphabet_info* alphabet, char *s) {
	alphabet->input[alphabet->input_size] = s;
	alphabet->input_size++;
}

/** Adds the symbol s to the output propositions list **/
void
add_output_prop(alphabet_info* alphabet, char *s) {
	alphabet->output[alphabet->output_size] = s;
	alphabet->output_size++;
}

/** Computes the input and output alphabets (containing 2^input_size and 2^output_size elements) **/
/** TODO: extract duplicated code **/
void
compute_alphabets(alphabet_info *alphabet) {
	// Compute the size of the alphabet of each player
	alphabet->sigma_input_size = 1<<(alphabet->input_size);
	alphabet->sigma_output_size = 1<<(alphabet->output_size);

	// Initialize the alphabets
	alphabet->sigma_input = (LABEL_BIT_REPRES**)malloc((alphabet->sigma_input_size)*sizeof(LABEL_BIT_REPRES*));
	alphabet->sigma_output = (LABEL_BIT_REPRES**)malloc((alphabet->sigma_output_size)*sizeof(LABEL_BIT_REPRES*));
	int word_size_input = ceil((alphabet->input_size*1.0)/(LABEL_BIT_RANGE/2)); //number of uchar needed to represent a word of the input alphabet
	int word_size_output = ceil((alphabet->output_size*1.0)/(LABEL_BIT_RANGE/2)); //number of uchar needed to represent a word of the output alphabet
	LABEL_BIT_REPRES* cur_word;
	int j, k;
	for(j=0; j<alphabet->sigma_input_size; j++) {
		cur_word = (LABEL_BIT_REPRES*)malloc(word_size_input*sizeof(LABEL_BIT_REPRES));
		for(k=0; k<word_size_input; k++) {
			cur_word[k] = 0;
		}
		alphabet->sigma_input[j] = cur_word;
	}
	for(j=0; j<alphabet->sigma_output_size; j++) {
		cur_word = (LABEL_BIT_REPRES*)malloc(word_size_output*sizeof(LABEL_BIT_REPRES));
		for(k=0; k<word_size_output; k++) {
			cur_word[k] = 0;
		}
		alphabet->sigma_output[j] = cur_word;
	}

	// Compute input alphabet
	int i, l, init_value;
	if((alphabet->input_size%(LABEL_BIT_RANGE/2)) == 0) {
		init_value = 0;
	}
	else {
		init_value = LABEL_BIT_RANGE/2-alphabet->input_size%(LABEL_BIT_RANGE/2);
	}

	k = 1;
	for(l=0; l<word_size_input; l++) {
		for(i=init_value; i<(LABEL_BIT_RANGE/2); i++) {
			for(j=0; j<alphabet->sigma_input_size; j++) {
				if((j/k)%2 == 0) {
					alphabet->sigma_input[j][l]+=BIT(LABEL_BIT_RANGE-2-2*i);
				}
				else {
					alphabet->sigma_input[j][l]+=BIT(LABEL_BIT_RANGE-1-2*i);
				}
			}
			k*=2;
			init_value=0;
		}
	}

	// Compute output alphabet
	if((alphabet->output_size%(LABEL_BIT_RANGE/2)) == 0) {
		init_value = 0;
	}
	else {
		init_value = LABEL_BIT_RANGE/2-alphabet->output_size%(LABEL_BIT_RANGE/2);
	}

	k=1;
	for(l=0; l<word_size_output; l++) {
		for(i=init_value; i<(LABEL_BIT_RANGE/2); i++) {
			for(j=0; j<alphabet->sigma_output_size; j++) {
				if((j/k)%2 == 0) {
					alphabet->sigma_output[j][l]+=BIT(LABEL_BIT_RANGE-2-2*i);
				}
				else {
					alphabet->sigma_output[j][l]+=BIT(LABEL_BIT_RANGE-1-2*i);
				}
			}
			k*=2;
			init_value=0;
		}
	}
}

/** Returns true if state_index has at least one successor for sigma in aut **/
char
has_succ(tbucw *aut, struct tbucw_state *state, LABEL_BIT_REPRES *sigma, char player) {
	int i, j, k;
	int conjunction_size;
	if(player == P_O) {
		conjunction_size = ceil((aut->alphabet->output_size*1.0)/(LABEL_BIT_RANGE/2));
	}
	else {
		conjunction_size = ceil((aut->alphabet->input_size*1.0)/(LABEL_BIT_RANGE/2));
	}
	char break_done;

	for(i=0; i<state->nb_out_tran; i++) {
		for(j=0; j<state->out_tran[i]->label->disjunction_size; j++) {
			break_done = FALSE;
			for(k=0; k<conjunction_size; k++) {
				if((state->out_tran[i]->label->disjunction[j][k] & sigma[k]) != sigma[k]) {
					break_done = TRUE;
					break;
				}
			}
			if(break_done == FALSE) {
				return TRUE;
			}
		}
	}
	return FALSE;
}

/** Returns predecessors of state_index for label
	Format : vector of which first element is the number of predecessors **/
int*
get_pred(tbucw *aut, int state_index, LABEL_BIT_REPRES *label) {
	struct tbucw_state *st = aut->states[state_index];
	int *pred = (int*)malloc(((st->nb_in_tran)+1)*sizeof(int));
	char break_done;
	int i, j, k, conjunction_size, nb_pred=0;
	if(st->player == P_I) {
		conjunction_size = ceil((aut->alphabet->output_size*1.0)/(LABEL_BIT_RANGE/2));
	}
	else {
		conjunction_size = ceil((aut->alphabet->input_size*1.0)/(LABEL_BIT_RANGE/2));
	}

	for(i=0; i<st->nb_in_tran; i++) {
		for(j=0; j<st->in_tran[i]->label->disjunction_size; j++) {
			break_done = FALSE;
			for(k=0; k<conjunction_size; k++) {
				if((st->in_tran[i]->label->disjunction[j][k] & label[k]) != label[k]) {
					break_done = TRUE;
					break;
				}
			}
			if(break_done == FALSE) {
				nb_pred++;
				pred[nb_pred] = st->in_tran[i]->state_from->state_label;
				break;
			}
		}
	}
	pred[0] = nb_pred;

	return pred;
}

/** Returns successors of state_index for label 
	Format: vector of which first element is the number of successors **/
int*
get_succ(tbucw *aut, int state_index, LABEL_BIT_REPRES *label) {
	struct tbucw_state *st = aut->states[state_index];
	int *succ = (int*)malloc(((st->nb_out_tran)+1)*sizeof(int));
	char break_done;
	int i, j, k, conjunction_size, nb_succ=0;

	if(st->player == P_I) {
		conjunction_size = ceil((aut->alphabet->input_size*1.0)/(LABEL_BIT_RANGE/2));
	}
	else {
		conjunction_size = ceil((aut->alphabet->output_size*1.0)/(LABEL_BIT_RANGE/2));
	}

	for(i=0; i<st->nb_out_tran; i++) {
		for(j=0; j<st->out_tran[i]->label->disjunction_size; j++) {
			break_done = FALSE;
			for(k=0; k<conjunction_size; k++) {
				if((st->out_tran[i]->label->disjunction[j][k] & label[k]) != label[k]) {
					break_done = TRUE;
					break;
				}
			}
			if(break_done == FALSE) {
				nb_succ++;
				succ[nb_succ] = st->out_tran[i]->state_to->state_label;
				break;
			}
		}
	}
	succ[0] = nb_succ;
	return succ;
}

/** Returns successors of state_index for label sigma_index
    Format: vector of which first element is the number of successors **/
int*
get_succ_from_sigma_index(tbucw *aut, int state_index, int sigma_index) {
	if(get_player_id(aut, state_index) == P_O) {
		return get_succ(aut, state_index, aut->alphabet->sigma_output[sigma_index]);
	}
	else {
		return get_succ(aut, state_index, aut->alphabet->sigma_input[sigma_index]);
	}
}

/** Returns all reachable states in one step from state_index
	Format: vector of which first element is the number of successors **/
int*
get_all_succ(tbucw *aut, int state_index) {
	struct tbucw_state *st = aut->states[state_index];
	int *succ = (int*)malloc(((st->nb_out_tran)+1)*sizeof(int));
	int i;
	succ[0] = st->nb_out_tran;
	for(i=0; i<st->nb_out_tran; i++) {
		succ[i+1] = st->out_tran[i]->state_to->state_label;
	}
	return succ;
}

/** Returns P_I if player == P_O, P_O otherwise **/
char
switch_player(char player) {
	if(player == P_I) {
		return P_O;
	}
	else {
		return P_I;
	}
}

/** Prints the tbucw statistics (nb_states, nb_accepting_states, ...) **/
void
print_tbucw_stats(tbucw *aut) {
    int i, nb_accepting_states=0, nb_unbounded_states=0, nb_I_states=0, nb_O_states=0;
	for(i=0; i<aut->nb_states; i++) {
		if(aut->states[i]->is_accepting == TRUE) {
			nb_accepting_states++;
		}
		if(aut->states[i]->unbounded == TRUE) {
			nb_unbounded_states++;
		}
		if(aut->states[i]->player == P_I) {
			nb_I_states++;
		}
		if(aut->states[i]->player == P_O) {
			nb_O_states++;
		}
	}

	printf("nb states: %d (Environment: %d/System: %d)\n", aut->nb_states, nb_I_states, nb_O_states);
	printf("nb accepting states: %d\n", nb_accepting_states);
	printf("nb unbounded states: %d\n", nb_unbounded_states);
}

/** Prints the tbucw **/
void
print_tbucw(tbucw *aut) {
	int i, j;
	for (i=0; i<aut->nb_states; i++) {
		printf("State %d, labeled %d", (i+1), (aut->states[i]->state_label+1));
		if(aut->initial_state_index == i) {
			printf(", initial");
		}
		if(aut->states[i]->is_accepting) {
			printf(", final");
		}
		if(aut->states[i]->unbounded == TRUE) {
			printf(", unbounded");
		}
		if(aut->states[i]->is_trash == TRUE) {
			printf(", trash state");
		}

		printf(", belongs to Player %d :\n", aut->states[i]->player);
		printf("        -Incoming transitions :\n");
		for (j=0; j<aut->states[i]->nb_in_tran; j++) {
			//printf("%d\n", aut->states[i]->in_tran[j]);
			printf("        ::from %d, labeled ", ((aut->states[i]->in_tran[j]->state_from->state_label)+1));
			print_label(aut, aut->states[i]->in_tran[j]->label, switch_player(aut->states[i]->player));
			printf("\n");
		}
		printf("        -Outgoing transitions :\n");
		for(j=0; j<aut->states[i]->nb_out_tran; j++) {
			printf("        ::to %d, labeled ", ((aut->states[i]->out_tran[j]->state_to->state_label)+1));
			print_label(aut, aut->states[i]->out_tran[j]->label, aut->states[i]->player);
			printf("\n");
		}
	}
}

/** Prints the label which is a DNF formula **/
void
print_label(tbucw *aut, label* label, char player) {
	int i, nb_prop;
	char **prop;
	if(player == P_I) {
		nb_prop = aut->alphabet->input_size;
		prop = aut->alphabet->input;
	}
	else {
		nb_prop = aut->alphabet->output_size;;
		prop = aut->alphabet->output;
	}
	for(i=0; i<label->disjunction_size; i++) {
		print_formula(label->disjunction[i], nb_prop, prop);

		if(i != label->disjunction_size-1) {
			printf(" U ");
		}
	}
}

/** Prints a formula which corresponds to a conjunction of propositions **/
void
print_formula(LABEL_BIT_REPRES *formula, int nb_prop, char **prop) {
	int i, k, global_i, init_value, conjunction_size;
	LABEL_BIT_REPRES mask;
	char is_true;

	conjunction_size = ceil((nb_prop*1.0)/(LABEL_BIT_RANGE/2));
	if((nb_prop%(LABEL_BIT_RANGE/2)) == 0) {
		init_value = 0;
	}
	else {
		init_value = (LABEL_BIT_RANGE/2)-nb_prop%(LABEL_BIT_RANGE/2);
	}
	global_i = 0;
	is_true = FALSE;

	char label_true = TRUE;
	int comp;
	if((nb_prop%(LABEL_BIT_RANGE/2)) == 0) {
		comp = LABEL_BIT_RANGE+1;
	}
	else {
		comp = 2*(nb_prop%(LABEL_BIT_RANGE/2));
	}
	mask = build_int(comp-1, comp);

	if((mask & formula[0]) == mask) {
		comp = 2*LABEL_BIT_RANGE;
		for(i=1; i<conjunction_size; i++) {
			mask = build_int(comp-1, comp);
			if((mask & formula[i]) != mask) {
				label_true = FALSE;
				break;
			}
		}
		if(label_true == TRUE) {
			printf("T");
			return;
		}
	}

	for(k=0; k<conjunction_size && is_true == FALSE; k++) {
		for(i=init_value; i<(LABEL_BIT_RANGE/2); i++, global_i++) {
			mask = BIT(2*((LABEL_BIT_RANGE/2)-i)-1);
			if((mask & formula[k]) == mask) {
				mask = BIT(2*((LABEL_BIT_RANGE/2)-i)-2);
				if((mask & formula[k]) != mask) {
					printf("%s", prop[global_i]);
				}
			}
			else
				printf("!%s", prop[global_i]);
		}
		init_value=0;
	}
}

/** Frees the turn based automaton
 	Warning: it does not free the alphabet **/
void
free_tbucw(tbucw* aut) {
	int nb_states = aut->nb_states;
	int i;
	for(i=0; i<nb_states; i++) {
		free_tbucw_state(aut->states[i]);
	}
	free(aut);
}

/** Frees the turn based automaton state s **/
static void
free_tbucw_state(struct tbucw_state *s) {
	int nb_in_tran = s->nb_in_tran;
	int i;
	for(i=0; i<nb_in_tran; i++) {
		free_label(s->in_tran[i]->label);
		free(s->in_tran[i]);
	}
}

/** Frees the label **/
static void
free_label(label *l) {
	free(l->disjunction);
	free(l);
}

/** Optimization1: Returns TRUE if state state_index can carry counter k, FALSE otherwise (see optimization "reducing number of counters") **/
char
is_tbucw_state_unbounded(tbucw *aut, int state_index) {
	return aut->states[state_index]->unbounded;
}

/** Optimization 2: Removes states in states_to_remove_bool (+ non reachable states) from aut and replace them by 2 trash states (also updates/removes transitions correspondingly to the states suppression) **/
tbucw*
optimize_tbucw(tbucw *aut, char* states_to_remove_bool) {
	//Build a boolean array of length aut->nb_states which represents states to remove + all states which won't be reachable when states in states_to_remove_bool will be removed
	//(goal: only keep the reachable states)
	char* all_states_to_remove_bool = (char*)malloc((aut->nb_states)*sizeof(char));
	int i;
	for(i=0; i<aut->nb_states; i++) {
		all_states_to_remove_bool[i] = TRUE; //init
	}

	//Detect reachable states (set to FALSE in all_states_to_remove_bool all reachable states in aut from the initial state, considering states_to_remove_bool)
	states_to_remove_bool = detect_reachable_states(aut, aut->states[aut->initial_state_index], states_to_remove_bool, all_states_to_remove_bool);

	//Build an integer array containing indexes of all states to remove
	int nb_states_to_remove = 0;
	int* states_to_remove = (int*)malloc((aut->nb_states)*sizeof(int));
	for(i=0; i<aut->nb_states; i++) {
		if(states_to_remove_bool[i]) {
			states_to_remove[nb_states_to_remove] = i;
			nb_states_to_remove++;
		}
	}


	//Build the optimized automaton
	int nb_states_in_opt_aut = aut->nb_states-nb_states_to_remove+2; //minus those states, plus 2 trash (or trap) states (one for each player)
	tbucw *opt_aut = (tbucw*)malloc(sizeof(tbucw)+nb_states_in_opt_aut*sizeof(struct tbucw_state*));

	opt_aut->nb_states = nb_states_in_opt_aut;
	//Propositions and alphabets do not change
	opt_aut->alphabet = aut->alphabet;
	opt_aut->v_I = aut->v_I;
	opt_aut->v_O = aut->v_O;

	//TBUCW_states
	int j, nb_trash_PO_in_tran = 0, nb_trash_PI_in_tran = 0;
	struct tbucw_state *cur_state_to_remove;

	//Build two boolean arrays (to_remove and to_update) representing states in which a incoming or outgoing (resp.) transition will have to be removed or updated (resp.)) because of a state suppression
	char* to_remove = (char*)malloc((aut->nb_states)*sizeof(char)); //indicates in which states an incoming transition has to be removed
	for(i=0; i<aut->nb_states; i++) {
		to_remove[i] = FALSE; //init
	}
	for(i=0; i<nb_states_to_remove; i++) {
		cur_state_to_remove = aut->states[states_to_remove[i]];
		for(j=0; j<cur_state_to_remove->nb_out_tran; j++) {
			to_remove[cur_state_to_remove->out_tran[j]->state_to->state_label] = TRUE;
		}
	}

	char* to_update = (char*)malloc((aut->nb_states)*sizeof(char)); //indicates in which states an outgoing transition has to be updated
	for(i=0; i<aut->nb_states; i++) {
		to_update[i] = FALSE; //init
	}
	for(i=0; i<nb_states_to_remove; i++) {
		cur_state_to_remove = aut->states[states_to_remove[i]];
		if(get_player_id(aut, states_to_remove[i]) == P_O) { //cur state to remove belongs to P_O -> will be replaced by the PO_trash
			nb_trash_PO_in_tran += cur_state_to_remove->nb_in_tran; //maximum number of incoming trans
		}
		else { //cur state to remove belongs to P_I -> will be replaced by the PI_trash
			nb_trash_PI_in_tran += cur_state_to_remove->nb_in_tran; //maximum number of incoming trans
		}

		for(j=0; j<cur_state_to_remove->nb_in_tran; j++) {
			to_update[cur_state_to_remove->in_tran[j]->state_from->state_label] = TRUE;
		}
	}

	//Trash states
	struct tbucw_state *trash_PO = (struct tbucw_state*)malloc(sizeof(struct tbucw_state));
	trash_PO->in_tran = (struct tbucw_tran**)malloc((nb_trash_PO_in_tran+1)*sizeof(struct tbucw_tran*));
	trash_PO->out_tran = (struct tbucw_tran**)malloc(1*sizeof(struct tbucw_tran*));
	trash_PO->nb_in_tran = 1; //will be incremented each time a transition is added, the first in_tran comes from trash_PI
	trash_PO->nb_out_tran = 1;
	trash_PO->player = P_O;
	trash_PO->unbounded = TRUE;
	trash_PO->is_trash = TRUE;
	struct tbucw_state *trash_PI = (struct tbucw_state*)malloc(sizeof(struct tbucw_state));
	trash_PI->in_tran = (struct tbucw_tran**)malloc((nb_trash_PI_in_tran+1)*sizeof(struct tbucw_tran*));
	trash_PI->out_tran = (struct tbucw_tran**)malloc(1*sizeof(struct tbucw_tran*));
	trash_PI->nb_in_tran = 1; //will be incremented each time a transition is added, the first in_tran comes from trash_PO
	trash_PI->nb_out_tran = 1;
	trash_PI->player = P_I;
	trash_PI->state_label = nb_states_in_opt_aut-1;
	trash_PI->unbounded = TRUE;
	trash_PI->is_trash = TRUE;
	if(get_player_id(aut, aut->initial_state_index) == P_O) {
		trash_PO->is_accepting = FALSE;
		trash_PI->is_accepting = TRUE;
		trash_PO->state_label = 0; //trash state of starting player is the first element of the states array
		trash_PI->state_label = nb_states_in_opt_aut-1; //trash state of other player is the last element of the states array

	}
	else {
		trash_PO->is_accepting = TRUE;
		trash_PI->is_accepting = FALSE;
		trash_PI->state_label = 0; //trash state of starting player is the first element of the states array
		trash_PO->state_label = nb_states_in_opt_aut-1; //trash state of other player is the last element of the states array
	}

	//Transitions between trash states
	struct tbucw_tran *tr_PO_to_PI = (struct tbucw_tran*)malloc(sizeof(struct tbucw_tran));
	tr_PO_to_PI->state_from = trash_PO;
	tr_PO_to_PI->state_to = trash_PI;
	tr_PO_to_PI->label = compute_label(aut, "T", 1, P_O);
	struct tbucw_tran *tr_PI_to_PO = (struct tbucw_tran*)malloc(sizeof(struct tbucw_tran));
	tr_PI_to_PO->state_from = trash_PI;
	tr_PI_to_PO->state_to = trash_PO;
	tr_PI_to_PO->label = compute_label(aut, "T", 1, P_I);
	trash_PO->out_tran[0] = tr_PO_to_PI;
	trash_PO->in_tran[0] = tr_PI_to_PO;
	trash_PI->out_tran[0] = tr_PI_to_PO;
	trash_PI->in_tran[0] = tr_PO_to_PI;

	//Others states
	int k;
	j=1;
	for(i=0; i<aut->nb_states; i++) {
		if(states_to_remove_bool[i] == FALSE) {
			opt_aut->states[j] = get_copy_of_state(aut->states[i]);
			if(to_update[i] == TRUE) { //update outgoing transitions leading to a state in to_remove_states to a trash state
				for(k=0; k<opt_aut->states[j]->nb_out_tran; k++) {
					if(states_to_remove_bool[opt_aut->states[j]->out_tran[k]->state_to->state_label] == TRUE) {
						if(get_player_id(aut, i) == P_O) {
							opt_aut->states[j]->out_tran[k]->state_to = trash_PI; //Input trash state is the last state
							//add incoming transition to input trash state
							trash_PI->in_tran[trash_PI->nb_in_tran] = opt_aut->states[j]->out_tran[k];
							trash_PI->nb_in_tran++;
						}
						else {
							opt_aut->states[j]->out_tran[k]->state_to = trash_PO; //Output trash state is the state before the last state
							//add incoming transition to output trash state
							trash_PO->in_tran[trash_PO->nb_in_tran] = opt_aut->states[j]->out_tran[k];
							trash_PO->nb_in_tran++;
						}
					}
				}
			}
			if(to_remove[i] == TRUE) { //remove incoming transitions coming from a state in to_remove_states
				opt_aut->states[j]->in_tran = (struct tbucw_tran**)malloc((aut->states[i]->nb_in_tran)*sizeof(struct tbucw_tran*)); //it can't have more incoming transition than it had before
				opt_aut->states[j]->nb_in_tran = 0; //will be incremented each time a transition is added
				for(k=0; k<aut->states[i]->nb_in_tran; k++) {
					if(states_to_remove_bool[aut->states[i]->in_tran[k]->state_from->state_label] == FALSE) { //add all transitions which do not come from a state in states_to_remove
						opt_aut->states[j]->in_tran[opt_aut->states[j]->nb_in_tran] = aut->states[i]->in_tran[k];
						opt_aut->states[j]->nb_in_tran++;
					}
				}
			}
			j++;
		}
	}

	if(get_player_id(aut, aut->initial_state_index) == P_O) {
		opt_aut->states[0] = trash_PO;
		opt_aut->states[nb_states_in_opt_aut-1] = trash_PI;
	}
	else {
		opt_aut->states[0] = trash_PI;
		opt_aut->states[nb_states_in_opt_aut-1] = trash_PO;
	}


	//Set the state_label right in opt_aut (i.e. the label of each state is his index) (this can not be done before)
	for(i=0; i<opt_aut->nb_states; i++) {
		if(opt_aut->states[i]->state_label != i) {
			aut->states[opt_aut->states[i]->state_label]->state_label = i;
			opt_aut->states[i]->state_label = i;
		}
	}

	//Initial state : does not change if it has not been removed, becomes a trash state otherwise
	if(states_to_remove_bool[aut->initial_state_index] == TRUE) {
		opt_aut->initial_state_index = 0;
	}
	else {
		opt_aut->initial_state_index = aut->states[aut->initial_state_index]->state_label;
	}

	free(all_states_to_remove_bool);
	free(states_to_remove);
	free(to_remove);
	free(to_update);

	return opt_aut;
}

/** Returns a new tbucw_state which is a copy of st **/
struct tbucw_state*
get_copy_of_state(struct tbucw_state *st) {
	struct tbucw_state *copy = (struct tbucw_state*)malloc(sizeof(struct tbucw_state));

	copy->state_label = st->state_label;
	copy->is_accepting = st->is_accepting;
	copy->player = st->player;
	copy->unbounded = st->unbounded;
	copy->is_trash = st->is_trash;
	copy->nb_in_tran = st->nb_in_tran;
	copy->nb_out_tran = st->nb_out_tran;
	copy->in_tran = (struct tbucw_tran**)malloc((copy->nb_in_tran)*sizeof(struct tbucw_tran*));
	copy->out_tran = (struct tbucw_tran**)malloc((copy->nb_out_tran)*sizeof(struct tbucw_tran*));

	struct tbucw_tran *tr;
	int i;
	for(i=0; i<copy->nb_in_tran; i++) {
		 tr = (struct tbucw_tran*)malloc(sizeof(struct tbucw_tran));
		 tr->label = st->in_tran[i]->label;
		 tr->state_from = st->in_tran[i]->state_from;
		 tr->state_to = st->in_tran[i]->state_to;

		 copy->in_tran[i] = tr;
	}
	for(i=0; i<copy->nb_out_tran; i++) {
		 tr = (struct tbucw_tran*)malloc(sizeof(struct tbucw_tran));
		 tr->label = st->out_tran[i]->label;
		 tr->state_from = st->out_tran[i]->state_from;
		 tr->state_to = st->out_tran[i]->state_to;

		 copy->out_tran[i] = tr;
	}

	return copy;
}

/** Recursive method which detects non reachable states from *st considering the states to remove in states_to_remove_bool
 * 	(i.e. if a state s is reachable only via a state in states_to_remove_bool, then it will be set as unreachable) 			**/
char*
detect_reachable_states(tbucw* aut, struct tbucw_state *st, char* states_to_remove_bool, char* unreachable_states) {
	if(states_to_remove_bool[st->state_label] == FALSE) { //if current state is not to remove, then set it reachable and visit his successors
		unreachable_states[st->state_label] = FALSE;
		int i, cur_succ_index;
		for(i=0; i<st->nb_out_tran; i++) {
			cur_succ_index = st->out_tran[i]->state_to->state_label;
			if(unreachable_states[cur_succ_index] == TRUE) { //if this successor has not been visited yet
				unreachable_states = detect_reachable_states(aut, aut->states[cur_succ_index], states_to_remove_bool, unreachable_states);
			}
		}
	}
	return unreachable_states;
}

/** Resets the tbucw_states labels to their correct value (i.e. their index in the states array) **/
void
reset_tbucw_states_labels(tbucw* aut) {
	int i;
	for(i=0; i<aut->nb_states; i++) {
		aut->states[i]->state_label = i;
	}
}

/** Sets the dimension of weight functions **/
tbucw*
set_dimension(tbucw *aut, int dimension) {
	aut->dimension = dimension;
	return aut;
}

/** Sets the weight function defined over sigma **/
tbucw*
set_weight_function(tbucw *aut, char player, int** function) {
	int sigma_size;
	if(player == P_I) {
		sigma_size = aut->alphabet->sigma_input_size;
	}
	else {
		sigma_size = aut->alphabet->sigma_output_size;
	}

	int **v = (int**)malloc(sigma_size*sizeof(int*));
	int i, j;
	for(i=0; i<sigma_size; i++) {
		v[i] = (int*)malloc((aut->dimension)*sizeof(int));
		for(j=0; j<aut->dimension; j++) {
			v[i][j] = function[i][j];
		}
	}

	if(player == P_I) {
		aut->v_I = v;
	}
	else {
		aut->v_O = v;
	}

	return aut;
}
