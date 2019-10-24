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
#include "rbtree.h"

#define COLOR_STR(c) (c == RED ? "red" : "black")

static void removeCase2(RBTree*);
static void removeCase3(RBTree*);
static void removeCase4(RBTree*);
static void removeCase5(RBTree*);
static void removeCase6(RBTree*);

static inline RBTree* sibling(RBTree* n) {
    if (n->parent == NULL) {
        return NULL;
    } else {
        return n == n->parent->left ? n->parent->right : n->parent->left;
    }
}

static inline RBTree* uncle(RBTree* n) {
    if (n->parent == NULL) {
        return NULL;
    } else {
        return sibling(n->parent);
    }
}

static void recursivePrint(RBTree* n, int h) {
    // Essentially: a DFS with in-order printing
    if (n == NULL)
        return;
    recursivePrint(n->left, h + 1);
    printf("%d:%d (%s) ", h, n->key, COLOR_STR(n->color));
    recursivePrint(n->right, h + 1);
}

void printRBTree(RBTree* n) {
    recursivePrint(n, 0);
    printf("\n");
}

static void recursiveFree(RBTree* n) {
    // Essentially: a DFS with in-order printing
    if (n == NULL)
        return;
    recursiveFree(n->left);
    if (n->data != NULL)
        free(n->data);
    free(n);
    recursiveFree(n->right);
}

void freeRBTree(RBTree* n) {
    recursiveFree(n);
}

static bool recursiveInsert(RBTree* root, RBTree* n) {
    // Essentially: go to the leaves and insert a new
    // red node
    if (root != NULL) {
        if (n->key < root->key) {
            if (root->left != NULL) {
                return recursiveInsert(root->left, n);
            } else {
                root->left = n;
            }
        } else if (n->key == root->key) {
            return false;
        } else { // the root's key is strictly larger
            if (root->right != NULL) {
                return recursiveInsert(root->right, n);
            } else {
                root->right = n;
            }
        }
    }
    // insert n
    // Note: root may be NULL here
    n->parent = root;
    n->left = NULL;
    n->right = NULL;
    n->color = RED;
    return true;
}

static inline void attachToParent(RBTree* n, RBTree* p, RBTree* nnew) {
    if (p != NULL) {
        if (n == p->left) {
            p->left = nnew;
        } else if (n == p->right) {
            p->right = nnew;
        }
    }
    nnew->parent = p;
}

static void rotateLeft(RBTree* n) {
    RBTree* nnew = n->right;
    RBTree* p = n->parent;
    assert(nnew != NULL);  // Since the leaves of a red-black tree are empty,
                           // they cannot become internal nodes.
    n->right = nnew->left;
    nnew->left = n;
    n->parent = nnew;
    // handle other child/parent pointers
    if (n->right != NULL)
        n->right->parent = n;

    // attach to parent if n is not the root
    attachToParent(n, p, nnew);
}

static void rotateRight(RBTree* n) {
    RBTree* nnew = n->left;
    RBTree* p = n->parent;
    assert(nnew != NULL);  // Since the leaves of a red-black tree are empty,
                           // they cannot become internal nodes.
    n->left = nnew->right;
    nnew->right = n;
    n->parent = nnew;
    // handle other child/parent pointers
    if (n->left != NULL)
        n->left->parent = n;

    // attach to parent if n is not the root
    attachToParent(n, p, nnew);
}

static void repairRBTree(RBTree* n) {
    if (n->parent == NULL) {
        // the root must be black
        n->color = BLACK;
    } else if (n->parent->color == BLACK) {
        // do nothing, all properties hold
    } else if (uncle(n) != NULL && uncle(n)->color == RED) {
        // we swap colors of parent & uncle with their parent
        n->parent->color = BLACK;
        uncle(n)->color = BLACK;
        n->parent->parent->color = RED;
        // and recurse on the grandparent to fix potential problems
        repairRBTree(n->parent->parent);
    } else {
        // we now want to rotate n with its grandparent, but for this to work
        // we need n to be either the left-left grandchild or the right-right
        // grandchild
        RBTree* p = n->parent;
        RBTree* g = p->parent;
        // we start by rotating n with its parent, if needed,
        // to guarantee this property
        if (n == p->right && p == g->left) {
            rotateLeft(p);
            n = n->left;
        } else if (n == p->left && p == g->right) {
            rotateRight(p);
            n = n->right;
        }
        // now we can rotate with the grandparent, and swap the colors of
        // parent and grandparent
        p = n->parent;
        g = p->parent;
        if (n == p->left) {
            rotateRight(g);
        } else {
            rotateLeft(g);
        }
        p->color = BLACK;
        g->color = RED;
    }
}

RBTree* insertRBTree(RBTree* root, int key, void* data) {
    RBTree* n;
    n = malloc(sizeof(RBTree));
    n->key = key;
    n->data = data;
    // the recursive insertion fails if a node with the
    // same key exists already
    if (!recursiveInsert(root, n)) {
        free(n);
        return root;
    }
    // repair the tree to recover red-black properties
    repairRBTree(n);
    // find the new root
    root = n;
    while (root->parent != NULL)
        root = root->parent;
    return root;
}

