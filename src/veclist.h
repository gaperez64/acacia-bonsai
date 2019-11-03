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

#ifndef _VECLIST_H_
#define _VECLIST_H_

// A singly-linked list to store int-array pointers
typedef struct VLNode VLNode;
struct VLNode {
    VLNode* next;
    int* data;
};

// creates a new node with the given data
VLNode* newVLNode(int*);
// creates a new node with the given data, adds it
// as the next of the given node, and returns a pointer
// of the new one
VLNode* appendVLNode(VLNode*, int*);
// deletes all next nodes and returns NULL,
// WARNING: this will not delete the data vectors
VLNode* deleteVList(VLNode*);
// sorts the vector list based on the given dimension index
void sortVList(VLNode*, int);
// creates a copy of the list and returns a pointer to it
VLNode* copyVList(VLNode*);
// print the list for debugging purposes
void printVList(VLNode*, int);

#endif
