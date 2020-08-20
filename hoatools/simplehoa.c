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

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#include "simplehoa.h"

void defaultsHoa(HoaData* data) {
    data->noStates = -1; 
    data->noAPs = -1;
    data->noStart = -1;
    data->noAliases = -1;
    data->noANPs = -1;
    data->noProps = -1;
    data->noCntAPs = -1;
    data->noAccSets = -1;
    data->start = NULL;
    data->version = NULL;
    data->aps = NULL;
    data->aliases = NULL;
    data->acc = NULL;
    data->accNameID = NULL;
    data->accNameParameters = NULL;
    data->toolName = NULL;
    data->toolVersion = NULL;
    data->name = NULL;
    data->properties = NULL;
    data->states = NULL;
    data->cntAPs = NULL;
}

// A DFS deleting in post-order
static void deleteBTree(BTree* root) {
    if (root == NULL)
        return;
    if (root->alias != NULL)
        free(root->alias);
    deleteBTree(root->left);
    deleteBTree(root->right);
    free(root);
}

static void deleteStringList(char** list, int cnt) {
    if (list == NULL) return;
    for (int i = 0; i < cnt; i++)
        if (list[i] != NULL)
            free(list[i]);
    free(list);
}

static void deleteTransList(Transition* list, int cnt) {
    if (list == NULL) return;
    for (int i = 0; i < cnt; i++) {
        deleteBTree(list[i].label);
        if (list[i].successors != NULL) free(list[i].successors);
        if (list[i].accSig != NULL) free(list[i].accSig);
    }
    free(list);
}

static void deleteStateList(State* list, int cnt) {
    if (list == NULL) return;
    for (int i = 0; i < cnt; i++) {
        if (list[i].name != NULL) free(list[i].name);
        deleteBTree(list[i].label);
        if (list[i].accSig != NULL) free(list[i].accSig);
        deleteTransList(list[i].transitions, list[i].noTrans);
    }
    free(list);
}

static void deleteAliases(Alias* list, int cnt) {
    if (list == NULL) return;
    for (int i = 0; i < cnt; i++) {
        assert(list[i].alias != NULL);
        free(list[i].alias);
        deleteBTree(list[i].labelExpr);
    }
    free(list);
}

void resetHoa(HoaData* data) {
    // Strings
    if (data->version != NULL) free(data->version);
    if (data->accNameID != NULL) free(data->accNameID);
    if (data->toolName != NULL) free(data->toolName);
    if (data->toolVersion != NULL) free(data->toolVersion);
    if (data->name != NULL) free(data->name);
    // String lists
    deleteStringList(data->aps, data->noAPs);
    deleteStringList(data->accNameParameters, data->noANPs);
    deleteStringList(data->properties, data->noProps);
    // int lists
    if (data->start != NULL) free(data->start);
    if (data->cntAPs != NULL) free(data->cntAPs);
    // BTrees
    deleteBTree(data->acc);
    // Aliases
    deleteAliases(data->aliases, data->noAliases);
    // State lists
    deleteStateList(data->states, data->noStates);
}

// DFS printing in in/pre-order
static void printBTree(BTree* root) {
    if (root == NULL)
        return;
    switch (root->type) {
        case NT_AP:
            printf("%d", root->id);
            break;
        case NT_ALIAS:
            printf("@%s", root->alias);
            break;
        case NT_AND:
            printf("AND(");
            printBTree(root->left);
            printf(",");
            printBTree(root->right);
            printf(")");
            break;
        case NT_OR:
            printf("OR(");
            printBTree(root->left);
            printf(",");
            printBTree(root->right);
            printf(")");
            break;
        case NT_FIN:
            printf("FIN(");
            printBTree(root->left);
            printf(")");
            break;
        case NT_INF:
            printf("INF(");
            printBTree(root->left);
            printf(")");
            break;
        case NT_NOT:
            printf("NOT(");
            printBTree(root->left);
            printf(")");
            break;
        case NT_SET:
            printf("%d", root->id);
            break;
        case NT_BOOL:
            if (root->id)
                printf("True");
            else
                printf("False");
    }
}

