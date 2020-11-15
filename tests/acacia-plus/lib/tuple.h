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

#ifndef TUPLE_H_
#define TUPLE_H_

#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include "antichain.h"
#include "counting_function.h"
#include "vector.h"

/** Structures **/
typedef struct {
	counting_function *cf;
	vector *credits;
} tuple;

extern tuple *NOT_DEFINED;

/** Function prototypes **/
void set_not_defined_tuple();
tuple* new_tuple(counting_function*, vector*);
tuple* build_maximal_tuple(char, GNode*, int, int*);
tuple* build_initial_tuple(GNode*, int, int*);
tuple* add_maximal_vector_to_tuple(tuple*, int, int*);

tuple* clone_tuple(tuple*);

char is_tuple_empty(tuple*);
char compare_tuples(tuple*, tuple*);
char compare_tuples_reverse(tuple*, tuple*);
char are_tuples_equal(tuple*, tuple*);

tuple* compute_tuples_intersection(tuple*, tuple*);

tuple* tuple_omega(tuple*, int, LABEL_BIT_REPRES**);
tuple* tuple_succ(tuple*, int, alphabet_info*);
antichain* minimal_tuple_succ(tuple*, alphabet_info*);
antichain* maximal_tuple_succ(tuple*, alphabet_info*);

void free_tuple(tuple*);
void free_tuple_full(tuple*);
void free_tuple_full_protected(tuple*);
void free_not_defined_tuple();

void print_tuple(tuple*);

tuple* compose_tuples(tuple **ts, int nb_ts, GNode *composition_info);

#endif /* TUPLE_H_ */
