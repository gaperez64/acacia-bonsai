%{
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
 * along with HOATOOLS.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * Guillermo A. Perez
 * University of Antwerp
 * guillermoalberto.perez@uantwerpen.be
 *************************************************************************/

/* C declarations */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "simplehoa.h"
#include "hoalexer.h"

// helper functions for the parser
void yyerror(const char* str) {
    fprintf(stderr, "Parsing error: %s [line %d]\n", str, yylineno);
}
 
int yywrap() {
    return 1;
}

bool autoError = false;
bool seenHeader[12] = {false};
const char* headerStrs[] = {"", "HOA", "Acceptance", "States", "AP",
                            "controllable-AP", "acc-name", "tool",
                            "name", "Start", "Alias", "properties"};

void hdrItemError(const char* str) {
    fprintf(stderr,
            "Automaton error: more than one %s header item [line %d]\n",
            str, yylineno - 1);  // FIXME: This is shifted for some reason
    autoError = true;
}

// where we will save everything parsed
static HoaData* loadedData;

// temporary internal structures for easy insertion, essentially
// singly linked lists
typedef struct StringList {
    char* str;
    struct StringList* next;
} StringList;

typedef struct IntList {
    int i;
    struct IntList* next;
} IntList;

typedef struct TransList {
    BTree* label;
    IntList* successors;
    IntList* accSig;
    struct TransList* next;
} TransList;

typedef struct StateList {
    int id;
    char* name;
    BTree* label;
    IntList* accSig;
    TransList* transitions;
    struct StateList* next;
} StateList;

typedef struct AliasList {
    char* alias;
    BTree* labelExpr;
    struct AliasList* next;
} AliasList;

static StringList* tempAps = NULL;
static StringList* tempAccNameParameters = NULL;
static StringList* tempProperties = NULL;
static IntList* tempStart = NULL;
static IntList* tempCntAPs = NULL;
static StateList* tempStates = NULL;
static AliasList* tempAliases = NULL;

// list management functions
static IntList* newIntNode(int val) {
    IntList* list = (IntList*) malloc(sizeof(IntList));
    list->i = val;
    list->next = NULL;
    return list;
}

static IntList* prependIntNode(IntList* node, int val) {
    IntList* newHead = (IntList*) malloc(sizeof(IntList));
    newHead->i = val;
    newHead->next = node;
    return newHead;
}

static StringList* prependStrNode(StringList* node, char* str) {
    StringList* newHead = (StringList*) malloc(sizeof(StringList));
    newHead->str = str;
    newHead->next = node;
    return newHead;
}

static StringList* concatStrLists(StringList* list1, StringList* list2) {
    if (list2 == NULL)
        return list1;
    if (list1 == NULL)
        return list2;

    StringList* cur = list1;
    while (cur->next != NULL)
        cur = cur->next;
    cur->next = list2;
    return list1;
}

static IntList* concatIntLists(IntList* list1, IntList* list2) {
    if (list2 == NULL)
        return list1;
    if (list1 == NULL)
        return list2;

    IntList* cur = list1;
    while (cur->next != NULL)
        cur = cur->next;
    cur->next = list2;
    return list1;
}

static char** simplifyStrList(StringList* list, int* cnt) {
    (*cnt) = 0;
    if (list == NULL) return NULL;
    StringList* cur = list;
    while (cur != NULL) {
        (*cnt)++;
        cur = cur->next;
    }
    // we copy them to a plain array while deleting nodes
    cur = list;
    int i = 0;
    char** dest = (char**) malloc(sizeof(char*) * (*cnt));
    while (cur != NULL) {
        StringList* next = cur->next;
        assert(cur->str != NULL);
        dest[i++] = cur->str;
        free(cur);
        cur = next;
    }
    return dest;
}

static int* simplifyIntList(IntList* list, int* cnt) {
    (*cnt) = 0;
    if (list == NULL) return NULL;
    IntList* cur = list;
    while (cur != NULL) {
        (*cnt)++;
        cur = cur->next;
    }
    // we copy them to a plain array while deleting nodes
    cur = list;
    int i = 0;
    int* dest = (int*) malloc(sizeof(int) * (*cnt));
    while (cur != NULL) {
        IntList* next = cur->next;
        dest[i++] = cur->i;
        free(cur);
        cur = next;
    }
    return dest;
}

// more list management functions
static StateList* newStateNode(int id, char* name, BTree* label, IntList* accSig) {
    StateList* list = (StateList*) malloc(sizeof(StateList));
    list->id = id;
    list->name = name;
    list->label = label;
    list->accSig = accSig;
    list->transitions = NULL;
    return list;
}

static StateList* prependStateNode(StateList* node, StateList* newNode,
                            TransList* transitions) {
    newNode->next = node;
    newNode->transitions = transitions;
    return newNode;
}

static TransList* prependTransNode(TransList* node , BTree* label,
                            IntList* successors, IntList* accSig) {
    TransList* newHead = (TransList*) malloc(sizeof(TransList));
    newHead->label = label;
    newHead->successors = successors;
    newHead->accSig = accSig;
    newHead->next = node;
    return newHead;
}

static AliasList* prependAliasNode(AliasList* node, char* alias, BTree* labelExpr) {
    AliasList* newHead = (AliasList*) malloc(sizeof(AliasList));
    newHead->alias = alias;
    newHead->next = node;
    newHead->labelExpr = labelExpr;
    newHead->next = node;
    return newHead;
}

static Transition* simplifyTransList(TransList* list, int* cnt) {
    (*cnt) = 0;
    if (list == NULL) return NULL;
    TransList* cur = list;
    while (cur != NULL) {
       (*cnt)++;
       cur = cur->next;
    }
    // we copy them to a plain array while deleting nodes
    cur = list;
    int i = 0;
    Transition* dest = (Transition*) malloc(sizeof(Transition) * (*cnt));
    while (cur != NULL) {
        TransList* next = cur->next;
        dest[i].label = cur->label;
        dest[i].successors = simplifyIntList(cur->successors,
                                             &(dest[i].noSucc));
        dest[i].accSig = simplifyIntList(cur->accSig,
                                         &(dest[i].noAccSig));
        i++;
        free(cur);
        cur = next;
    }
    return dest;
}

static State* simplifyStateList(StateList* list, int* cnt) {
    (*cnt) = 0;
    if (list == NULL) return NULL;
    StateList* cur = list;
    while (cur != NULL) {
        (*cnt)++;
        cur = cur->next;
    }
    // we copy them to a plain array while deleting nodes
    cur = list;
    int i = 0;
    State* dest = (State*) malloc(sizeof(State) * (*cnt));
    while (cur != NULL) {
        StateList* next = cur->next;
        dest[i].id = cur->id;
        dest[i].name = cur->name;
        dest[i].label = cur->label;
        dest[i].accSig = simplifyIntList(cur->accSig, &(dest[i].noAccSig));
        dest[i].transitions = simplifyTransList(cur->transitions,
                                                &(dest[i].noTrans));
        i++;
        free(cur);
        cur = next;
    }
    return dest;
}

static Alias* simplifyAliasList(AliasList* list, int* cnt) {
    (*cnt) = 0;
    if (list == NULL) return NULL;
    AliasList* cur = list;
    while (cur != NULL) {
        (*cnt)++;
        cur = cur->next;
    }
    // we copy them to a plain array while deleting nodes
    cur = list;
    int i = 0;
    Alias* dest = (Alias*) malloc(sizeof(Alias) * (*cnt));
    while (cur != NULL) {
        AliasList* next = cur->next;
        dest[i].alias = cur->alias;
        dest[i].labelExpr = cur->labelExpr;
        i++;
        free(cur);
        cur = next;
    }
    return dest;
}

// tree management functions
static BTree* boolBTree(bool b) {
    BTree* created = (BTree*) malloc(sizeof(BTree));
    created->left = NULL;
    created->right = NULL;
    created->alias = NULL;
    created->type = NT_BOOL;
    created->id = b ? 1 : 0;
    return created;
}

static BTree* andBTree(BTree* u, BTree* v) {
    BTree* created = (BTree*) malloc(sizeof(BTree));
    created->left = u;
    created->right = v;
    created->alias = NULL;
    created->type = NT_AND;
    created->id = -1;
    return created;
}

static BTree* orBTree(BTree* u, BTree* v) {
    BTree* created = (BTree*) malloc(sizeof(BTree));
    created->left = u;
    created->right = v;
    created->alias = NULL;
    created->type = NT_OR;
    created->id = -1;
    return created;
}

static BTree* notBTree(BTree* u) {
    BTree* created = (BTree*) malloc(sizeof(BTree));
    created->left = u;
    created->right = NULL;
    created->alias = NULL;
    created->type = NT_NOT;
    created->id = -1;
    return created;
}

static BTree* aliasBTree(char* alias) {
    BTree* created = (BTree*) malloc(sizeof(BTree));
    created->left = NULL;
    created->right = NULL;
    created->alias = alias;
    created->type = NT_ALIAS;
    created->id = -1;
    return created;
}

static BTree* apBTree(int id) {
    BTree* created = (BTree*) malloc(sizeof(BTree));
    created->left = NULL;
    created->right = NULL;
    created->alias = NULL;
    created->type = NT_AP;
    created->id = id;
    return created;
}

static BTree* accidBTree(NodeType type, int id, bool negated) {
    BTree* tree = (BTree*) malloc(sizeof(BTree));
    tree->left = NULL;
    tree->right = NULL;
    tree->alias = NULL;
    tree->type = NT_SET;
    tree->id = id;

    if (negated) {
        BTree* original = tree;
        tree = (BTree*) malloc(sizeof(BTree));
        tree->left = original;
        tree->right = NULL;
        tree->alias = NULL;
        tree->type = NT_NOT;
        tree->id = -1;
    }

    BTree* created = (BTree*) malloc(sizeof(BTree));
    created->left = tree;
    created->right = NULL;
    created->alias = NULL;
    created->type = type;
    created->id = -1;
    return created;
}

%}

