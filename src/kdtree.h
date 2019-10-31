/* Copyright 2019, Guillermo A. Perez @ University of Antwerp
 *
 * This file is part of Acacia bonsai.
 *
 * Acacia bonsai is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Acacia bonsai is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Acacia bonsai. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _KDTREE_H_
#define _KDTREE_H_

#include <stdbool.h>

#include "veclist.h"

// A doubly-linked list to store int-array pointers
typedef struct KDTNode KDTNode;
struct KDTNode {
    int guard;
    KDTNode* lte;
    KDTNode* gt;
    int* data;
};

// creates a KDTree for the given list of vectors
KDTNode* createKDTree(DLLNode*, int);
// deletes all tree nodes,
// WARNING: this will not delete the data vectors
void deleteKDTree(KDTNode*);
// print the list for debugging purposes
void printKDTree(KDTNode*, int);
// check for domination of an element by anything in the tree
bool isDominatedKDTree(KDTNode*, int, int*);

#endif
