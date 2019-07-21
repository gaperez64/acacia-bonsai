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
#include "vecachain.h"

static void recursivePrint(RBTree* t, int i, int dim, int** v) {
    // Essentially: a DFS with in-order printing
    if (t == NULL) {
        printf("(%d", v[0]);
        for (int i = 0; i < dim; i++)
            printf(",%d", v[i]);
        printf(")\n");        
        return;
    }
    recursivePrint(t->left, i, dim, v);
    v[i] = t->key;
    recursivePrint(t->data, i + 1, dim, v);
    recursivePrint(t->right, i, dim, v);
}

void printVecAntichain(VecAntichain* achain) {
    int v[achain->dim];
    recursivePrint(achain->tree, 0, achain->dim, &v);
}

static void recursiveInsert(RBTree* t, int i, int dim, int** v) {
    // Essentially: go to the real leaves and insert a new node
    if (i + 1 == dim) {
        RBTree* node = malloc(sizeof(RBTree));
        node->key = v[i];
        node->data = NULL;
        insertRBTree(t, node);
    } else if (root 
    if (root != NULL && v[i] < root->key) {
        if (root->left != NULL) {
            recursiveInsert(root->left, n);
            return;
        } else {
            root->left = n;
        }
    } else if (root != NULL) {
        if (root->right != NULL) {
            recursiveInsert(root->right, n);
            return;
        } else {
            root->right = n;
        }
    }
    // insert n
    // Note: root may be NULL here
    n->parent = root;
    n->left = NULL;
    n->right = NULL;
    n->color = RED;
}

RBTree* insertRBTree(RBTree* root, RBTree* n) {
    recursiveInsert(root, n);
    // repair the tree to recover red-black properties
    repairRBTree(n);
    // find the new root
    root = n;
    while (root->parent != NULL)
        root = root->parent;
    return root;
}
