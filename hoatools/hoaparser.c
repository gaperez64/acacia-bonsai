/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton implementation for Bison's Yacc-like parsers in C

   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "2.3"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Using locations.  */
#define YYLSP_NEEDED 1



/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     HOAHDR = 1,
     ACCEPTANCE = 2,
     STATES = 3,
     AP = 4,
     CNTAP = 5,
     ACCNAME = 6,
     TOOL = 7,
     NAME = 8,
     START = 9,
     ALIAS = 10,
     PROPERTIES = 11,
     LPAR = 258,
     RPAR = 259,
     LBRACE = 260,
     RBRACE = 261,
     LSQBRACE = 262,
     RSQBRACE = 263,
     BOOLOR = 264,
     BOOLAND = 265,
     BOOLNOT = 266,
     STATEHDR = 267,
     INF = 268,
     FIN = 269,
     BEGINBODY = 270,
     ENDBODY = 271,
     STRING = 272,
     IDENTIFIER = 273,
     ANAME = 274,
     HEADERNAME = 275,
     INT = 276,
     BOOL = 277
   };
#endif
/* Tokens.  */
#define HOAHDR 1
#define ACCEPTANCE 2
#define STATES 3
#define AP 4
#define CNTAP 5
#define ACCNAME 6
#define TOOL 7
#define NAME 8
#define START 9
#define ALIAS 10
#define PROPERTIES 11
#define LPAR 258
#define RPAR 259
#define LBRACE 260
#define RBRACE 261
#define LSQBRACE 262
#define RSQBRACE 263
#define BOOLOR 264
#define BOOLAND 265
#define BOOLNOT 266
#define STATEHDR 267
#define INF 268
#define FIN 269
#define BEGINBODY 270
#define ENDBODY 271
#define STRING 272
#define IDENTIFIER 273
#define ANAME 274
#define HEADERNAME 275
#define INT 276
#define BOOL 277




/* Copy the first part of user declarations.  */
#line 1 "hoa.y"

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



/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

/* Enabling the token table.  */
#ifndef YYTOKEN_TABLE
# define YYTOKEN_TABLE 0
#endif

#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 420 "hoa.y"
{
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
/* Line 193 of yacc.c.  */
#line 569 "hoaparser.c"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
} YYLTYPE;
# define yyltype YYLTYPE /* obsolescent; will be withdrawn */
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif


/* Copy the second part of user declarations.  */


/* Line 216 of yacc.c.  */
#line 594 "hoaparser.c"

#ifdef short
# undef short
#endif

#ifdef YYTYPE_UINT8
typedef YYTYPE_UINT8 yytype_uint8;
#else
typedef unsigned char yytype_uint8;
#endif

#ifdef YYTYPE_INT8
typedef YYTYPE_INT8 yytype_int8;
#elif (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
typedef signed char yytype_int8;
#else
typedef short int yytype_int8;
#endif

#ifdef YYTYPE_UINT16
typedef YYTYPE_UINT16 yytype_uint16;
#else
typedef unsigned short int yytype_uint16;
#endif

#ifdef YYTYPE_INT16
typedef YYTYPE_INT16 yytype_int16;
#else
typedef short int yytype_int16;
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif ! defined YYSIZE_T && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(e) ((void) (e))
#else
# define YYUSE(e) /* empty */
#endif

/* Identity function, used to suppress warnings about constant conditions.  */
#ifndef lint
# define YYID(n) (n)
#else
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static int
YYID (int i)
#else
static int
YYID (i)
    int i;
#endif
{
  return i;
}
#endif

#if ! defined yyoverflow || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#     ifndef _STDLIB_H
#      define _STDLIB_H 1
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (YYID (0))
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined _STDLIB_H \
       && ! ((defined YYMALLOC || defined malloc) \
	     && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef _STDLIB_H
#    define _STDLIB_H 1
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (! defined yyoverflow \
     && (! defined __cplusplus \
	 || (defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL \
	     && defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss;
  YYSTYPE yyvs;
    YYLTYPE yyls;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE) + sizeof (YYLTYPE)) \
      + 2 * YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  YYSIZE_T yyi;				\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (YYID (0))
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack)					\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack, Stack, yysize);				\
	Stack = &yyptr->Stack;						\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (YYID (0))

#endif

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  6
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   97

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  34
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  26
/* YYNRULES -- Number of rules.  */
#define YYNRULES  65
/* YYNRULES -- Number of states.  */
#define YYNSTATES  111

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   277