void printHoa(const HoaData* data) {
    printf("== LOADED HOA FILE ==\n");
    printf("HOA format version: %s\n", data->version);
    if (data->name != NULL)
        printf("File name: %s\n", data->name);
    printf("No. of states: %d\n", data->noStates);

    printf("Start states: ");
    for (int i = 0; i < data->noStart; i++)
        printf("%d, ", data->start[i]);
    printf("\n");

    printf("No. of atomic propositions: %d\n", data->noAPs);

    printf("Atomic propositions: ");
    for (int i = 0; i < data->noAPs; i++)
        printf("%s, ", data->aps[i]);
    printf("\n");

    printf("Controllable APs: ");
    for (int i = 0; i < data->noCntAPs; i++)
        printf("%d, ", data->cntAPs[i]);
    printf("\n");

    printf("No. of acceptance sets: %d\n", data->noAccSets);
    if (data->accNameID != NULL)
        printf("Acceptance name: %s\n", data->accNameID);

    printf("Acceptance: ");
    printBTree(data->acc);
    printf("\n");

    printf("Aliases: ");
    for (int i = 0; i < data->noAliases; i++) {
        printf("%s = ", data->aliases[i].alias);
        printBTree(data->aliases[i].labelExpr);
        printf(", ");
    }
    printf("\n");

    printf("Acceptance parameters: ");
    for (int i = 0; i < data->noANPs; i++)
        printf("%s, ", data->accNameParameters[i]);
    printf("\n");

    if (data->toolName != NULL) {
        assert(data->toolVersion != NULL);
        printf("Tool name: %s version %s\n",
               data->toolName,
               data->toolVersion);
    }

    printf("Properties: ");
    for (int i = 0; i < data->noProps; i++)
        printf("%s, ", data->properties[i]);
    printf("\n");
 
    for (int i = 0; i < data->noStates; i++) {
        printf("** State: %d", data->states[i].id);
        if (data->states[i].name != NULL)
            printf(" %s", data->states[i].name);
        printf(" **\n");
        if (data->states[i].label != NULL) {
            printf("label = ");
            printBTree(data->states[i].label);
            printf("\n");
        }
        if (data->states[i].accSig != NULL) {
            printf("acc sets = ");
            for (int j = 0; j < data->states[i].noAccSig; j++)
                printf("%d, ", data->states[i].accSig[j]);
            printf("\n");
        }
        printf("transitions:\n");
        for (int j = 0; j < data->states[i].noTrans; j++) {
            printf("to ");
            for (int k = 0; k < data->states[i].transitions[j].noSucc; k++)
                printf("%d, ", data->states[i].transitions[j].successors[k]);
            if (data->states[i].transitions[j].label != NULL) {
                printf(" with label = ");
                printBTree(data->states[i].transitions[j].label);
            }
            if (data->states[i].transitions[j].accSig != NULL) {
                printf(" acc sets = ");
                for (int k = 0; k < data->states[i].transitions[j].noAccSig; k++)
                    printf("%d, ", data->states[i].transitions[j].accSig[k]);
            }
            printf("\n");
        }
    }

    printf("== END HOA FILE DATA ==\n");
}

static bool checkAccNode(BTree* acc, bool good, int priority) {
    assert(acc != NULL);
    BTree* node = acc;

    // work with node only now
    if (!good && node->type != NT_FIN) return false;
    if (good && node->type != NT_INF) return false;
    assert(node->left != NULL);
    node = node->left;
    if (node->type != NT_SET) return false;
    if (node->id != priority) return false;
    return true;
}

static bool checkAccName(BTree* acc, int lastPriority, bool isMaxParity,
                         short resGoodPriority, int curPriority) {
    assert(acc != NULL);
    // is the current priority good?
    bool good = curPriority % 2 == resGoodPriority;

    // base case
    if ((isMaxParity && curPriority == 0)
        || (!isMaxParity && curPriority == lastPriority - 1))
        return checkAccNode(acc, good, curPriority);

    // otherwise we need to call the function recursively
    if (good && acc->type != NT_OR) return false;
    if (!good && acc->type != NT_AND) return false;
    bool checkLeft = checkAccNode(acc->left, good, curPriority);
    int nextPriority = isMaxParity ? curPriority - 1 : curPriority + 1;
    bool checkRight = checkAccName(acc->right, lastPriority, isMaxParity,
                                   resGoodPriority, nextPriority);
    if (good)
        return checkLeft || checkRight;
    else
        return checkLeft && checkRight;
}

int isParityGFG(const HoaData* data, bool* isMaxParity, short* resGoodPriority) {
    // (1) the automaton should be a parity one
    if (strcmp(data->accNameID, "parity") != 0) {
        fprintf(stderr, "Expected \"parity...\" automaton, found \"%s\" "
                        "as automaton type\n", data->accNameID);
        return 100;
    }
    bool foundOrd = false;
    bool foundRes = false;
    for (int i = 0; i < data->noANPs; i++) {
        if (strcmp(data->accNameParameters[i], "max") == 0) {
            (*isMaxParity) = true;
            foundOrd = true;
        }
        if (strcmp(data->accNameParameters[i], "min") == 0) {
            (*isMaxParity) = false;
            foundOrd = true;
        }
        if (strcmp(data->accNameParameters[i], "even") == 0) {
            (*resGoodPriority) = 0;
            foundRes = true;
        }
        if (strcmp(data->accNameParameters[i], "odd") == 0) {
            (*resGoodPriority) = 1;
            foundRes = true;
        }
    }
    if (!foundOrd) {
        fprintf(stderr, "Expected \"max\" or \"min\" in the acceptance name\n");
        return 101;
    }
    if (!foundRes) {
        fprintf(stderr, "Expected \"even\" or \"odd\" in the acceptance name\n");
        return 102;
    }
    if (!checkAccName(data->acc, data->noAccSets, *isMaxParity, *resGoodPriority,
                      (*isMaxParity) ? data->noAccSets - 1 : 0)) {
        fprintf(stderr, "Mismatch with canonical acceptance spec. for parity\n");
        return 103;
    }
    // (2) the automaton should be deterministic, complete, colored
    bool det = false;
    bool complete = false;
    bool colored = false;
    for (int i = 0; i < data->noProps; i++) {
        if (strcmp(data->properties[i], "deterministic") == 0)
            det = true;
        if (strcmp(data->properties[i], "complete") == 0)
            complete = true;
        if (strcmp(data->properties[i], "colored") == 0)
            colored = true;
    }
    if (!det) {
        fprintf(stderr, "Expected a deterministic automaton, "
                        "did not find \"deterministic\" in the properties\n");
        return 200;
    }
    if (!complete) {
        fprintf(stderr, "Expected a complete automaton, "
                        "did not find \"complete\" in the properties\n");
        return 201;
    }
    if (!colored) {
        fprintf(stderr, "Expected one acceptance set per transition, "
                        "did not find \"colored\" in the properties\n");
        return 202;
    }
    // (3) the automaton should have a unique start state
    if (data->noStart != 1) {
        fprintf(stderr, "Expected a unique start state\n");
        return 300;
    }
    return 0;
}
