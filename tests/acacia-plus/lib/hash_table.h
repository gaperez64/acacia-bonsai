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

#ifndef HASH_TABLE_H_
#define HASH_TABLE_H_

#include <glib.h>
#include "counting_function.h"
#include "tuple.h"

/** Structures **/
typedef struct {
	tuple* t;
	int sigma_index;
} hash_table_key;

/** Function prototypes **/
static guint hash_int(int);
hash_table_key* new_hash_table_key(tuple*, int);
guint hash_key(gconstpointer);
size_t get_hash_from_tuple(tuple*);
gboolean compare_keys(gconstpointer, gconstpointer);
void free_hash_table_key(hash_table_key*);

#endif /* HASH_TABLE_H_ */
