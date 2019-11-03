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

#ifndef _VECACHAIN_H_
#define _VECACHAIN_H_

#include "kdtree.h"

// A singly-linked list to store a list of kdtrees following
// "the logarithmic method" to dynamify a static data structure
// see: J. L. Bentley and J. B. Saxe. Decomposable searching problems I:
// Static-to-dynamic transformation. J. Algorithms 1(4):301–358, 1980.
typedef struct VANode VANode;
struct VANode {
    VANode* next;
    KDTNode* tree;
};
// check for domination of an element by anything in the antichain
bool isDominatedVAntichain(VANode*, int, int*);
// insert element if incomparable to anything in the antichain
// returns true if and only if the vector was inserted
bool insertVAntichain(VANode*, int, int*);




#endif
