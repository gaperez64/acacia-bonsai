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

#ifndef LINKED_LIST_H
#define LINKED_LIST_H

#include <stdio.h>
#include <stdlib.h>
#include <glib.h>

/** Function prototypes **/
GList* remove_last(GList*);
GList* remove_first(GList*);
GList* scan_add_or_remove(GList*, void*, char (*)(void*, void*));
GList* scan_add_or_remove_and_free(GList*, void*, char (*)(void*, void*), void (*)(void*));
GList* scan_add_or_remove_and_sort(GList*, void*, char (*)(void*, void*, char (*)(void*, void*)), char (*)(void*, void*), char (*)(void*, void*));
GList* scan_add_or_remove_and_sort_and_free(GList*, void*, char (*)(void*, void*, char (*)(void*, void*)), char (*)(void*, void*), char (*)(void*, void*), void (*)(void*));
GList* insert_sorted(GList*, void*, char (*)(void*, void*));
char is_link_null(GList*);
void* get_link_data(GList*);
GList* clone_linked_list(GList*, void* (*)(void*));
void print_linked_list(GList*, void* (*)(void*));

#endif

