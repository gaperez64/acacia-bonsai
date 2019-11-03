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

static int median(VLNode* list, int idx, int len) {
    assert(len > 0 && list != NULL);
    int mid = (int) floor(len / 2.0);
    VLNode* node = list;
    for (int i = 1; i < mid; i++) {  // len is 1-indexed, so is mid
        assert(node->next != NULL);
        node = node->next;
    }
    assert(node->data != NULL);
    return node->data[idx];
}

static KDTNode* newLeaf(int* data) {
    KDTNode* n = malloc(sizeof(KDTNode));
    n->data = data;
    n->lte = NULL;
    n->gt = NULL;
    n->guard = -1;
    return n;
}

/**
 * NOTE: This method takes care of freeing the doubly-linked lists but not the
 * memory containing their addresses.
 */
static KDTNode* recursiveKDNode(VLNode** perdim, int dim, int len, int idx) {
    assert(len > 0);
    if (len == 1) {
        int* vec = perdim[0]->data;
        deleteVList(perdim[0]);
        return newLeaf(vec);
    }
    int med = median(perdim[idx], idx, len);
    // all lists have to be split based on the median
    VLNode* lteSorted[dim];
    VLNode* gtSorted[dim];
    int lteLen;
    int gtLen;
    for (int i = 0; i < dim; i++) {
        VLNode* lteLast = NULL;
        VLNode* gtLast = NULL;
        lteLen = 0;
        gtLen = 0;
        for (VLNode* n = perdim[i]; n != NULL; n = n->next) {
            if (n->data[idx] <= med) {
                if (lteLast == NULL) {
                    lteSorted[i] = newVLNode(n->data);
                    lteLast = lteSorted[i];
                } else {
                    lteLast = appendVLNode(lteLast, n->data);
                }
                lteLen++;
            } else {
                if (gtLast == NULL) {
                    gtSorted[i] = newVLNode(n->data);
                    gtLast = gtSorted[i];
                } else {
                    gtLast = appendVLNode(gtLast, n->data);
                }
                gtLen++;
            }
        }
        assert(lteLen + gtLen == len);
        // the original list is now redundant
        deleteVList(perdim[i]);
    }
    // make recursive calls with next dimension
    int nextIdx = (idx + 1) % dim;
    KDTNode* tree = malloc(sizeof(KDTNode));
    tree->guard = med;
    tree->data = NULL;
    tree->lte = recursiveKDNode(lteSorted, dim, lteLen, nextIdx);
    tree->gt = recursiveKDNode(gtSorted, dim, gtLen, nextIdx);
    return tree;
}

KDTNode* createKDTree(VLNode* list, int dim) {
    assert(list != NULL);
    int len = 0;
    for (VLNode* i = list; i != NULL; i = i->next)
        len++;
    VLNode* sorted[dim];
    for (int i = 0; i < dim; i++) {
        sorted[i] = copyVList(list);
        sortVList(sorted[i], i);
    }
    // the method will free the sorted VLists
    return recursiveKDNode(sorted, dim, len, 0);
}

void deleteKDTree(KDTNode* root) {
    assert(root != NULL);
    if (root->lte != NULL)
        deleteKDTree(root->lte);
    if (root->gt != NULL)
        deleteKDTree(root->gt);
    free(root);
}

static void recursivePrint(KDTNode* n, int dim, int idx) {
    assert(n != NULL);
    if (n->data == NULL) {  // internal node
        printf("guard: %d (dim %d)\n", n->guard, idx);
        int nextIdx = (idx + 1) % dim;
        recursivePrint(n->lte, dim, nextIdx);
        recursivePrint(n->gt, dim, nextIdx);
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
        if (vec[idx] <= n->guard)
            return recursiveSearch(n->lte, dim, vec, nextIdx) ||
                   recursiveSearch(n->gt, dim, vec, nextIdx);
        else  // if (vec[idx] >= n->guard)
            return recursiveSearch(n->gt, dim, vec, nextIdx);
    } else {  // leaf
        bool dominated = true;
        for (int i = 0; i < dim; i++)
            dominated &= (vec[i] <= n->data[i]);
        return dominated;
    }
}

bool isDominatedKDTree(KDTNode* root, int dim, int* vec) {
    return recursiveSearch(root, dim, vec, 0);
}