/* Yacc declarations: Tokens/terminal used in the grammar */

%locations

/* HEADER TOKENS */
/* compulsory: must appear exactly once */
%token HOAHDR 1 ACCEPTANCE 2 /* indexed from 1 since 0 means EOF for bison */
/* at most once */
%token STATES 3 AP 4 CNTAP 5 ACCNAME 6 TOOL 7 NAME 8
/* multiple */
%token START 9 ALIAS 10 PROPERTIES 11

/* OTHERS */
%token LPAR "("
%token RPAR ")"
%token LBRACE "{"
%token RBRACE "}"
%token LSQBRACE "["
%token RSQBRACE "]"
%token BOOLOR "|"
%token BOOLAND "&"
%token BOOLNOT "!"
%token STATEHDR INF FIN BEGINBODY ENDBODY

%union {
    int number;
    char* string;
    bool boolean;
    NodeType nodetype;
    void* numlist;
    void* strlist;
    void* trlist;
    void* statelist;
    BTree* tree;
}

%token <string> STRING IDENTIFIER ANAME HEADERNAME
%token <number> INT
%token <boolean> BOOL

%type <string> maybe_string
%type <number> header_item header_list
%type <numlist> state_conj int_list maybe_accsig
%type <strlist> string_list id_list boolintid_list
%type <trlist> trans_list
%type <statelist> statespec_list state_name
%type <nodetype> accid
%type <tree> acceptance_cond acc_cond_conj acc_cond_atom
%type <tree> label_expr lab_exp_conj lab_exp_atom maybe_label

