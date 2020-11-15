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

#ifndef CACHE_H
#define CACHE_H

#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include "antichain.h"
#include "hash_table.h"
#include "tuple.h"

/** Global variables **/
// Two cache memories for the backward fix point computation
extern GHashTable *cache_antichains;
extern GHashTable *cache_tuples;
// Two cache memories for the critical signals set computation
extern GHashTable *cache_succ;
extern GHashTable *cache_min_succ;

/** Function prototypes **/
void initialize_cache();
void initialize_cache_critical_set();
void destroy_cache();
static void free_cache_antichains(gpointer, gpointer, gpointer);
void clean_cache();
void clean_cache_critical_set();
void add_to_cache(GHashTable*, tuple*, int, void*);
void* get_from_cache(GHashTable*, tuple*, int);

#endif /* CACHE_H_ */
