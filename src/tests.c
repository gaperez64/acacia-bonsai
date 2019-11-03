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
#include "kdtree.h"

static void testsVecList() {
    int a[] = {3, 5, 1};
    VLNode* list = newVLNode(a);
    VLNode* last = list;
    int b[] = {4, 8, 9};
    last = appendVLNode(last, b);
    int c[] = {2, 6, 10};
    last = appendVLNode(last, c);

    // test correct insertion
    assert(list != NULL);
    assert(list->data == a);
    VLNode* i = list->next;
    assert(i != NULL);
    assert(i->data == b);
    VLNode* j = i->next;
    assert(j != NULL);
    assert(j->data == c);
    assert(j->next == NULL);

    // test the print
    printf("Test the printing of DL lists\n");
    printVList(list, 3);

    // test the copy
    VLNode* list2 = copyVList(list);
    assert(list2 != NULL);
    assert(list2->data == a);
    i = list2->next;
    assert(i != NULL);
    assert(i->data == b);
    j = i->next;
    assert(j != NULL);
    assert(j->data == c);
    assert(j->next == NULL);

    // test the sort
    sortVList(list2, 1);
    assert(list2 != NULL);
    assert(list2->data == a);
    i = list2->next;
    assert(i != NULL);
    assert(i->data == c);
    j = i->next;
    assert(j != NULL);
    assert(j->data == b);
    assert(j->next == NULL);

    // test the deletion of a list
    deleteVList(list);
    deleteVList(list2);
}

void testsKDTree() {
    int a[] = {3, 5, 1};
    VLNode* list = newVLNode(a);
    VLNode* last = list;
    int b[] = {4, 8, 9};
    last = appendVLNode(last, b);
    int c[] = {2, 6, 10};
    last = appendVLNode(last, c);
    int d[] = {3, 3, 4};
    last = appendVLNode(last, d);
    int e[] = {1, 2, 3};
    last = appendVLNode(last, e);

    // test KDTree creation
    KDTNode* tree = createKDTree(list, 3);

    // test print
    printf("Test printing of KDTrees\n");
    printKDTree(tree, 3);

    // test domination
    assert(isDominatedKDTree(tree, 3, a));
    int f[] = {2, 5, 9};
    assert(isDominatedKDTree(tree, 3, f));
    int x[] = {8, 1, 1};
    assert(!isDominatedKDTree(tree, 3, x));
    int y[] = {1, 1, 100};
    assert(!isDominatedKDTree(tree, 3, y));
    int z[] = {0, 0, 0};
    assert(isDominatedKDTree(tree, 3, z));
    
    // test deletion
    deleteKDTree(tree);
    deleteVList(list);
}

int main() {
    testsVecList();
    testsKDTree();
    return 0;
}
