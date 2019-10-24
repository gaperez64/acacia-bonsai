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

DLLNode* newNode(int** data) {
    DLLNode* n = malloc(sizeof(DLLNode));
    n->next = NULL;
    n->prev = NULL;
    n->data = data;
    return n;
}

DLLNode* addNext(DLLNode* n, int** data) {
    assert(n != NULL);
    DLLNode* m = newNode(data);
    n->next = m;
    m->prev = n;
    return m;
}

DLLNode* deleteTail(DLLnode* n) {
    for (DLLNode* start = n; start != NULL; start = start->next) {
        DLLNode* toRemove = start;
        free(toRemove);       
    }
    return NULL;
}

static DLLNode* partition(DLLNode* list, DLLNode* first, DLLNode* last, int dim) {
    int pivot = (last->data*)[dim];
    DLLNode* i = first;
    int** temp;
    for (DLLNode* j = first; j != last; j = j->next) {
        if ((j->data*)[dim] < pivot) {
            temp = i->data;
            i->data = j->data;
            j->data = temp;
            i = i->next;
        }
    }
    temp = i->data;
    i->data = last->data;
    last->data = temp;
    return i;
}

static void quicksort(DLLNode* list, DLLNode* first, DLLNode* last, int dim) {
    if (first != last) {
        DLLNode* p = partition(list, first, last, dim);
        quicksort(list, first, p->prev, dim);
        quicksort(list, p->next, last, dim);
    }
}

DLLNode* sort(DLLNode* first, int dim) {
    if (first == NULL || first->next == NULL)
        return first;
    DLLNode* last = first;
    while (last->next != NULL) {
        last = last->next;
    }
    quicksort(first, first, last, dim);
    // sorting is in place, so we return the same address as was given
    return first;
}

DLLNode* copyList(DLLNode* original) {
    if (original == NULL)
        return NULL;
    DLLNode* last = newNode(original->data);
    DLLNode* list = last;
    DLLNode* originalNext = original->next;
    while (DLLNode* originalNext = original->next; next != NULL;
           originalNext = originalNext->next) {
        last = addNext(last, originalNext->data);
    }
    return list;
}

static void printDLLNode(DLLNode* n, int dim) {
    assert(n != NULL);
    printf("(%d", (n->data*)[0]);
    for (int i = 1; i < dim; i++)
        printf(",%d", (n->data*)[i]);
    printf(")")
    recursivePrint(n->next, dim);
}

void printList(DLLNode* start, int dim) {
    assert(start != NULL);
    printf("[");
    printDLLNode(start, dim);
    for (DLLNode* n = start->next; n != NULL; n = n->next) {
        printf(", ");
        printDLLNode(n, dim);
    }
    printf("]");
}
