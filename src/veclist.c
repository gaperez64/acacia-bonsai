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

DLLNode* newDLLNode(int* data) {
    DLLNode* n = malloc(sizeof(DLLNode));
    n->next = NULL;
    n->prev = NULL;
    n->data = data;
    return n;
}

DLLNode* appendDLLNode(DLLNode* n, int* data) {
    assert(n != NULL);
    DLLNode* m = newDLLNode(data);
    n->next = m;
    m->prev = n;
    return m;
}

DLLNode* deleteDLList(DLLNode* n) {
    for (DLLNode* start = n; start != NULL; start = start->next) {
        DLLNode* toRemove = start;
        free(toRemove);       
    }
    return NULL;
}

static DLLNode* partition(DLLNode* list, DLLNode* first, DLLNode* last, int idx) {
    int pivot = last->data[idx];
    DLLNode* i = first;
    int* temp;
    for (DLLNode* j = first; j != last; j = j->next) {
        if (j->data[idx] < pivot) {
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

static void quicksort(DLLNode* list, DLLNode* first, DLLNode* last, int idx) {
    if (first != last) {
        DLLNode* p = partition(list, first, last, idx);
        if (p != first)
            quicksort(list, first, p->prev, idx);
        if (p != last)
            quicksort(list, p->next, last, idx);
    }
}

void sortDLList(DLLNode* first, int idx) {
    if (first == NULL || first->next == NULL)
        return;
    DLLNode* last = first;
    while (last->next != NULL) {
        last = last->next;
    }
    quicksort(first, first, last, idx);
}

DLLNode* copyDLList(DLLNode* original) {
    if (original == NULL)
        return NULL;
    DLLNode* last = newDLLNode(original->data);
    DLLNode* list = last;
    DLLNode* originalNext = original->next;
    for (DLLNode* originalNext = original->next; originalNext != NULL;
           originalNext = originalNext->next) {
        last = appendDLLNode(last, originalNext->data);
    }
    return list;
}

void printDLList(DLLNode* start, int dim) {
    assert(start != NULL);
    printf("[");
    printVec(start->data, dim);
    for (DLLNode* n = start->next; n != NULL; n = n->next) {
        printf(", ");
        printVec(n->data, dim);
    }
    printf("]\n");
}
