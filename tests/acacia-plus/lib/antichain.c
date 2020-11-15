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

#include "antichain.h"

/** Creates a new empty antichain **/
antichain*
new_antichain() {
	antichain* a = (antichain*)malloc(sizeof(antichain));
	a->size = 0;
	GList *list = NULL;
	a->incomparable_elements = list;

	return a;
}

/** Adds an element to the antichain:
 	If element is incomparable to any element contained in a, element is added to a
 	If there exists x in a such that x > element (according to compare_elements function), element is not added to a and is freed
 	Otherwise, all x in a such that element > x are removed from a and freed **/
void
add_element_to_antichain_and_free(antichain* a, void *element, char (*compare_elements)(void*, void*), void (*free_element)(void*)) {
	GList *cur_link = a->incomparable_elements;
	GList *elements_to_remove = NULL;
	char incomparable = TRUE;
	while(cur_link != NULL) {
		if(compare_elements(element, cur_link->data) == TRUE) {
			incomparable = FALSE;
			break;
		}
		else if(compare_elements(cur_link->data, element) == TRUE) {
			elements_to_remove = g_list_append(elements_to_remove, cur_link);
		}
		cur_link = cur_link->next;
	}
	if(incomparable == TRUE) {
		while(elements_to_remove != NULL) {
			free_element(((GList*)(elements_to_remove->data))->data);
			a->incomparable_elements = g_list_remove_link(a->incomparable_elements, elements_to_remove->data);
			g_list_free(elements_to_remove->data);
			elements_to_remove = elements_to_remove->next;
			a->size--;
		}
		a->incomparable_elements = g_list_append(a->incomparable_elements, element);
		a->size++;
	}
	else {
		free_element(element);
	}
}

/** Adds an element to the antichain:
 	If element is incomparable to any element contained in a, element is added to a
 	If there exists x in a such that x > element (according to compare_elements function), do nothing
 	Otherwise, all x in a such that element > x are removed from a
 	Warning: any discarded element is not freed **/
void
add_element_to_antichain(antichain* a, void *element, char (*compare_elements)(void*, void*)) {
	GList *cur_link = a->incomparable_elements;
	GList *elements_to_remove = NULL;
	char incomparable = TRUE;
	while(cur_link != NULL) {
		if(compare_elements(element, cur_link->data) == TRUE) {
			incomparable = FALSE;
			break;
		}
		else if(compare_elements(cur_link->data, element) == TRUE) {
			elements_to_remove = g_list_append(elements_to_remove, cur_link);
		}
		cur_link = cur_link->next;
	}
	if(incomparable == TRUE) {
		while(elements_to_remove != NULL) {
			a->incomparable_elements = g_list_remove_link(a->incomparable_elements, elements_to_remove->data);
			g_list_free(elements_to_remove->data);
			elements_to_remove = elements_to_remove->next;
			a->size--;
		}
		a->incomparable_elements = g_list_append(a->incomparable_elements, element);
		a->size++;
	}
}

/** Computes the intersection between 2 antichains
 	First checks whether antichain1 < antichain2 (resp. antichain2 < antichain1). If so, returns antichain1 (resp. antichain2)
 	While checking antichain1 < antichain2, remembers all f1 in antichain1 s.t. there exists a f2 in antichain2 s.t. f1 < f2
 		(and do the same for antichain2 < antichain1)
 	Use that information to accelerate the intersection computation (those f1 and f2 will always be in the intersection,
 		so we can add them immediatly and skip all computation about them 														**/
