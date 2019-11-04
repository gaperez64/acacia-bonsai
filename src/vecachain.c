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

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "veclist.h"
#include "kdtree.h"
#include "vecachain.h"

// check for domination of an element by anything in the antichain
bool isDominatedVAntichain(VANode* achain, int dim, int* vec) {
    assert(achain != NULL);
    for (VANode* i = achain; i != NULL; i = i->next) {
        if (i->data != NULL && isDominatedKDTree(i->data, dim, vec))
            return true;
    }
    return false;
}

// TODO: fix the code below, to keep the antichain invariant we actually have
// to REMOVE elements from trees that are already built if they are dominated
// by vec

// insert element if incomparable to anything in the antichain
// returns true if and only if the vector was inserted
bool insertVAntichain(VANode* achain, int dim, int* vec) {
    assert(achain != NULL);
    if (isDominatedVAntichain(achain, dim, vec))
        return false;
    VANode* empty = achain;
    while (empty->next != NULL && empty->data != NULL)
        empty = empty->next;
    // either we got to the end of the list, or we found an empty slot
    if (empty->next == NULL && empty->data != NULL) {  // no empty slot
        // in this case we create a new node
        empty->next = malloc(sizeof(VANode));
        empty = empty->next;
        empty->next = NULL;
    }
    // we now have to create a new kdtree from all preceding nodes and
    // remove them
    VLNode* new = newVLNode(vec);
    for (VANode* i = achain; i != empty; i = i->next) {
        listAllKDTree(i->data, new);
        deleteKDTree(i->data);
        i->data = NULL;
    }
    // the final step is the creation of the new kdtree
    empty->data = createKDTree(new, dim);
    deleteVList(new);
    return true;
}
