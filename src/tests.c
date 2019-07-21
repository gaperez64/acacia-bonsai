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
#include "rbtree.h"

int main() {
    RBTree* tree = NULL;
    int nodes[] = {1, 27, 6, 8, 11, 13, 25, 17, 22, 15, 1};
    for (int i = 0; i < 11; i++) {
        tree = insertRBTree(tree, nodes[i], NULL);
    }
    printRBTree(tree);
    // we test isDominatedRBTree here
    assert(isDominatedRBTree(tree, 17));
    assert(isDominatedRBTree(tree, 7));
    assert(!isDominatedRBTree(tree, 28));
    assert(!isDominatedRBTree(tree, 100));
    return 0;
}