%%
/* Grammar rules and actions follow */

automaton: header BEGINBODY body ENDBODY
         {
            if (!seenHeader[HOAHDR]) /* redundant because of the grammar */
                yyerror("No HOA: header item");
            if (!seenHeader[ACCEPTANCE])
                yyerror("No Acceptance: header item");
         };

header: format_version header_list;

format_version: HOAHDR IDENTIFIER
              {
                  loadedData->version = $2;
                  if (seenHeader[HOAHDR])
                      hdrItemError("HOA:");
                  else
                      seenHeader[HOAHDR] = true;
              };

header_list: /* empty */ { /* no new item, nothing to check */ }
           | header_list header_item
           {
               if ($2 <= 8) {
                   if (seenHeader[$2])
                       hdrItemError(headerStrs[$2]);
                   else
                       seenHeader[$2] = true;
               }
           };

header_item: STATES INT                        {
                                                 loadedData->noStates = $2;
                                                 $$ = STATES;
                                               }
           | START state_conj                  {
                                                 tempStart =
                                                    concatIntLists(
                                                        tempStart,
                                                        $2
                                                    );
                                                 $$ = START;
                                               }
           | AP INT string_list                {
                                                 loadedData->noAPs = $2;
                                                 tempAps = $3;
                                                 $$ = AP;
                                               }
           | CNTAP int_list                    {
                                                 tempCntAPs = $2;
                                                 $$ = CNTAP;
                                               }
           | ALIAS ANAME label_expr            {
                                                 tempAliases =
                                                    prependAliasNode(
                                                        tempAliases,
                                                        $2, $3
                                                    );
                                                 $$ = ALIAS;
                                               }
           | ACCEPTANCE INT acceptance_cond    { 
                                                 loadedData->noAccSets = $2;
                                                 loadedData->acc = $3;
                                                 $$ = ACCEPTANCE;
                                               }
           | ACCNAME IDENTIFIER boolintid_list { 
                                                 loadedData->accNameID = $2;
                                                 tempAccNameParameters
                                                    = concatStrLists(
                                                        tempAccNameParameters,
                                                        $3
                                                    );
                                                 $$ = ACCNAME;
                                               }
           | TOOL STRING maybe_string          { $$ = TOOL; }
           | NAME STRING                       {
                                                 loadedData->name = $2;
                                                 $$ = NAME;
                                               }
           | PROPERTIES id_list                { 
                                                 tempProperties =
                                                     concatStrLists(
                                                         tempProperties,
                                                         $2
                                                     );
                                                 $$ = PROPERTIES;
                                               }
           | HEADERNAME boolintstrid_list      { free($1); $$ = HEADERNAME; }
           ;