antichain*
compute_antichains_intersection(antichain *antichain1, antichain *antichain2, char (*compare_elements)(void*, void*), void* (*intersection)(void*, void*), void* (*clone_element)(void*), void (*free_element)(void*)) {
	//Check whether antichain1 < antichain2 or antichain2 < antichain1 (goal: to prevent a useless intersection computation (because the result is antichain1 or antichain2)
	int* compare_ext_1 = compare_antichains_extended(antichain1, antichain2, (void*)compare_elements);
	int* compare_ext_2 = compare_antichains_extended(antichain2, antichain1, (void*)compare_elements);
	if(compare_ext_1[0] == TRUE) {
		free(compare_ext_1);
		free(compare_ext_2);

		return clone_antichain(antichain1, (void*)clone_element); //antichain1 < antichain2 -> the intersection is antichain1
	}
	else if(compare_ext_2[0] == TRUE) {
		free(compare_ext_1);
		free(compare_ext_2);

		return clone_antichain(antichain2, (void*)clone_element); //antichain2 < antichain1 -> the intersection is antichain2
	}
	else {
		antichain *antichains_intersection = new_antichain();

		//Optimize the way to compute the intersection: antichain1 (inters) antichain2 or antichain2 (inters) antichain1 (to minimize the number of iterations)
		int a1_size = antichain1->size-compare_ext_1[1]; //a1_size is the number of elements f1 in antichain1 s.t. there is no element f2 in antichain2 s.t. f1 < f2
		int a2_size = antichain2->size-compare_ext_2[1]; //a2_size is the number of elements f2 in antichain2 s.t. there is no element f1 in antichain1 s.t. f2 < f1
		int x = a1_size*antichain2->size;
		int y = a2_size*antichain2->size;
		if(y>x) { //swap the two antichains to minimize the number of iterations
			antichain* temp = antichain1;
			antichain1 = antichain2;
			antichain2 = temp;
			int* temp2 = compare_ext_1;
			compare_ext_1 = compare_ext_2;
			compare_ext_2 = temp2;
		}

		//Compute the intersection
		void *current_element_computed;
		GList *curlink_list1 = antichain1->incomparable_elements;
		GList *curlink_list2;
		int i = 2, j;
		char first_loop = TRUE;
		while(curlink_list1 != NULL) { //for each f1
			if(compare_ext_1[i] == FALSE) { //if there is no f in antichain2 s.t. f1 < f, compute all intersections for f1
				curlink_list2 = antichain2->incomparable_elements;
				j = 2;
				while(curlink_list2 != NULL) { //for each f2
					if(compare_ext_2[j] == FALSE) { //if there is no f in antichain1 s.t. f2 < f, compute the intersection between f2 and f1
						//Computes the intersection of a pair (f1, f2) where f1 (resp. f2) is a maximal element of antichain1 (resp. antichain2)
						current_element_computed = intersection(curlink_list1->data, curlink_list2->data);
						add_element_to_antichain_and_free(antichains_intersection, current_element_computed, (void*)compare_elements, (void*)free_element);
					}
					else { //there is a f in antichain1 s.t. f2 < f -> f2 will always be in the intersection -> add it once
						if(first_loop == TRUE) {
							add_element_to_antichain_and_free(antichains_intersection, clone_element(curlink_list2->data), (void*)compare_elements, (void*)free_element);
						}
					}
					//Move to next link in list2
					curlink_list2 = curlink_list2->next;
					j++;
				}
				first_loop = FALSE;
			}
			else { //there is a f in antichain2 s.t. f1 < f -> f1 will always be in the intersection -> add it and skip to the next f1
				add_element_to_antichain_and_free(antichains_intersection, clone_element(curlink_list1->data), (void*)compare_elements, (void*)free_element);
			}
			//Move to next link in list1
			curlink_list1 = curlink_list1->next;
			i++;
		}

		free(compare_ext_1);
		free(compare_ext_2);

		return antichains_intersection;
	}
}


/** Computes the union between 2 antichains **/
antichain*
compute_antichains_union(antichain *antichain1, antichain *antichain2, char (*compare_elements)(void*, void*), void (*free_element)(void*)) {
	//Initialize union to antichain1
	antichain *antichains_union = malloc(sizeof(antichain));
	antichains_union->size = antichain1->size;
	antichains_union->incomparable_elements = antichain1->incomparable_elements;

	GList *curlink = antichain2->incomparable_elements;

	//For each element of antichain2, scan the union list to find < or > elements
	while(curlink != NULL) {
		// Add curlink to the union if it is maximal and remove all links from the union smaller than curlink
		add_element_to_antichain_and_free(antichains_union, curlink->data, (void*)compare_elements, (void*)free_element);
		curlink = curlink->next;
	}

	return antichains_union;
}


/** Returns TRUE if for all maximal element e1 of antichain1, there exists a maximal element e2 of antichain 2, such that e2 is greater than e1, FALSE otherwise **/
char
compare_antichains(antichain *antichain1, antichain *antichain2, char (*compare_elements)(void*, void*)) {
	GList *curlink_list1 = antichain1->incomparable_elements;
	while(curlink_list1 != NULL) {
		if(contains_element(antichain2, curlink_list1->data, compare_elements) == FALSE) {
			return FALSE;
		}
		curlink_list1 = curlink_list1->next;
	}

	return TRUE;
}

/** Checks whether antichain1 is smaller than antichain2 (cf. compare_antichains)
 	Returns an integers array s.t. :
 		- the first element is TRUE (resp. FALSE) if antichain1 is smaller (resp. not smaller) than antichain2
 		- the second element is the number of maximal elements f1 of antichain1 s.t. there exists a maximal elements f2 in antichain2
 			s.t. f1 < f2 (according to compare_elements func)
 		- the rest of the array contains boolean values (TRUE in the ith position if for the (i-2)th maximal element of antichain1, there exists
 			a maximal f2 in antichain2 s.t. f1 < f2 (according to compare_elements func), FALSE otherwise 										 **/
