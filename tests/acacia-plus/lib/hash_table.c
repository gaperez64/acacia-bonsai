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

#include "hash_table.h"

/** Hashes an int **/
static guint
hash_int(int key) {
    guint result = key;
    result = ~result + (result << 15);
    result = result ^ (result >> 12);
    result = result + (result << 2);
    result = result ^ (result >> 4);
    result = result * 2057;
    result = result ^ (result >> 16);
    return result;
}

hash_table_key*
new_hash_table_key(tuple *t, int sigma_index) {
	hash_table_key* key = (hash_table_key*)malloc(sizeof(hash_table_key));
	key->t = t;
	key->sigma_index = sigma_index;

	return key;
}

/** Hashes a hash_table_key structure **/
guint
hash_key(gconstpointer p) {
	guint result = 0x345678;
	hash_table_key* key = (hash_table_key*)p;
	cf_info* info = ((cf_info*)(key->t->cf->info->data));
	int i;
	for (i=0; i<info->cf_size_sum; i++) {
		result = (result * 1000003) ^ hash_int(key->t->cf->mapping[i]);
	}
	if(key->sigma_index >= 0) {
		result = (result * 1000003) ^ hash_int(key->sigma_index);
	}
	for(i=0; i<key->t->credits->dimension; i++) {
		result = (result * 1000003) ^ hash_int(key->t->credits->values[i]);
	}
	result = (result * 1000003) ^ hash_int(key->t->cf->player);
	if (result == -1) {
		result = -2;
	}
	return result;
}

/** Computes and returns the hash of a tuple **/
size_t
get_hash_from_tuple(tuple* t) {
	hash_table_key* key = (hash_table_key*)malloc(sizeof(hash_table_key));
	key->t = t;
	key->sigma_index = -1;
	size_t hash = (size_t)hash_key(key);
	free(key);
	return hash;
}

/** Compares the tuples contained in hash_table_keys key1 and key2 **/
gboolean
compare_keys(gconstpointer key1, gconstpointer key2) {
	hash_table_key *h_key1 = (hash_table_key*)key1;
	hash_table_key *h_key2 = (hash_table_key*)key2;

	if(h_key1->sigma_index == h_key2->sigma_index) {
		return are_tuples_equal(h_key1->t, h_key2->t);
	}
	return FALSE;
}

/** Frees an hash_table_key and the tuple in it **/
void
free_hash_table_key(hash_table_key *key) {
	free_tuple_full(key->t);
	free(key);
}