state_conj: INT                 { $$ = newIntNode($1); }
          | INT "&" state_conj  { $$ = prependIntNode($3, $1); }
          ;

label_expr: lab_exp_conj                { $$ = $1; }
          | label_expr "|" lab_exp_conj { $$ = orBTree($1, $3); }
          ;
          
lab_exp_conj: lab_exp_atom                  { $$ = $1; }
            | lab_exp_conj "&" lab_exp_atom { $$ = andBTree($1, $3); }
            ;

lab_exp_atom: BOOL               { $$ = boolBTree($1); }
            | INT                { $$ = apBTree($1); }
            | ANAME              { $$ = aliasBTree($1); }
            | "!" lab_exp_atom   { $$ = notBTree($2); }
            | "(" label_expr ")" { $$ = $2; }
            ;

acceptance_cond: acc_cond_conj                     { $$ = $1; }
               | acceptance_cond "|" acc_cond_conj { $$ = orBTree($1, $3); }
               ;
               
acc_cond_conj: acc_cond_atom                   { $$ = $1; }
             | acc_cond_conj "&" acc_cond_atom { $$ = andBTree($1, $3); }
             ;
             
acc_cond_atom: accid "(" INT ")"       { $$ = accidBTree($1, $3, false); }
             | accid "(" "!" INT ")"   { $$ = accidBTree($1, $4, true); }
             | "(" acceptance_cond ")" { $$ = $2; }
             | BOOL                    { $$ = boolBTree($1); }
             ; 