static int*
compare_antichains_extended(antichain *antichain1, antichain *antichain2, char (*compare_elements)(void*, void*)) {
	int* result_array = (int*)malloc((2+antichain1->size)*sizeof(int));
	GList *curlink_list1 = antichain1->incomparable_elements;
	char found, compare = TRUE;
	int count = 0, i = 2;
	while(curlink_list1 != NULL) { //for each f1 in antichain1
		found = FALSE;
		if(contains_element(antichain2, curlink_list1->data, compare_elements) == TRUE) {
			found = TRUE; //found a f2 s.t. f1 < f2
			count++; //increment the number of f1 in antichain1 s.t. there exists an f2 in antichain2 s.t. f1 < f2
		}
		else {
			compare = FALSE; //there is no f2 s.t. f1 < f2
		}
		result_array[i] = found; //set the boolean value for f1
		i++;
		curlink_list1 = curlink_list1->next;
	}
	result_array[0] = compare; //set the compare variable (TRUE if antichain1 < antichain2, FALSE otherwise)
	result_array[1] = count; //set the number of f1 in antichain1 s.t. there exists an f2 in antichain2 s.t. f1 < f2

	return result_array;
}

/** Returns TRUE is antichain1 has more elements than antichain2, FALSE otherwise **/
char
compare_antichains_size(antichain *antichain1, antichain *antichain2) {
	if(antichain1->size >= antichain2->size) {
		return TRUE;
	}
	return FALSE;
}

/** Returns TRUE if element is in the antichain, FALSE otherwise **/
char
contains_element(antichain *a, void *element, char (*compare)(void*, void*)) {
	GList *curlink = a->incomparable_elements;
	while(curlink != NULL) {
		if(compare(element, curlink->data)) {
			return TRUE;
		}
		curlink = curlink->next;
	}

	return FALSE;
}


/** Returns TRUE if the antichain is empty (or reduced to the empty element), FALSE otherwise **/
char
is_antichain_empty(antichain *a, char (*is_empty)(void*)) {
	if(a->incomparable_elements == NULL) {
		return TRUE;
	}
	else if(a->size == 1) {
		if(is_empty(a->incomparable_elements->data)) {
			return TRUE;
		}
	}
	return FALSE;
}


/** Prints antichain, uses context to pass potential required data to print_element method **/
void
print_antichain(antichain *antichain, void (*print_element)(void*)) {
	GList *curlink = antichain->incomparable_elements;
	printf("{");
	while(curlink != NULL) {
		print_element(curlink->data);
		if(curlink->next != NULL) {
			printf(", ");
		}

		curlink = curlink->next;
	}
	printf("}\n");
}


/** Creates a new antichain which is the exact copy of a **/
antichain*
clone_antichain(antichain* a, void* (*clone_element)(void*)) {
	antichain* copy = (antichain*)malloc(sizeof(antichain));
	copy->size = a->size;
	copy->incomparable_elements = clone_linked_list(a->incomparable_elements, (void*)clone_element);
	return copy;
}


/** Frees the memory allocated to a without freeing the elements contained in a **/
void
free_antichain(antichain* a) {
	g_list_free(a->incomparable_elements);
	free(a);
}

/** Frees the memory allocated to a and frees all elements contained in a (using the free_element function) **/
void
free_antichain_full(antichain* a, void (*free_element)(void*)) {
	GList *cur_link = a->incomparable_elements;
	while(cur_link != NULL) {
		free_element(cur_link->data);
		cur_link = cur_link->next;
	}
	g_list_free(a->incomparable_elements);
	free(a);
}

/** Creates a new antichain corresponding to the composition of antichains **/
antichain*
compose_antichains(antichain** antichains, int nb_antichains, void* (*compose_elements)(void**, int, GNode*), GNode* composition_info) {
	GList *list = NULL;

	// Initialization of cur_links and cur_elements
	GList *cur_links[nb_antichains];
	void **cur_elements = (void**)malloc(nb_antichains*sizeof(void*));
	int i;
	for(i=0; i<nb_antichains; i++) {
		cur_links[i] = antichains[i]->incomparable_elements;
		cur_elements[i] = cur_links[i]->data;
	}
	char finished = FALSE;
	int j;
	while(finished == FALSE) {
		//Computes the composition of each element in cur_elements and add it to the composition list
		list = g_list_append(list, compose_elements(cur_elements, nb_antichains, composition_info));
		j = nb_antichains-1;
		while(j >= 0 && cur_links[j]->next == NULL) {
			cur_links[j] = antichains[j]->incomparable_elements;
			cur_elements[j] = cur_links[j]->data;
			j--;
		}

		if(j < 0) {
			finished = TRUE;
		}
		else {
			cur_links[j] = cur_links[j]->next;
			cur_elements[j] = cur_links[j]->data;
		}
	}
	antichain *composition = (antichain*)malloc(sizeof(antichain));
	composition->size = g_list_length(list);
	composition->incomparable_elements = list;

	return composition;
}
