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

#include "cache.h"

GHashTable *cache_antichains;
GHashTable *cache_tuples;
GHashTable *cache_succ;
GHashTable *cache_min_succ;

/** Initializes the cache GHashTable for the backward algorithm **/
void
initialize_cache() {
	cache_antichains = g_hash_table_new((GHashFunc)hash_key, (GEqualFunc)compare_keys);
	cache_tuples = g_hash_table_new_full((GHashFunc)hash_key, (GEqualFunc)compare_keys, (GDestroyNotify)free_hash_table_key, (GDestroyNotify)free_tuple_full_protected);
}

/** Initializes the cache GHashTable for the critical signals set procedure of the backward algorithm **/
void
initialize_cache_critical_set() {
	cache_min_succ = g_hash_table_new((GHashFunc)hash_key, (GEqualFunc)compare_keys);
	cache_succ = g_hash_table_new_full((GHashFunc)hash_key, (GEqualFunc)compare_keys, (GDestroyNotify)free_hash_table_key, (GDestroyNotify)free_tuple_full_protected);
}

/** Destroys the cache GHashTable **/
void
destroy_cache() {
	g_hash_table_destroy(cache_antichains);
	g_hash_table_destroy(cache_tuples);
}

static void
free_cache_antichains(gpointer key, gpointer value, gpointer user_data) {
	free_hash_table_key(key);
	free_antichain_full(value, (void*)free_tuple_full);
}

/** Cleans the cache GHashTable **/
void
clean_cache() {
	g_hash_table_foreach(cache_antichains, (GHFunc)free_cache_antichains, (gpointer)free_tuple_full);
	g_hash_table_remove_all(cache_antichains);

	g_hash_table_remove_all(cache_tuples);
}

/** Cleans the cache GHashTable used to compute critical signals sets **/
void
clean_cache_critical_set() {
	g_hash_table_foreach(cache_min_succ, (GHFunc)free_cache_antichains, (gpointer)free_tuple_full);
	g_hash_table_remove_all(cache_min_succ);

	g_hash_table_remove_all(cache_succ);
}

/** Adds value to the cache at (cf, sigma_index) entry **/
void
add_to_cache(GHashTable* cache, tuple* t, int sigma_index, void* value) {
	hash_table_key* key = new_hash_table_key(t, sigma_index);

	g_hash_table_insert(cache, (gconstpointer*)key, (gconstpointer*)value);
}

/** Returns the value contained in cache at (cf, sigma_index) entry **/
void*
get_from_cache(GHashTable* cache, tuple* t, int sigma_index) {
	hash_table_key* key = new_hash_table_key(t, sigma_index);

	void* value = g_hash_table_lookup(cache, key);
	free(key);

	return value;
}