accid: FIN { $$ = NT_FIN; }
     | INF { $$ = NT_INF; }
     ;

boolintid_list: /* empty */               { $$ = NULL; }
              | BOOL boolintid_list       { 
                                            $$ = $1 ? prependStrNode($2,
                                                                     strdup("True"))
                                                : prependStrNode($2,
                                                                 strdup("False"));
                                          }
              | INT boolintid_list        {
                                            char buffer[66];
                                            sprintf(buffer, "%d", $1);
                                            $$ = prependStrNode($2,
                                                                strdup(buffer));
                                          }
              | IDENTIFIER boolintid_list { $$ = prependStrNode($2, $1); }
              ;

boolintstrid_list: /* empty */
                 | boolintstrid_list BOOL
                 | boolintstrid_list INT
                 | boolintstrid_list STRING
                 | boolintstrid_list IDENTIFIER { free($2); };

string_list: /* empty */        { $$ = NULL; }
           | STRING string_list { $$ = prependStrNode($2, $1); }
           ;

id_list: /* empty */        { $$ = NULL; }
       | IDENTIFIER id_list { $$ = prependStrNode($2, $1); }
       ;

body: statespec_list
    { tempStates = $1; }
    ;

statespec_list: /* empty */ { $$ = NULL; }
              | state_name trans_list statespec_list
              {
                $$ = prependStateNode($3, $1, $2);
              }
              ;

state_name: STATEHDR maybe_label INT maybe_string maybe_accsig
          { $$ = newStateNode($3, $4, $2, $5); }
          ;

maybe_label: /* empty */        { $$ = NULL; }
           | "[" label_expr "]" { $$ = $2; }
           ;

maybe_string: /* empty */ { $$ = NULL; }
            | STRING      { $$ = $1; }
            ;

maybe_accsig: /* empty */      { $$ = NULL; }
            | "{" int_list "}" { $$ = $2; }
            ;

int_list: /* empty */  { $$ = NULL; }
        | INT int_list { $$ = prependIntNode($2, $1); }
        ;

trans_list: /* empty */ { $$ = NULL; }
          | maybe_label state_conj maybe_accsig trans_list
          { $$ = prependTransNode($4, $1, $2, $3); }
          ;

%%
/* Additional C code */
  
int parseHoa(FILE* input, HoaData* data) {
    loadedData = data;
    yyin = input;
    int ret = yyparse();

    // Last (semantic) checks:
    bool semanticError = false;
    // let us check that the number of APs makes sense
    int noAPs = 0;
    for (StringList* it = tempAps; it != NULL; it = it->next)
        noAPs++;
    if (noAPs != loadedData->noAPs) {
        fprintf(stderr,
                "Semantic error: the number and list of atomic propositions "
                "do not match (%d vs %d)\n", noAPs, loadedData->noAPs);
        semanticError = true;
    }

    // clean up internal handy elements
    loadedData->aps = simplifyStrList(tempAps, &(loadedData->noAPs));
    loadedData->accNameParameters = simplifyStrList(tempAccNameParameters,
                                                    &(loadedData->noANPs));
    loadedData->properties = simplifyStrList(tempProperties,
                                             &(loadedData->noProps));
    loadedData->start = simplifyIntList(tempStart, &(loadedData->noStart));
    loadedData->cntAPs = simplifyIntList(tempCntAPs, &(loadedData->noCntAPs));
    loadedData->states = simplifyStateList(tempStates, &(loadedData->noStates));
    loadedData->aliases = simplifyAliasList(tempAliases,
                                            &(loadedData->noAliases));
    
    return ret | autoError | semanticError;
}