bool isDominatedRBTree(RBTree* root, int k) {
    if (root == NULL) {
        return false;
    } else if (k <= root->key) {
        return true;
    } else {
        return isDominatedRBTree(root->right, k);
    }
}

static RBTree* getMax(RBTree* n) {
    if (n->right != NULL)
        return getMax(n->right);
    else
        return n;
}

static RBTree* getMin(RBTree* n) {
    if (n->left != NULL)
        return getMin(n->left);
    else
        return n;
}

static inline void replace(RBTree* n, RBTree* child) {
    if (child != NULL)
        child->parent = n->parent;
    if (n->parent != NULL) {
        if (n == n->parent->left) {
            n->parent->left = child;
        } else {
            n->parent->right = child;
        }
    }
}

static void removeCase1(RBTree* n) {
    if (n->parent != NULL)
        removeCase2(n);
}

static void removeCase2(RBTree* n) {
    // NOTE: this function requires that n is not the root
    assert(n->parent != NULL);
    RBTree* s = sibling(n);

    if (s->color == RED) {
        n->parent->color = RED;
        s->color = BLACK;
        if (n == n->parent->left) {
            rotateLeft(n->parent);
        } else {
            rotateRight(n->parent);
        }
    }
    removeCase3(n);
}

static void removeCase3(RBTree* n) {
    // NOTE: this function requires that n is not the root
    assert(n->parent != NULL);
    RBTree* s = sibling(n);
  
    if ((n->parent->color == BLACK) && (s->color == BLACK) &&
        (s->left == NULL || s->left->color == BLACK) &&
        (s->right == NULL || s->right->color == BLACK)) {
        s->color = RED;
        removeCase1(n->parent);
    } else {
        removeCase4(n);
    }
}

static void removeCase4(RBTree* n) {
    // NOTE: this function requires that n is not the root
    assert(n->parent != NULL);
    RBTree* s = sibling(n);

    if ((n->parent->color == RED) && (s->color == BLACK) &&
        (s->left == NULL || s->left->color == BLACK) && 
        (s->right == NULL || s->right->color == BLACK)) {
        s->color = RED;
        n->parent->color = BLACK;
    } else {
        removeCase5(n);
    }
}

static void removeCase5(RBTree* n) {
    // NOTE: this function requires that n is not the root
    assert(n->parent != NULL);
    RBTree* s = sibling(n);

    // even though case 2 changed the sibling to a sibling's child, the
    // sibling's child can't be red, since no red parent can have a red
    // child
    assert(s->color == BLACK);
    
    // the following statements just force the red to be on the left of the
    // left of the parent, or right of the right, so case six will rotate
    // correctly
    if ((n == n->parent->left) &&
        (s->right != NULL) &&
        (s->right->color == BLACK)) {
        // this holds due to cases 2-4
        assert(s->left != NULL && s->left->color == RED);
        s->color = RED;
        s->left->color = BLACK;
        rotateRight(s);
    } else if ((n == n->parent->right) &&
               (s->left != NULL) &&
               (s->left->color == BLACK)) {
        // this holds due to cases 2-4
        assert(s->right != NULL && s->right->color == RED);
        s->color = RED;
        s->right->color = BLACK;
        rotateLeft(s);
    }
    removeCase6(n);
}

static void removeCase6(RBTree* n) {
    // NOTE: this function requires that n is not the root
    assert(n->parent != NULL);
    RBTree* s = sibling(n);

    s->color = n->parent->color;
    n->parent->color = BLACK;

    if (n == n->parent->left) {
        s->right->color = BLACK;
        rotateLeft(n->parent);
    } else {
        s->left->color = BLACK;
        rotateRight(n->parent);
    }
}

static RBTree* removeOneChild(RBTree* n) {
    // NOTE: this function requires that n has at most one child
    assert(n != NULL && (n->right == NULL || n->left == NULL));
    RBTree* child = n->right == NULL ? n->right : n->left;

    if (n->color == RED) {
        // replacing is enough in this case
        replace(n, child);
    } else if (n->color == BLACK) {
        // if child can be re-painted to black then we're done,
        // otherwise things get messy
        if (child != NULL && child->color == RED){
            replace(n, child);
            child->color = BLACK;
        } else {
            // if child is NULL it is important that we remove first
            // and ONLY THEN replace (Essentially: we are using n
            // as a phantom black leaf)
            if (child == NULL) {
                removeCase1(n);
                replace(n, child);
            } else {
                replace(n, child);
                removeCase1(child);
            }
        }
    }
    // before we free n, we find the new root from it
    RBTree* root = n;
    while (root->parent != NULL)
        root = root->parent;
    free(n);
    return root;
}

RBTree* removeRBTree(RBTree* n) {
    // ideally, the node we want to remove has at most one real child; to
    // ensure this we look for the max on the left or the min on the right and
    // copy its contents and key into n
    RBTree* toRemove = NULL;
    if (n == NULL) {
        return NULL;
    } else if (n->left != NULL) {
        toRemove = getMax(n);
    } else if (n->right != NULL) {
        toRemove = getMin(n);
    }
    // it could be that n had no real child
    if (toRemove == NULL) {
        toRemove = n;
    } else {
        n->key = toRemove->key;
        n->data = toRemove->data;
    }
    return removeOneChild(toRemove);
}
