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

#include "linked_list.h"

/** Removes the last link of the linked list
 	Returns the new start of the list with first link removed
 	Warning: it does not free the element contained into the removed link **/
GList*
remove_last(GList *a_list) {
	GList *last = g_list_last(a_list);
	if(last != NULL) {
		a_list = g_list_delete_link(a_list, last); //removes and frees last from a_list
	}
	return a_list;
}

/** Removes the first link of the linked list
 	Returns the new start of the list with first link removed
 	Warning: it does not free the element contained into the removed link **/
GList*
remove_first(GList *a_list) {
	GList *first = g_list_first(a_list);
	if(first != NULL) {
		a_list = g_list_delete_link(a_list, first); //removes and frees first from a_list
	}
	return a_list;
}

/** Scans the linked list
	Removes links which contain data d such that d <= data (see compare method of data type)
	Adds a link containing data if data is maximal in the linked list
	Returns the resulting linked_list
	Warning: it does not free the elements removed from a_list or data when it is not added to a_list **/
GList*
scan_add_or_remove(GList *a_list, void* data, char (*compare)(void*, void*)) {
	GList *links_to_remove = NULL;
	GList *current_link = a_list;
	int is_maximal = TRUE, diff_nb_links = 0;
	while(current_link != NULL) {
		if(compare(data, current_link->data) == TRUE) {
			is_maximal = FALSE;
			break;
		}
		else if(compare(current_link->data, data) == TRUE) {
			links_to_remove = g_list_append(links_to_remove, current_link);
		}
		current_link = current_link->next;
	}
	if(is_maximal == TRUE) {
		while(links_to_remove != NULL) {
			a_list = g_list_delete_link(a_list, links_to_remove->data);
			links_to_remove = links_to_remove->next;
			diff_nb_links--;
		}
		a_list = g_list_append(a_list, data);
		diff_nb_links++;
	}

	g_list_free(links_to_remove);
	return a_list;
}

/** Scans the linked list
	Removes links which contain data d such that d <= data (see compare method of data type)
	Adds a link containing data if data is maximal in the linked list
	Returns the resulting linked_list
	Frees each element removed from a_list and data if it is not added to a_list **/
GList*
scan_add_or_remove_and_free(GList *a_list, void* data, char (*compare)(void*, void*), void (*free_element)(void*)) {
	GList *links_to_remove = NULL;
	GList *current_link = a_list;
	int is_maximal = TRUE, diff_nb_links = 0;
	while(current_link != NULL) {
		if(compare(data, current_link->data) == TRUE) {
			is_maximal = FALSE;
			break;
		}
		else if(compare(current_link->data, data) == TRUE) {
			links_to_remove = g_list_append(links_to_remove, current_link);
		}
		current_link = current_link->next;
	}
	if(is_maximal == TRUE) {
		while(links_to_remove != NULL) {
			free_element(((GList*)(links_to_remove->data))->data);
			a_list = g_list_delete_link(a_list, links_to_remove->data);
			links_to_remove = links_to_remove->next;
			diff_nb_links--;
		}
		a_list = g_list_append(a_list, data);
		diff_nb_links++;
	}
	else {
		free_element(data);
	}
	g_list_free(links_to_remove);
	return a_list;
}

/** Scans the linked list
 	Removes links which contains data d such that d <= data (see compare method of data type)
 	Method compare2 is used by compare (e.g. data type antichain: compare is compare_antichain and compare2 is the method used to compare the element stored by the antichain)
 	Adds a link containing data if data is maximal in the sorted list (-> add it at the right place, according to method compare3)
 	Returns the resulting linked_list
	Warning: it does not free the elements removed from a_list or data when it is not added to a_list **/
