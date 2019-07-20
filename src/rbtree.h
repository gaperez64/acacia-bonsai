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

#ifndef _RBTREE_H_

enum NodeColor {BLACK, RED};

typedef struct RBTree RBTree;
struct RBTree {
    RBTree* parent;
    RBTree* left;
    RBTree* right;
    enum NodeColor color;
    int key;
    void* data;
};

RBTree* insertRBTree(RBTree* root, RBTree* n);
void printRBTree(RBTree*);

#endif