#define YYTRANSLATE(YYX)						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const yytype_uint8 yytranslate[] =
{
       0,     3,     4,     5,     6,     7,     8,     9,    10,    11,
      12,    13,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint8 yyprhs[] =
{
       0,     0,     3,     8,    11,    14,    15,    18,    21,    24,
      28,    31,    35,    39,    43,    47,    50,    53,    56,    58,
      62,    64,    68,    70,    74,    76,    78,    80,    83,    87,
      89,    93,    95,    99,   104,   110,   114,   116,   118,   120,
     121,   124,   127,   130,   131,   134,   137,   140,   143,   144,
     147,   148,   151,   153,   154,   158,   164,   165,   169,   170,
     172,   173,   177,   178,   181,   182
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int8 yyrhs[] =
{
      35,     0,    -1,    36,    26,    52,    27,    -1,    37,    38,
      -1,     3,    29,    -1,    -1,    38,    39,    -1,     5,    32,
      -1,    11,    40,    -1,     6,    32,    50,    -1,     7,    58,
      -1,    12,    30,    41,    -1,     4,    32,    44,    -1,     8,
      29,    48,    -1,     9,    28,    56,    -1,    10,    28,    -1,
      13,    51,    -1,    31,    49,    -1,    32,    -1,    32,    21,
      40,    -1,    42,    -1,    41,    20,    42,    -1,    43,    -1,
      42,    21,    43,    -1,    33,    -1,    32,    -1,    30,    -1,
      22,    43,    -1,    14,    41,    15,    -1,    45,    -1,    44,
      20,    45,    -1,    46,    -1,    45,    21,    46,    -1,    47,
      14,    32,    15,    -1,    47,    14,    22,    32,    15,    -1,
      14,    44,    15,    -1,    33,    -1,    25,    -1,    24,    -1,
      -1,    33,    48,    -1,    32,    48,    -1,    29,    48,    -1,
      -1,    49,    33,    -1,    49,    32,    -1,    49,    28,    -1,
      49,    29,    -1,    -1,    28,    50,    -1,    -1,    29,    51,
      -1,    53,    -1,    -1,    54,    59,    53,    -1,    23,    55,
      32,    56,    57,    -1,    -1,    18,    41,    19,    -1,    -1,
      28,    -1,    -1,    16,    58,    17,    -1,    -1,    32,    58,
      -1,    -1,    55,    40,    57,    59,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   449,   449,   457,   459,   468,   469,   479,   483,   491,
     496,   500,   508,   513,   522,   523,   527,   535,   538,   539,
     542,   543,   546,   547,   550,   551,   552,   553,   554,   557,
     558,   561,   562,   565,   566,   567,   568,   571,   572,   575,
     576,   582,   588,   591,   592,   593,   594,   595,   597,   598,
     601,   602,   605,   609,   610,   616,   620,   621,   624,   625,
     628,   629,   632,   633,   636,   637
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "HOAHDR", "ACCEPTANCE", "STATES", "AP",
  "CNTAP", "ACCNAME", "TOOL", "NAME", "START", "ALIAS", "PROPERTIES",
  "\"(\"", "\")\"", "\"{\"", "\"}\"", "\"[\"", "\"]\"", "\"|\"", "\"&\"",
  "\"!\"", "STATEHDR", "INF", "FIN", "BEGINBODY", "ENDBODY", "STRING",
  "IDENTIFIER", "ANAME", "HEADERNAME", "INT", "BOOL", "$accept",
  "automaton", "header", "format_version", "header_list", "header_item",
  "state_conj", "label_expr", "lab_exp_conj", "lab_exp_atom",
  "acceptance_cond", "acc_cond_conj", "acc_cond_atom", "accid",
  "boolintid_list", "boolintstrid_list", "string_list", "id_list", "body",
  "statespec_list", "state_name", "maybe_label", "maybe_string",
  "maybe_accsig", "int_list", "trans_list", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,     1,     2,     3,     4,     5,     6,     7,
       8,     9,    10,    11,   258,   259,   260,   261,   262,   263,
     264,   265,   266,   267,   268,   269,   270,   271,   272,   273,
     274,   275,   276,   277
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    34,    35,    36,    37,    38,    38,    39,    39,    39,
      39,    39,    39,    39,    39,    39,    39,    39,    40,    40,
      41,    41,    42,    42,    43,    43,    43,    43,    43,    44,
      44,    45,    45,    46,    46,    46,    46,    47,    47,    48,
      48,    48,    48,    49,    49,    49,    49,    49,    50,    50,
      51,    51,    52,    53,    53,    54,    55,    55,    56,    56,
      57,    57,    58,    58,    59,    59
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     4,     2,     2,     0,     2,     2,     2,     3,
       2,     3,     3,     3,     3,     2,     2,     2,     1,     3,
       1,     3,     1,     3,     1,     1,     1,     2,     3,     1,
       3,     1,     3,     4,     5,     3,     1,     1,     1,     0,
       2,     2,     2,     0,     2,     2,     2,     2,     0,     2,
       0,     2,     1,     0,     3,     5,     0,     3,     0,     1,
       0,     3,     0,     2,     0,     4
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     0,     0,     0,     5,     4,     1,    53,     3,    56,
       0,    52,    64,     0,     0,     0,    62,     0,     0,     0,
       0,     0,    50,    43,     6,     0,     0,     2,     0,    53,
       0,     7,    48,    62,    10,    39,    58,    15,    18,     8,
       0,    50,    16,    17,     0,     0,    26,    25,    24,     0,
      20,    22,    58,    60,    54,     0,    38,    37,    36,    12,
      29,    31,     0,    48,     9,    63,    39,    39,    39,    13,
      59,    14,     0,    11,    51,    46,    47,    45,    44,     0,
      27,    57,     0,     0,    60,    62,    64,     0,     0,     0,
       0,    49,    42,    41,    40,    19,    28,    21,    23,    55,
       0,    65,    35,    30,    32,     0,     0,    61,     0,    33,
      34
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
      -1,     2,     3,     4,     8,    24,    39,    49,    50,    51,
      59,    60,    61,    62,    69,    43,    64,    42,    10,    11,
      12,    28,    71,    86,    34,    29
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -44
static const yytype_int8 yypact[] =
{
      10,    -5,    36,    13,   -44,   -44,   -44,    21,    -1,    28,
      24,   -44,    -4,    23,    25,    26,    27,    31,    33,    35,
      30,    37,    39,   -44,   -44,     1,    32,   -44,    30,    21,
       2,   -44,    38,    27,   -44,     9,    41,   -44,    44,   -44,
       1,    39,   -44,   -11,     1,     1,   -44,   -44,   -44,    34,
      49,   -44,    41,    40,   -44,     2,   -44,   -44,   -44,    51,
      52,   -44,    58,    38,   -44,   -44,     9,     9,     9,   -44,
     -44,   -44,    30,    54,   -44,   -44,   -44,   -44,   -44,     5,
     -44,   -44,     1,     1,    40,    27,    -4,    17,     2,     2,
      -3,   -44,   -44,   -44,   -44,   -44,   -44,    49,   -44,   -44,
      59,   -44,   -44,    52,   -44,    43,    62,   -44,    63,   -44,
     -44
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -44,   -44,   -44,   -44,   -44,   -44,   -27,     3,    -2,   -43,
      29,    -9,    -8,   -44,   -18,   -44,    19,    42,   -44,    56,
     -44,    77,    45,     4,   -33,     6
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -57
static const yytype_int8 yytable[] =
{
      65,    53,    80,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,     1,    25,    44,    55,    75,    76,   105,
      96,    77,    78,    45,     5,    82,    56,    57,   -56,   106,
      23,    46,   102,    47,    48,    58,     6,    88,    66,     7,
      98,    67,    68,    73,     9,    95,    25,    79,    92,    93,
      94,    27,   100,    81,    82,    30,    85,    31,    32,    33,
      35,    36,    38,    37,    52,    72,    63,    40,    41,    70,
      83,    88,    90,    89,    82,   108,   107,   109,   110,   103,
      97,   104,    91,    74,    87,    54,    26,     0,    99,     0,
       0,     0,   101,     0,     0,     0,     0,    84
};

static const yytype_int8 yycheck[] =
{
      33,    28,    45,     4,     5,     6,     7,     8,     9,    10,
      11,    12,    13,     3,    18,    14,    14,    28,    29,    22,
      15,    32,    33,    22,    29,    20,    24,    25,    32,    32,
      31,    30,    15,    32,    33,    33,     0,    20,    29,    26,
      83,    32,    33,    40,    23,    72,    18,    44,    66,    67,
      68,    27,    85,    19,    20,    32,    16,    32,    32,    32,
      29,    28,    32,    28,    32,    21,    28,    30,    29,    28,
      21,    20,    14,    21,    20,    32,    17,    15,    15,    88,
      82,    89,    63,    41,    55,    29,     9,    -1,    84,    -1,
      -1,    -1,    86,    -1,    -1,    -1,    -1,    52
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,    35,    36,    37,    29,     0,    26,    38,    23,
      52,    53,    54,     4,     5,     6,     7,     8,     9,    10,
      11,    12,    13,    31,    39,    18,    55,    27,    55,    59,
      32,    32,    32,    32,    58,    29,    28,    28,    32,    40,
      30,    29,    51,    49,    14,    22,    30,    32,    33,    41,
      42,    43,    32,    40,    53,    14,    24,    25,    33,    44,
      45,    46,    47,    28,    50,    58,    29,    32,    33,    48,
      28,    56,    21,    41,    51,    28,    29,    32,    33,    41,
      43,    19,    20,    21,    56,    16,    57,    44,    20,    21,
      14,    50,    48,    48,    48,    40,    15,    42,    43,    57,
      58,    59,    15,    45,    46,    22,    32,    17,    32,    15,
      15
};

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrorlab


/* Like YYERROR except do call yyerror.  This remains here temporarily
   to ease the transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */

#define YYFAIL		goto yyerrlab

#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      yytoken = YYTRANSLATE (yychar);				\
      YYPOPSTACK (1);						\
      goto yybackup;						\
    }								\
  else								\
    {								\
      yyerror (YY_("syntax error: cannot back up")); \
      YYERROR;							\
    }								\
while (YYID (0))


#define YYTERROR	1
#define YYERRCODE	256


/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#define YYRHSLOC(Rhs, K) ((Rhs)[K])
#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)				\
    do									\
      if (YYID (N))                                                    \
	{								\
	  (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;	\
	  (Current).first_column = YYRHSLOC (Rhs, 1).first_column;	\
	  (Current).last_line    = YYRHSLOC (Rhs, N).last_line;		\
	  (Current).last_column  = YYRHSLOC (Rhs, N).last_column;	\
	}								\
      else								\
	{								\
	  (Current).first_line   = (Current).last_line   =		\
	    YYRHSLOC (Rhs, 0).last_line;				\
	  (Current).first_column = (Current).last_column =		\
	    YYRHSLOC (Rhs, 0).last_column;				\
	}								\
    while (YYID (0))
#endif


/* YY_LOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

#ifndef YY_LOCATION_PRINT
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
#  define YY_LOCATION_PRINT(File, Loc)			\
     fprintf (File, "%d.%d-%d.%d",			\
	      (Loc).first_line, (Loc).first_column,	\
	      (Loc).last_line,  (Loc).last_column)
# else
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif
#endif


/* YYLEX -- calling `yylex' with the right arguments.  */

#ifdef YYLEX_PARAM
# define YYLEX yylex (YYLEX_PARAM)
#else
# define YYLEX yylex ()
#endif

/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)			\
do {						\
  if (yydebug)					\
    YYFPRINTF Args;				\
} while (YYID (0))

# define YY_SYMBOL_PRINT(Title, Type, Value, Location)			  \
do {									  \
  if (yydebug)								  \
    {									  \
      YYFPRINTF (stderr, "%s ", Title);					  \
      yy_symbol_print (stderr,						  \
		  Type, Value, Location); \
      YYFPRINTF (stderr, "\n");						  \
    }									  \
} while (YYID (0))


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
#else
static void
yy_symbol_value_print (yyoutput, yytype, yyvaluep, yylocationp)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
    YYLTYPE const * const yylocationp;
#endif
{
  if (!yyvaluep)
    return;
  YYUSE (yylocationp);
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# else
  YYUSE (yyoutput);
# endif
  switch (yytype)
    {
      default:
	break;
    }
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
#else
static void
yy_symbol_print (yyoutput, yytype, yyvaluep, yylocationp)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
    YYLTYPE const * const yylocationp;
#endif
{
  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  YY_LOCATION_PRINT (yyoutput, *yylocationp);
  YYFPRINTF (yyoutput, ": ");
  yy_symbol_value_print (yyoutput, yytype, yyvaluep, yylocationp);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_stack_print (yytype_int16 *bottom, yytype_int16 *top)
#else
static void
yy_stack_print (bottom, top)
    yytype_int16 *bottom;
    yytype_int16 *top;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (; bottom <= top; ++bottom)
    YYFPRINTF (stderr, " %d", *bottom);
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (YYID (0))


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_reduce_print (YYSTYPE *yyvsp, YYLTYPE *yylsp, int yyrule)
#else
static void
yy_reduce_print (yyvsp, yylsp, yyrule)
    YYSTYPE *yyvsp;
    YYLTYPE *yylsp;
    int yyrule;
#endif
{
  int yynrhs = yyr2[yyrule];
  int yyi;
  unsigned long int yylno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
	     yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      fprintf (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr, yyrhs[yyprhs[yyrule] + yyi],
		       &(yyvsp[(yyi + 1) - (yynrhs)])
		       , &(yylsp[(yyi + 1) - (yynrhs)])		       );
      fprintf (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (yyvsp, yylsp, Rule); \
} while (YYID (0))

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static YYSIZE_T
yystrlen (const char *yystr)
#else
static YYSIZE_T
yystrlen (yystr)
    const char *yystr;
#endif
{
  YYSIZE_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static char *
yystpcpy (char *yydest, const char *yysrc)
#else
static char *
yystpcpy (yydest, yysrc)
    char *yydest;
    const char *yysrc;
#endif
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYSIZE_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYSIZE_T yyn = 0;
      char const *yyp = yystr;

      for (;;)
	switch (*++yyp)
	  {
	  case '\'':
	  case ',':
	    goto do_not_strip_quotes;

	  case '\\':
	    if (*++yyp != '\\')
	      goto do_not_strip_quotes;
	    /* Fall through.  */
	  default:
	    if (yyres)
	      yyres[yyn] = *yyp;
	    yyn++;
	    break;

	  case '"':
	    if (yyres)
	      yyres[yyn] = '\0';
	    return yyn;
	  }
    do_not_strip_quotes: ;
    }

  if (! yyres)
    return yystrlen (yystr);

  return yystpcpy (yyres, yystr) - yyres;
}
# endif

/* Copy into YYRESULT an error message about the unexpected token
   YYCHAR while in state YYSTATE.  Return the number of bytes copied,
   including the terminating null byte.  If YYRESULT is null, do not
   copy anything; just return the number of bytes that would be
   copied.  As a special case, return 0 if an ordinary "syntax error"
   message will do.  Return YYSIZE_MAXIMUM if overflow occurs during
   size calculation.  */
static YYSIZE_T
yysyntax_error (char *yyresult, int yystate, int yychar)
{
  int yyn = yypact[yystate];

  if (! (YYPACT_NINF < yyn && yyn <= YYLAST))
    return 0;
  else
    {
      int yytype = YYTRANSLATE (yychar);
      YYSIZE_T yysize0 = yytnamerr (0, yytname[yytype]);
      YYSIZE_T yysize = yysize0;
      YYSIZE_T yysize1;
      int yysize_overflow = 0;
      enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
      char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
      int yyx;

# if 0
      /* This is so xgettext sees the translatable formats that are
	 constructed on the fly.  */
      YY_("syntax error, unexpected %s");
      YY_("syntax error, unexpected %s, expecting %s");
      YY_("syntax error, unexpected %s, expecting %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s");
# endif
      char *yyfmt;
      char const *yyf;
      static char const yyunexpected[] = "syntax error, unexpected %s";
      static char const yyexpecting[] = ", expecting %s";
      static char const yyor[] = " or %s";
      char yyformat[sizeof yyunexpected
		    + sizeof yyexpecting - 1
		    + ((YYERROR_VERBOSE_ARGS_MAXIMUM - 2)
		       * (sizeof yyor - 1))];
      char const *yyprefix = yyexpecting;

      /* Start YYX at -YYN if negative to avoid negative indexes in
	 YYCHECK.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;

      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yycount = 1;

      yyarg[0] = yytname[yytype];
      yyfmt = yystpcpy (yyformat, yyunexpected);

      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
	if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	  {
	    if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
	      {
		yycount = 1;
		yysize = yysize0;
		yyformat[sizeof yyunexpected - 1] = '\0';
		break;
	      }
	    yyarg[yycount++] = yytname[yyx];
	    yysize1 = yysize + yytnamerr (0, yytname[yyx]);
	    yysize_overflow |= (yysize1 < yysize);
	    yysize = yysize1;
	    yyfmt = yystpcpy (yyfmt, yyprefix);
	    yyprefix = yyor;
	  }

      yyf = YY_(yyformat);
      yysize1 = yysize + yystrlen (yyf);
      yysize_overflow |= (yysize1 < yysize);
      yysize = yysize1;

      if (yysize_overflow)
	return YYSIZE_MAXIMUM;

      if (yyresult)
	{
	  /* Avoid sprintf, as that infringes on the user's name space.
	     Don't have undefined behavior even if the translation
	     produced a string with the wrong number of "%s"s.  */
	  char *yyp = yyresult;
	  int yyi = 0;
	  while ((*yyp = *yyf) != '\0')
	    {
	      if (*yyp == '%' && yyf[1] == 's' && yyi < yycount)
		{
		  yyp += yytnamerr (yyp, yyarg[yyi++]);
		  yyf += 2;
		}
	      else
		{
		  yyp++;
		  yyf++;
		}
	    }
	}
      return yysize;
    }
}
#endif /* YYERROR_VERBOSE */


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
#else
static void
yydestruct (yymsg, yytype, yyvaluep, yylocationp)
    const char *yymsg;
    int yytype;
    YYSTYPE *yyvaluep;
    YYLTYPE *yylocationp;
#endif
{
  YYUSE (yyvaluep);
  YYUSE (yylocationp);

  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  switch (yytype)
    {

      default:
	break;
    }
}


/* Prevent warnings from -Wmissing-prototypes.  */

#ifdef YYPARSE_PARAM
#if defined __STDC__ || defined __cplusplus
int yyparse (void *YYPARSE_PARAM);
#else
int yyparse ();
#endif
#else /* ! YYPARSE_PARAM */
#if defined __STDC__ || defined __cplusplus
int yyparse (void);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */



/* The look-ahead symbol.  */
int yychar;

/* The semantic value of the look-ahead symbol.  */
YYSTYPE yylval;

/* Number of syntax errors so far.  */
int yynerrs;
/* Location data for the look-ahead symbol.  */
YYLTYPE yylloc;



/*----------.
| yyparse.  |
`----------*/

#ifdef YYPARSE_PARAM
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void *YYPARSE_PARAM)
#else
int
yyparse (YYPARSE_PARAM)
    void *YYPARSE_PARAM;
#endif
#else /* ! YYPARSE_PARAM */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void)
#else
int
yyparse ()

#endif
#endif
{
  
  int yystate;
  int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Look-ahead token as an internal (translated) token number.  */
  int yytoken = 0;
#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  yytype_int16 yyssa[YYINITDEPTH];
  yytype_int16 *yyss = yyssa;
  yytype_int16 *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  YYSTYPE *yyvsp;

  /* The location stack.  */
  YYLTYPE yylsa[YYINITDEPTH];
  YYLTYPE *yyls = yylsa;
  YYLTYPE *yylsp;
  /* The locations where the error started and ended.  */
  YYLTYPE yyerror_range[2];

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N), yylsp -= (N))

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss;
  yyvsp = yyvs;
  yylsp = yyls;
#if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
  /* Initialize the default location before parsing starts.  */
  yylloc.first_line   = yylloc.last_line   = 1;
  yylloc.first_column = yylloc.last_column = 0;
#endif

  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack.  Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	yytype_int16 *yyss1 = yyss;
	YYLTYPE *yyls1 = yyls;

	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow (YY_("memory exhausted"),
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),
		    &yyls1, yysize * sizeof (*yylsp),
		    &yystacksize);
	yyls = yyls1;
	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyexhaustedlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	yytype_int16 *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyexhaustedlab;
	YYSTACK_RELOCATE (yyss);
	YYSTACK_RELOCATE (yyvs);
	YYSTACK_RELOCATE (yyls);
#  undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
      yylsp = yyls + yysize - 1;

      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
		  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
	YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

  /* Do appropriate processing given the current state.  Read a
     look-ahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to look-ahead token.  */
  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a look-ahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid look-ahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = YYLEX;
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yyn == 0 || yyn == YYTABLE_NINF)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the look-ahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  yystate = yyn;
  *++yyvsp = yylval;
  *++yylsp = yylloc;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     `$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];

  /* Default location.  */
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 2:
#line 450 "hoa.y"
    {
            if (!seenHeader[HOAHDR]) /* redundant because of the grammar */
                yyerror("No HOA: header item");
            if (!seenHeader[ACCEPTANCE])
                yyerror("No Acceptance: header item");
         ;}
    break;

  case 4:
#line 460 "hoa.y"
    {
                  loadedData->version = (yyvsp[(2) - (2)].string);
                  if (seenHeader[HOAHDR])
                      hdrItemError("HOA:");
                  else
                      seenHeader[HOAHDR] = true;
              ;}
    break;

  case 5:
#line 468 "hoa.y"
    { /* no new item, nothing to check */ ;}
    break;

  case 6:
#line 470 "hoa.y"
    {
               if ((yyvsp[(2) - (2)].number) <= 8) {
                   if (seenHeader[(yyvsp[(2) - (2)].number)])
                       hdrItemError(headerStrs[(yyvsp[(2) - (2)].number)]);
                   else
                       seenHeader[(yyvsp[(2) - (2)].number)] = true;
               }
           ;}
    break;

  case 7:
#line 479 "hoa.y"
    {
                                                 loadedData->noStates = (yyvsp[(2) - (2)].number);
                                                 (yyval.number) = STATES;
                                               ;}
    break;

  case 8:
#line 483 "hoa.y"
    {
                                                 tempStart =
                                                    concatIntLists(
                                                        tempStart,
                                                        (yyvsp[(2) - (2)].numlist)
                                                    );
                                                 (yyval.number) = START;
                                               ;}
    break;

  case 9:
#line 491 "hoa.y"
    {
                                                 loadedData->noAPs = (yyvsp[(2) - (3)].number);
                                                 tempAps = (yyvsp[(3) - (3)].strlist);
                                                 (yyval.number) = AP;
                                               ;}
    break;

  case 10:
#line 496 "hoa.y"
    {
                                                 tempCntAPs = (yyvsp[(2) - (2)].numlist);
                                                 (yyval.number) = CNTAP;
                                               ;}
    break;

  case 11:
#line 500 "hoa.y"
    {
                                                 tempAliases =
                                                    prependAliasNode(
                                                        tempAliases,
                                                        (yyvsp[(2) - (3)].string), (yyvsp[(3) - (3)].tree)
                                                    );
                                                 (yyval.number) = ALIAS;
                                               ;}
    break;

  case 12:
#line 508 "hoa.y"
    { 
                                                 loadedData->noAccSets = (yyvsp[(2) - (3)].number);
                                                 loadedData->acc = (yyvsp[(3) - (3)].tree);
                                                 (yyval.number) = ACCEPTANCE;
                                               ;}
    break;

  case 13:
#line 513 "hoa.y"
    { 
                                                 loadedData->accNameID = (yyvsp[(2) - (3)].string);
                                                 tempAccNameParameters
                                                    = concatStrLists(
                                                        tempAccNameParameters,
                                                        (yyvsp[(3) - (3)].strlist)
                                                    );
                                                 (yyval.number) = ACCNAME;
                                               ;}
    break;

  case 14:
#line 522 "hoa.y"
    { (yyval.number) = TOOL; ;}
    break;

  case 15:
#line 523 "hoa.y"
    {
                                                 loadedData->name = (yyvsp[(2) - (2)].string);
                                                 (yyval.number) = NAME;
                                               ;}
    break;

  case 16:
#line 527 "hoa.y"
    { 
                                                 tempProperties =
                                                     concatStrLists(
                                                         tempProperties,
                                                         (yyvsp[(2) - (2)].strlist)
                                                     );
                                                 (yyval.number) = PROPERTIES;
                                               ;}
    break;

  case 17:
#line 535 "hoa.y"
    { free((yyvsp[(1) - (2)].string)); (yyval.number) = HEADERNAME; ;}
    break;

  case 18:
#line 538 "hoa.y"
    { (yyval.numlist) = newIntNode((yyvsp[(1) - (1)].number)); ;}
    break;

  case 19:
#line 539 "hoa.y"
    { (yyval.numlist) = prependIntNode((yyvsp[(3) - (3)].numlist), (yyvsp[(1) - (3)].number)); ;}
    break;

  case 20:
#line 542 "hoa.y"
    { (yyval.tree) = (yyvsp[(1) - (1)].tree); ;}
    break;

  case 21:
#line 543 "hoa.y"
    { (yyval.tree) = orBTree((yyvsp[(1) - (3)].tree), (yyvsp[(3) - (3)].tree)); ;}
    break;

  case 22:
#line 546 "hoa.y"
    { (yyval.tree) = (yyvsp[(1) - (1)].tree); ;}
    break;

  case 23:
#line 547 "hoa.y"
    { (yyval.tree) = andBTree((yyvsp[(1) - (3)].tree), (yyvsp[(3) - (3)].tree)); ;}
    break;

  case 24:
#line 550 "hoa.y"
    { (yyval.tree) = boolBTree((yyvsp[(1) - (1)].boolean)); ;}
    break;

  case 25:
#line 551 "hoa.y"
    { (yyval.tree) = apBTree((yyvsp[(1) - (1)].number)); ;}
    break;

  case 26:
#line 552 "hoa.y"
    { (yyval.tree) = aliasBTree((yyvsp[(1) - (1)].string)); ;}
    break;

  case 27:
#line 553 "hoa.y"
    { (yyval.tree) = notBTree((yyvsp[(2) - (2)].tree)); ;}
    break;

  case 28:
#line 554 "hoa.y"
    { (yyval.tree) = (yyvsp[(2) - (3)].tree); ;}
    break;

  case 29:
#line 557 "hoa.y"
    { (yyval.tree) = (yyvsp[(1) - (1)].tree); ;}
    break;

  case 30:
#line 558 "hoa.y"
    { (yyval.tree) = orBTree((yyvsp[(1) - (3)].tree), (yyvsp[(3) - (3)].tree)); ;}
    break;

  case 31:
#line 561 "hoa.y"
    { (yyval.tree) = (yyvsp[(1) - (1)].tree); ;}
    break;

  case 32:
#line 562 "hoa.y"
    { (yyval.tree) = andBTree((yyvsp[(1) - (3)].tree), (yyvsp[(3) - (3)].tree)); ;}
    break;

  case 33:
#line 565 "hoa.y"
    { (yyval.tree) = accidBTree((yyvsp[(1) - (4)].nodetype), (yyvsp[(3) - (4)].number), false); ;}
    break;

  case 34:
#line 566 "hoa.y"
    { (yyval.tree) = accidBTree((yyvsp[(1) - (5)].nodetype), (yyvsp[(4) - (5)].number), true); ;}
    break;

  case 35:
#line 567 "hoa.y"
    { (yyval.tree) = (yyvsp[(2) - (3)].tree); ;}
    break;

  case 36:
#line 568 "hoa.y"
    { (yyval.tree) = boolBTree((yyvsp[(1) - (1)].boolean)); ;}
    break;

  case 37:
#line 571 "hoa.y"
    { (yyval.nodetype) = NT_FIN; ;}
    break;

  case 38:
#line 572 "hoa.y"
    { (yyval.nodetype) = NT_INF; ;}
    break;

  case 39:
#line 575 "hoa.y"
    { (yyval.strlist) = NULL; ;}
    break;

  case 40:
#line 576 "hoa.y"
    { 
                                            (yyval.strlist) = (yyvsp[(1) - (2)].boolean) ? prependStrNode((yyvsp[(2) - (2)].strlist),
                                                                     strdup("True"))
                                                : prependStrNode((yyvsp[(2) - (2)].strlist),
                                                                 strdup("False"));
                                          ;}
    break;

  case 41:
#line 582 "hoa.y"
    {
                                            char buffer[66];
                                            sprintf(buffer, "%d", (yyvsp[(1) - (2)].number));
                                            (yyval.strlist) = prependStrNode((yyvsp[(2) - (2)].strlist),
                                                                strdup(buffer));
                                          ;}
    break;

  case 42:
#line 588 "hoa.y"
    { (yyval.strlist) = prependStrNode((yyvsp[(2) - (2)].strlist), (yyvsp[(1) - (2)].string)); ;}
    break;

  case 47:
#line 595 "hoa.y"
    { free((yyvsp[(2) - (2)].string)); ;}
    break;

  case 48:
#line 597 "hoa.y"
    { (yyval.strlist) = NULL; ;}
    break;

  case 49:
#line 598 "hoa.y"
    { (yyval.strlist) = prependStrNode((yyvsp[(2) - (2)].strlist), (yyvsp[(1) - (2)].string)); ;}
    break;

  case 50:
#line 601 "hoa.y"
    { (yyval.strlist) = NULL; ;}
    break;

  case 51:
#line 602 "hoa.y"
    { (yyval.strlist) = prependStrNode((yyvsp[(2) - (2)].strlist), (yyvsp[(1) - (2)].string)); ;}
    break;

  case 52:
#line 606 "hoa.y"
    { tempStates = (yyvsp[(1) - (1)].statelist); ;}
    break;

  case 53:
#line 609 "hoa.y"
    { (yyval.statelist) = NULL; ;}
    break;

  case 54:
#line 611 "hoa.y"
    {
                (yyval.statelist) = prependStateNode((yyvsp[(3) - (3)].statelist), (yyvsp[(1) - (3)].statelist), (yyvsp[(2) - (3)].trlist));
              ;}
    break;

  case 55:
#line 617 "hoa.y"
    { (yyval.statelist) = newStateNode((yyvsp[(3) - (5)].number), (yyvsp[(4) - (5)].string), (yyvsp[(2) - (5)].tree), (yyvsp[(5) - (5)].numlist)); ;}
    break;

  case 56:
#line 620 "hoa.y"
    { (yyval.tree) = NULL; ;}
    break;

  case 57:
#line 621 "hoa.y"
    { (yyval.tree) = (yyvsp[(2) - (3)].tree); ;}
    break;

  case 58:
#line 624 "hoa.y"
    { (yyval.string) = NULL; ;}
    break;

  case 59:
#line 625 "hoa.y"
    { (yyval.string) = (yyvsp[(1) - (1)].string); ;}
    break;

  case 60:
#line 628 "hoa.y"
    { (yyval.numlist) = NULL; ;}
    break;

  case 61:
#line 629 "hoa.y"
    { (yyval.numlist) = (yyvsp[(2) - (3)].numlist); ;}
    break;

  case 62:
#line 632 "hoa.y"
    { (yyval.numlist) = NULL; ;}
    break;

  case 63:
#line 633 "hoa.y"
    { (yyval.numlist) = prependIntNode((yyvsp[(2) - (2)].numlist), (yyvsp[(1) - (2)].number)); ;}
    break;

  case 64:
#line 636 "hoa.y"
    { (yyval.trlist) = NULL; ;}
    break;

  case 65:
#line 638 "hoa.y"
    { (yyval.trlist) = prependTransNode((yyvsp[(4) - (4)].trlist), (yyvsp[(1) - (4)].tree), (yyvsp[(2) - (4)].numlist), (yyvsp[(3) - (4)].numlist)); ;}
    break;


/* Line 1267 of yacc.c.  */
#line 2279 "hoaparser.c"
      default: break;
    }
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;
  *++yylsp = yyloc;

  /* Now `shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*------------------------------------.
