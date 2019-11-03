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

#include "vecutil.h"
#include "veclist.h"

VLNode* newVLNode(int* data) {
    VLNode* n = malloc(sizeof(VLNode));
    n->next = NULL;
    n->data = data;
    return n;
}

VLNode* appendVLNode(VLNode* n, int* data) {
    assert(n != NULL);
    VLNode* m = newVLNode(data);
    n->next = m;
    return m;
}

VLNode* deleteVList(VLNode* n) {
    VLNode* next;
    for (VLNode* start = n; start != NULL; start = next) {
        VLNode* toRemove = start;
        next = start->next;
        free(toRemove);       
    }
    return NULL;
}

static void partition(VLNode* list, VLNode* first, VLNode* last, int idx,
                      VLNode** p1, VLNode** p2) {
    int pivot = last->data[idx];
    VLNode* i = first;
    VLNode* oldI;
    int* temp;
    for (VLNode* j = first; j != last; j = j->next) {
        if (j->data[idx] < pivot) {
            temp = i->data;
            i->data = j->data;
            j->data = temp;
            oldI = i;
            i = i->next;
        }
    }
    temp = i->data;
    i->data = last->data;
    last->data = temp;
    // return both the old i and i
    (*p1) = oldI;
    (*p2) = i;
}

static void quicksort(VLNode* list, VLNode* first, VLNode* last, int idx) {
    if (first != last) {
        VLNode* p1;
        VLNode* p2;
        partition(list, first, last, idx, &p1, &p2);
        if (p2 != first) {  // the partition  < pivot is nonempty
            assert(p1 != p2);  // p1 should point to the element before p2
            quicksort(list, first, p1, idx);
        }
        if (p2 != last) {  // the partition >= pivot is nonempty
            quicksort(list, p2->next, last, idx);
        }
    }
}

void sortVList(VLNode* first, int idx) {
    if (first == NULL || first->next == NULL)
        return;
    VLNode* last = first;
    while (last->next != NULL) {
        last = last->next;
    }
    quicksort(first, first, last, idx);
}

VLNode* copyVList(VLNode* original) {
    if (original == NULL)
        return NULL;
    VLNode* last = newVLNode(original->data);
    VLNode* list = last;
    for (VLNode* originalNext = original->next; originalNext != NULL;
           originalNext = originalNext->next) {
        last = appendVLNode(last, originalNext->data);
    }
    return list;
}

void printVList(VLNode* start, int dim) {
    assert(start != NULL);
    printf("[");
    printVec(start->data, dim);
    for (VLNode* n = start->next; n != NULL; n = n->next) {
        printf(", ");
        printVec(n->data, dim);
    }
    printf("]\n");
}
