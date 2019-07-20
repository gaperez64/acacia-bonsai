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
#include "rbtree.h"

int main() {
    RBTree* tree = NULL;
    RBTree* node;
    int nodes[10] = {1, 27, 6, 8, 11, 13, 25, 17, 22, 15};
    for (int i = 0; i < 10; i++) {
        node = malloc(sizeof(RBTree));
        node->key = nodes[i];
        tree = insertRBTree(tree, node);
    }
    return 0;
}
