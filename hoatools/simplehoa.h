/**************************************************************************
 * Copyright (c) 2019- Guillermo A. Perez
 * 
 * This file is part of HOATOOLS.
 * 
 * HOATOOLS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * HOATOOLS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with HOATOOLS. If not, see <http://www.gnu.org/licenses/>.
 * 
 * Guillermo A. Perez
 * University of Antwerp
 * guillermoalberto.perez@uantwerpen.be
 *************************************************************************/

#ifndef _SIMPLEHOA_H
#define _SIMPLEHOA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdbool.h>

/* All tree-like constructs will be BTrees, for binary tree. The
 * following enumerate structure gives us the types of nodes 
 * that could appear in such trees.
 */
typedef enum {
    NT_BOOL, NT_AND, NT_OR, NT_FIN,
    NT_INF, NT_NOT, NT_SET,
    NT_AP, NT_ALIAS
} NodeType;

typedef struct BTree {
    struct BTree* left;
    struct BTree* right;
    char* alias;
    int id;
    NodeType type;
} BTree;

typedef struct Transition {
    BTree* label;
    int* successors;
    int noSucc;
    int* accSig;
    int noAccSig;
} Transition;

typedef struct State {
    int id;
    char* name;
    BTree* label;
    int* accSig;
    int noAccSig;
    Transition* transitions;
    int noTrans;
} State;

typedef struct Alias {
    char* alias;
    BTree* labelExpr;
} Alias;

/* The centralized data structure for all data collected
 * from the file is the following.
 */
typedef struct HoaData {
    char** aps;
    int noAPs;
    char** accNameParameters;
    int noANPs;
    char** properties;
    int noProps;
    State* states;
    int noStates;
    Alias* aliases;
    int noAliases;
    int* start;
    int noStart;
    int* cntAPs;
    int noCntAPs;
    int noAccSets;
    BTree* acc;
    char* version;
    char* accNameID;
    char* toolName;
    char* toolVersion;
    char* name;
} HoaData;

// This function returns 0 if and only if parsing was successful
int parseHoa(FILE*, HoaData*);

// Defaults and destructor for centralized data structure
void defaultsHoa(HoaData*);
void deleteHoa(HoaData*);

// For debugging purposes, this prints all data in human-readable form
void printHoa(const HoaData*);

// To check if the parsed automaton is a parity one that is good-for-games
int isParityGFG(const HoaData*, bool*, short*);

#ifdef __cplusplus
}
#endif

#endif  // defining _SIMPLEHOA_H
