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

#include "vector.h"

/** oplus operator: min(max_value, c+n) if c != -1 and c+n>=0, -1 otherwise **/
int
oplus(int c, int n, int max_value) {
	if(c != -1 && (c+n) >= 0) {
		return MIN(max_value, c+n);
	}
	else {
		return -1;
	}
}

/** Creates a new vector of dimension d where each component is value **/
vector*
new_vector(int d, int *values, int *max_values) {
	vector *v = (vector*)malloc(sizeof(vector));
	v->dimension = d;
	v->max_values = (int*)malloc(d*sizeof(int));
	v->values = (int*)malloc(d*sizeof(int));
	int i;
	for(i=0; i<d; i++) {
		v->values[i] = values[i];
		v->max_values[i] = max_values[i];
	}

	return v;
}

/** Creates a new vector which is the exact copy of v **/
vector*
clone_vector(vector *v) {
	vector *copy = (vector*)malloc(sizeof(vector));
	copy->dimension = v->dimension;
	copy->max_values = (int*)malloc((copy->dimension)*sizeof(int));
	copy->values = (int*)malloc((copy->dimension)*sizeof(int));
	int i;
	for(i=0; i<copy->dimension; i++) {
		copy->values[i] = v->values[i];
		copy->max_values[i] = v->max_values[i];
	}

	return copy;
}

/** Returns TRUE if for all i, v1[i] <= v2[i], FALSE otherwise **/
char
compare_vectors(vector *v1, vector *v2) {
	int i;
	char smaller = TRUE;
	for(i=0; i<v1->dimension; i++) {
		if(v1->values[i] > v2->values[i]) {
			smaller = FALSE;
			break;
		}
	}
	if(smaller == TRUE) {
		return TRUE;
	}
	return FALSE;
}

/** Returns TRUE if v1[i] = v2[i] for all i, FALSE otherwise **/
char
are_vectors_equal(vector *v1, vector *v2) {
	int i;
	for(i=0; i<v1->dimension; i++) {
		if(v1->values[i] != v2->values[i]) {
			return FALSE;
		}
	}
	return TRUE;
}

/** Compute the intersection of two vectors
    the result is a new vector where each component is the maximum between the corresponding components of v1 and v2 **/
vector*
compute_vectors_intersection(vector *v1, vector *v2) {
	vector *inters = (vector*)malloc(sizeof(vector));
	inters->dimension = v1->dimension;
	inters->max_values = (int*)malloc((inters->dimension)*sizeof(int));
	inters->values = (int*)malloc((inters->dimension)*sizeof(int));
	int i;
	for(i=0; i<inters->dimension; i++) {
		inters->max_values[i] = MAX(v1->max_values[i], v2->max_values[i]);
		inters->values[i] = MAX(v1->values[i], v2->values[i]);
	}

	return inters;
}

/** Computes the predecessor of v
 	Warning: the omega function is not total and returns NULL when is not defined **/
vector*
vector_omega(vector *v, int *value) {
	vector *result = (vector*)malloc(sizeof(vector));
	result->dimension = v->dimension;
	result->max_values = (int*)malloc((result->dimension)*sizeof(int));
	result->values = (int*)malloc((result->dimension)*sizeof(int));

	int i;
	for(i=0; i<result->dimension; i++) {
		result->max_values[i] = v->max_values[i];
		if(value[i] >= 0) {
			if(value[i] > v->values[i]) {
				result->values[i] = 0;
			}
			else {
				result->values[i] = v->values[i] - value[i];
			}
		}
		else {
			if(v->values[i] - value[i] > result->max_values[i]) {
				free_vector(result);
				return NULL; // omega not defined
			}
			else {
				result->values[i] = v->values[i] - value[i];
			}
		}
	}

	return result;
}

/** Computes the successor of v by adding value to each of its component, with the oplus operator **/
vector*
vector_succ(vector *v, int *value) {
	vector *result = (vector*)malloc(sizeof(vector));
	result->dimension = v->dimension;
	result->max_values = (int*)malloc((result->dimension)*sizeof(int));
	result->values = (int*)malloc((result->dimension)*sizeof(int));

	int i;
	for(i=0; i<result->dimension; i++) {
		result->max_values[i] = v->max_values[i];
		result->values[i] = oplus(v->values[i], value[i], result->max_values[i]);
	}

	return result;
}

/** Frees the vector **/
void
free_vector(vector *v) {
	free(v->values);
	free(v->max_values);
	free(v);
}

/** Prints the vector **/
void
print_vector(vector *v) {
	printf("(");
	int i;
	for(i=0; i<v->dimension; i++) {
		printf("%d", v->values[i]);
		if(i != (v->dimension)-1) {
			printf(", ");
		}
	}
	printf(")");
}

/** Creates a new vector corresponding to the composition of vs **/
vector*
compose_vectors(vector **vs, int composition_size) {
	vector *comp = (vector*)malloc(sizeof(vector));
	comp->dimension = vs[0]->dimension;
	comp->max_values = (int*)malloc((comp->dimension)*sizeof(int));
	comp->values = (int*)malloc((comp->dimension)*sizeof(int));

	int i, j, cur_max;
	for(i=0; i<comp->dimension; i++) {
		cur_max = vs[0]->max_values[i];
		for(j=1; j<composition_size; j++) {
			cur_max = MAX(cur_max, vs[j]->max_values[i]);
		}
		comp->max_values[i] = cur_max;
	}
	for(i=0; i<comp->dimension; i++) {
		cur_max = vs[0]->values[i];
		for(j=1; j<composition_size; j++) {
			cur_max = MAX(cur_max, vs[j]->values[i]);
		}
		comp->values[i] = cur_max;
	}

	return comp;
}

