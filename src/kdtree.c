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
#include <math.h>

#include "vecutil.h"
#include "veclist.h"
#include "kdtree.h"

static int median(DLLNode* list, int idx, int len) {
    float mid = floor(len / 2.0);
    DLLNode* node = list;
    for (int i = 0; i < mid; i++) {
        assert(node->next != NULL);
        node = node->next;
    }
    return node->data[idx];
}

static KDTNode* newLeaf(int* data) {
    KDTNode* n = malloc(sizeof(KDTNode));
    n->data = data;
    n->lt = NULL;
    n->gte = NULL;
    n->guard = -1;
    return n;
}

/**
 * NOTE: This method takes care of freeing the doubly-linked lists but not the
 * memory containing their addresses.
 */
static KDTNode* recursiveKDNode(DLLNode** perdim, int dim, int len, int idx) {
    assert(len > 0);
    if (len == 1) {
        int* vec = perdim[0]->data;
        deleteDLList(perdim[0]);
        return newLeaf(vec);
    }
    int med = median(perdim[idx], idx, len);
    // all lists have to be split based on the median
    DLLNode* ltSorted[dim];
    DLLNode* gteSorted[dim];
    int ltLen;
    int gteLen;
    for (int i = 0; i < dim; i++) {
        DLLNode* ltLast = NULL;
        DLLNode* gteLast = NULL;
        ltLen = 0;
        gteLen = 0;
        for (DLLNode* n = perdim[i]; n != NULL; n = n->next) {
            if (n->data[idx] < med) {
                if (ltLast == NULL) {
                    ltSorted[dim] = newDLLNode(n->data);
                    ltLast = ltSorted[dim];
                } else {
                    ltLast = appendDLLNode(ltLast, n->data);
                }
                ltLen++;
            } else {
                if (gteLast == NULL) {
                    gteSorted[dim] = newDLLNode(n->data);
                    gteLast = gteSorted[dim];
                } else {
                    gteLast = appendDLLNode(gteLast, n->data);
                }
                gteLen++;
            }
        }
        assert(ltLen + gteLen == len);
        // the original list is now redundant
        deleteDLList(perdim[i]);
    }
    // make recursive calls with next dimension
    int nextIdx = (idx + 1) % dim;
    KDTNode* tree = malloc(sizeof(KDTNode));
    tree->guard = med;
    tree->data = NULL;
    tree->lt = recursiveKDNode(ltSorted, dim, ltLen, nextIdx);
    tree->gte = recursiveKDNode(gteSorted, dim, gteLen, nextIdx);
    return tree;
}

KDTNode* createKDTree(DLLNode* list, int dim) {
    assert(list != NULL);
    int len = 0;
    for (DLLNode* i = list; i != NULL; i = i->next)
        len++;
    DLLNode* sorted[dim];
    for (int i = 0; i < dim; i++) {
        sorted[i] = copyDLList(list);
        sortDLList(sorted[i], i);
    }
    // the method will free the sorted DLLists
    return recursiveKDNode(sorted, dim, len, 0);
}

void deleteKDTree(KDTNode* root) {
    assert(root != NULL);
    if (root->lt != NULL)
        deleteKDTree(root->lt);
    if (root->gte != NULL)
        deleteKDTree(root->gte);
    free(root);
}

static void recursivePrint(KDTNode* n, int dim, int idx) {
    assert(n != NULL);
    if (n->data == NULL) {  // internal node
        printf("guard: %d\n (dim %d)", n->guard, idx);
        int nextIdx = (idx + 1) % dim;
        recursivePrint(n->lt, dim, nextIdx);
        recursivePrint(n->gte, dim, nextIdx);
    } else {  // leaf
        printf("leaf: ");
        printVec(n->data, dim);
        printf("\n");
    }
}

void printKDTree(KDTNode* root, int dim) {
    recursivePrint(root, dim, 0);
}

/**
 * This is a recursive traversal of the tree with two cases: either both
 * subtrees have a region which intersects the dominating region or
 * only one branch contains the dominating region.
 */
static bool recursiveSearch(KDTNode* n, int dim, int* vec, int idx) {
    assert(n != NULL);
    assert(vec != NULL);
    if (n->data == NULL) {  // internal node
        int nextIdx = (idx + 1) % dim;
        if (vec[idx] < n->guard) {
            return recursiveSearch(n->lt, dim, vec, nextIdx) ||
                   recursiveSearch(n->gte, dim, vec, nextIdx);
        } else if (vec[idx] > n->guard) {
            return recursiveSearch(n->gte, dim, vec, nextIdx);
        } else {  // vec[idx] == n->guard
            return true;
        }
    } else {  // leaf
        bool dominated = true;
        for (int i = 0; i < dim; i++)
            dominated &= vec[idx] <= n->data[idx];
        return dominated;
    }
}

bool isDominatedKDTree(KDTNode* root, int dim, int* vec) {
    return recursiveSearch(root, dim, vec, 0);
}