| yyerrlab -- here on detecting error |
`------------------------------------*/
yyerrlab:
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
      {
	YYSIZE_T yysize = yysyntax_error (0, yystate, yychar);
	if (yymsg_alloc < yysize && yymsg_alloc < YYSTACK_ALLOC_MAXIMUM)
	  {
	    YYSIZE_T yyalloc = 2 * yysize;
	    if (! (yysize <= yyalloc && yyalloc <= YYSTACK_ALLOC_MAXIMUM))
	      yyalloc = YYSTACK_ALLOC_MAXIMUM;
	    if (yymsg != yymsgbuf)
	      YYSTACK_FREE (yymsg);
	    yymsg = (char *) YYSTACK_ALLOC (yyalloc);
	    if (yymsg)
	      yymsg_alloc = yyalloc;
	    else
	      {
		yymsg = yymsgbuf;
		yymsg_alloc = sizeof yymsgbuf;
	      }
	  }

	if (0 < yysize && yysize <= yymsg_alloc)
	  {
	    (void) yysyntax_error (yymsg, yystate, yychar);
	    yyerror (yymsg);
	  }
	else
	  {
	    yyerror (YY_("syntax error"));
	    if (yysize != 0)
	      goto yyexhaustedlab;
	  }
      }
#endif
    }

  yyerror_range[0] = yylloc;

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse look-ahead token after an
	 error, discard it.  */

      if (yychar <= YYEOF)
	{
	  /* Return failure if at end of input.  */
	  if (yychar == YYEOF)
	    YYABORT;
	}
      else
	{
	  yydestruct ("Error: discarding",
		      yytoken, &yylval, &yylloc);
	  yychar = YYEMPTY;
	}
    }

  /* Else will try to reuse look-ahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:

  /* Pacify compilers like GCC when the user code never invokes
     YYERROR and the label yyerrorlab therefore never appears in user
     code.  */
  if (/*CONSTCOND*/ 0)
     goto yyerrorlab;

  yyerror_range[0] = yylsp[1-yylen];
  /* Do not reclaim the symbols of the rule which action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;	/* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (yyn != YYPACT_NINF)
	{
	  yyn += YYTERROR;
	  if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
	    {
	      yyn = yytable[yyn];
	      if (0 < yyn)
		break;
	    }
	}

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
	YYABORT;

      yyerror_range[0] = *yylsp;
      yydestruct ("Error: popping",
		  yystos[yystate], yyvsp, yylsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  *++yyvsp = yylval;

  yyerror_range[1] = yylloc;
  /* Using YYLLOC is tempting, but would change the location of
     the look-ahead.  YYLOC is available though.  */
  YYLLOC_DEFAULT (yyloc, (yyerror_range - 1), 2);
  *++yylsp = yyloc;

  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

#ifndef yyoverflow
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEOF && yychar != YYEMPTY)
     yydestruct ("Cleanup: discarding lookahead",
		 yytoken, &yylval, &yylloc);
  /* Do not reclaim the symbols of the rule which action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
		  yystos[*yyssp], yyvsp, yylsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  /* Make sure YYID is used.  */
  return YYID (yyresult);
}


#line 641 "hoa.y"

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

