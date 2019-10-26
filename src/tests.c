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
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "veclist.h"

static void testsVecList() {
    int a[] = {3, 5, 1};
    DLLNode* list = newDLLNode(a);
    DLLNode* last = list;
    int b[] = {4, 8, 9};
    last = appendDLLNode(last, b);
    int c[] = {2, 6, 10};
    last = appendDLLNode(last, c);

    // test correct insertion
    assert(list != NULL);
    assert(list->data == a);
    assert(list->prev == NULL);
    DLLNode* i = list->next;
    assert(i != NULL);
    assert(i->data == b);
    assert(i->prev == list);
    DLLNode* j = i->next;
    assert(j != NULL);
    assert(j->data == c);
    assert(j->prev == i);
    assert(j->next == NULL);

    // test the print
    printDLList(list, 3);

    // test the copy
    DLLNode* list2 = copyDLList(list);
    assert(list2 != NULL);
    assert(list2->data == a);
    assert(list2->prev == NULL);
    i = list2->next;
    assert(i != NULL);
    assert(i->data == b);
    assert(i->prev == list2);
    j = i->next;
    assert(j != NULL);
    assert(j->data == c);
    assert(j->prev == i);
    assert(j->next == NULL);
    printDLList(list2, 3);

    // test the sort
    sortDLList(list2, 1);
    assert(list2 != NULL);
    assert(list2->data == a);
    assert(list2->prev == NULL);
    i = list2->next;
    assert(i != NULL);
    assert(i->data == c);
    assert(i->prev == list2);
    j = i->next;
    assert(j != NULL);
    assert(j->data == b);
    assert(j->prev == i);
    assert(j->next == NULL);
    printDLList(list2, 3);

    // test the deletion of a list
    deleteDLList(list);
    deleteDLList(list2);
}

int main() {
    testsVecList();
    return 0;
}
