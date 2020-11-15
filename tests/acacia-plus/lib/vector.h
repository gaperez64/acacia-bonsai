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

#ifndef VECTOR_H_
#define VECTOR_H_

#include <stdlib.h>
#include <stdio.h>
#include <glib.h>

/** Structures **/
typedef struct {
	int dimension;
	int *max_values;
	int *values;
} vector;

/** Functions prototypes **/
int oplus(int, int, int);

vector* new_vector(int, int*, int*);
vector* clone_vector(vector*);

char compare_vectors(vector*, vector*);
char are_vectors_equal(vector*, vector*);

vector* compute_vectors_intersection(vector*, vector*);

vector* vector_omega(vector*, int*);
vector* vector_succ(vector*, int*);

void free_vector(vector*);
void print_vector(vector*);

vector* compose_vectors(vector**, int);

#endif /* VECTOR_H_ */