GList*
scan_add_or_remove_and_sort(GList *a_list, void* data, char (*compare)(void*, void*, char (*)(void*, void*)), char (*compare2)(void*, void*), char (*compare3)(void*, void*)) {
	GList *links_to_remove = NULL;
	GList *current_link = a_list;
	int is_maximal = TRUE, diff_nb_links = 0;
	// Check whether data is a maximal element and if an element has to be removed
	while(current_link != NULL) {
		if(compare(data, current_link->data, (void*)compare2) == TRUE) {
			links_to_remove = g_list_append(links_to_remove, current_link);
		}
		else if(compare(current_link->data, data, (void*)compare2) == TRUE) {
			is_maximal = FALSE;
			break;
		}
		current_link = current_link->next;
	}
	if(is_maximal == TRUE) {
		while(links_to_remove != NULL) {
			a_list = g_list_delete_link(a_list, links_to_remove->data);
			links_to_remove = links_to_remove->next;
			diff_nb_links--;
		}
		a_list = insert_sorted(a_list, data, (void*)compare3);
		diff_nb_links++;
	}

	g_list_free(links_to_remove);
	return a_list;
}

/** Scans the linked list
 	Removes links which contains data d such that d <= data (see compare method of data type)
 	Method compare2 is used by compare (e.g. data type antichain: compare is compare_antichain and compare2 is the method used to compare the element stored by the antichain)
 	Adds a link containing data if data is maximal in the sorted list (-> add it at the right place, according to method compare3)
	Frees each element removed from a_list and data if it is not added to a_list **/
GList*
scan_add_or_remove_and_sort_and_free(GList *a_list, void* data, char (*compare)(void*, void*, char (*)(void*, void*)), char (*compare2)(void*, void*), char (*compare3)(void*, void*), void (*free_element)(void*)) {
	GList *links_to_remove = NULL;
	GList *current_link = a_list;
	int is_maximal = TRUE, diff_nb_links = 0;
	// Check whether data is a maximal element and if an element has to be removed
	while(current_link != NULL) {
		if(compare(data, current_link->data, (void*)compare2) == TRUE) {
			links_to_remove = g_list_append(links_to_remove, current_link);
		}
		else if(compare(current_link->data, data, (void*)compare2) == TRUE) {
			is_maximal = FALSE;
			break;
		}
		current_link = current_link->next;
	}
	if(is_maximal == TRUE) {
		while(links_to_remove != NULL) {
			free_element(((GList*)(links_to_remove->data))->data);
			a_list = g_list_delete_link(a_list, links_to_remove->data);
			links_to_remove = links_to_remove->next;
			diff_nb_links--;
		}
		a_list = insert_sorted(a_list, data, (void*)compare3);
		diff_nb_links++;
	}
	else {
		free_element(data);
	}

	g_list_free(links_to_remove);
	return a_list;
}

GList*
insert_sorted(GList *list, void* data, char (*compare)(void*, void*)) {
	GList *cur_link = list;
	char insertion_done = FALSE;
	while(cur_link != NULL) { // find the right place to add the link
		if(compare(cur_link->data, data) == TRUE) { // current link contains a data greater than the new link -> add the new link just before it
			list = g_list_insert_before(list, cur_link, data);
			insertion_done = TRUE;
			break;
		}
		cur_link = cur_link->next;
	}
	if(insertion_done == FALSE) { // if the link has to be added at the end of the list
		list = g_list_append(list, data);
	}
	return list;
}

/** Returns TRUE if a_link has no next element, FALSE otherwise **/
char
is_link_null(GList *a_link) {
	if(a_link == NULL) {
		return TRUE;
	}
	return FALSE;
}

/** Returns the data of a_link **/
void*
get_link_data(GList *a_link) {
	return a_link->data;
}

/** Creates a new linked_list which is the exact copy of list **/
GList*
clone_linked_list(GList* list, void* (*clone_element)(void*)) {
	GList* copy = NULL;
	GList* curlink = list;
	while(curlink != NULL) {
		copy = g_list_append(copy, clone_element(curlink->data));
		curlink = curlink->next;
	}
	return copy;
}

/** Prints the linked list **/
void
print_linked_list(GList *list, void* (*print_element)(void*)) {
	printf("Linked List : {\n");
	GList* curlink = list;
	while(curlink != NULL) {
		printf("  ");
		print_element(curlink->data);
		printf("\n");
		curlink = curlink->next;
	}
	printf("}\n");
}
