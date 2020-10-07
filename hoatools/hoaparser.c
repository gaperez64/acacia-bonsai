/* A Bison parser, made by GNU Bison 3.5.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2019 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

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

/* Undocumented macros, especially those whose name start with YY_,
   are private implementation details.  Do not rely on them.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "3.5"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
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


#line 465 "hoaparser.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

/* Use api.header.include to #include this header
   instead of duplicating it here.  */
#ifndef YY_YY_HOAPARSER_H_INCLUDED
# define YY_YY_HOAPARSER_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
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

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 420 "hoa.y"

    int number;
    char* string;
    bool boolean;
    NodeType nodetype;
    void* numlist;
    void* strlist;
    void* trlist;
    void* statelist;
    BTree* tree;

#line 563 "hoaparser.c"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif

/* Location type.  */
#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE YYLTYPE;
struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
};
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif


extern YYSTYPE yylval;
extern YYLTYPE yylloc;
int yyparse (void);

#endif /* !YY_YY_HOAPARSER_H_INCLUDED  */



#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))

/* Stored state numbers (used for stacks). */
typedef yytype_int8 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif

#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(E) ((void) (E))
#else
# define YYUSE(E) /* empty */
#endif

#if defined __GNUC__ && ! defined __ICC && 407 <= __GNUC__ * 100 + __GNUC_MINOR__
/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                            \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

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
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
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
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
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
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
  YYLTYPE yyls_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE) \
             + YYSIZEOF (YYLTYPE)) \
      + 2 * YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

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
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  111

#define YYUNDEFTOK  2
#define YYMAXUTOK   277


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
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
  /* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
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

#if YYDEBUG || YYERROR_VERBOSE || 0
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
  "maybe_accsig", "int_list", "trans_list", YY_NULLPTR
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[NUM] -- (External) token number corresponding to the
   (internal) symbol number NUM (which must be that of a token).  */
static const yytype_int16 yytoknum[] =
{
       0,   256,   257,     1,     2,     3,     4,     5,     6,     7,
       8,     9,    10,    11,   258,   259,   260,   261,   262,   263,
     264,   265,   266,   267,   268,   269,   270,   271,   272,   273,
     274,   275,   276,   277
};
# endif

#define YYPACT_NINF (-44)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-57)

#define yytable_value_is_error(Yyn) \
  0

  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
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

  /* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
     Performed when YYTABLE does not specify something else to do.  Zero
     means the default is an error.  */
static const yytype_int8 yydefact[] =
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

  /* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -44,   -44,   -44,   -44,   -44,   -44,   -27,     3,    -2,   -43,
      29,    -9,    -8,   -44,   -18,   -44,    19,    42,   -44,    56,
     -44,    77,    45,     4,   -33,     6
};

  /* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
      -1,     2,     3,     4,     8,    24,    39,    49,    50,    51,
      59,    60,    61,    62,    69,    43,    64,    42,    10,    11,
      12,    28,    71,    86,    34,    29
};

  /* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule whose
     number is the opposite.  If YYTABLE_NINF, syntax error.  */
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
static const yytype_int8 yystos[] =
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

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_int8 yyr1[] =
{
       0,    34,    35,    36,    37,    38,    38,    39,    39,    39,
      39,    39,    39,    39,    39,    39,    39,    39,    40,    40,
      41,    41,    42,    42,    43,    43,    43,    43,    43,    44,
      44,    45,    45,    46,    46,    46,    46,    47,    47,    48,
      48,    48,    48,    49,    49,    49,    49,    49,    50,    50,
      51,    51,    52,    53,    53,    54,    55,    55,    56,    56,
      57,    57,    58,    58,    59,    59
};

  /* YYR2[YYN] -- Number of symbols on the right hand side of rule YYN.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     4,     2,     2,     0,     2,     2,     2,     3,
       2,     3,     3,     3,     3,     2,     2,     2,     1,     3,
       1,     3,     1,     3,     1,     1,     1,     2,     3,     1,
       3,     1,     3,     4,     5,     3,     1,     1,     1,     0,
       2,     2,     2,     0,     2,     2,     2,     2,     0,     2,
       0,     2,     1,     0,     3,     5,     0,     3,     0,     1,
       0,     3,     0,     2,     0,     4
};


#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)
#define YYEMPTY         (-2)
#define YYEOF           0

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Error token number */
#define YYTERROR        1
#define YYERRCODE       256


/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)                                \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;        \
          (Current).first_column = YYRHSLOC (Rhs, 1).first_column;      \
          (Current).last_line    = YYRHSLOC (Rhs, N).last_line;         \
          (Current).last_column  = YYRHSLOC (Rhs, N).last_column;       \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).first_line   = (Current).last_line   =              \
            YYRHSLOC (Rhs, 0).last_line;                                \
          (Current).first_column = (Current).last_column =              \
            YYRHSLOC (Rhs, 0).last_column;                              \
        }                                                               \
    while (0)
#endif

#define YYRHSLOC(Rhs, K) ((Rhs)[K])


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)


/* YY_LOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

#ifndef YY_LOCATION_PRINT
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL

/* Print *YYLOCP on YYO.  Private, do not rely on its existence. */

YY_ATTRIBUTE_UNUSED
static int
yy_location_print_ (FILE *yyo, YYLTYPE const * const yylocp)
{
  int res = 0;
  int end_col = 0 != yylocp->last_column ? yylocp->last_column - 1 : 0;
  if (0 <= yylocp->first_line)
    {
      res += YYFPRINTF (yyo, "%d", yylocp->first_line);
      if (0 <= yylocp->first_column)
        res += YYFPRINTF (yyo, ".%d", yylocp->first_column);
    }
  if (0 <= yylocp->last_line)
    {
      if (yylocp->first_line < yylocp->last_line)
        {
          res += YYFPRINTF (yyo, "-%d", yylocp->last_line);
          if (0 <= end_col)
            res += YYFPRINTF (yyo, ".%d", end_col);
        }
      else if (0 <= end_col && yylocp->first_column < end_col)
        res += YYFPRINTF (yyo, "-%d", end_col);
    }
  return res;
 }

#  define YY_LOCATION_PRINT(File, Loc)          \
  yy_location_print_ (File, &(Loc))

# else
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif
#endif


# define YY_SYMBOL_PRINT(Title, Type, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Type, Value, Location); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo, int yytype, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
{
  FILE *yyoutput = yyo;
  YYUSE (yyoutput);
  YYUSE (yylocationp);
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyo, yytoknum[yytype], *yyvaluep);
# endif
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YYUSE (yytype);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo, int yytype, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
{
  YYFPRINTF (yyo, "%s %s (",
             yytype < YYNTOKENS ? "token" : "nterm", yytname[yytype]);

  YY_LOCATION_PRINT (yyo, *yylocationp);
  YYFPRINTF (yyo, ": ");
  yy_symbol_value_print (yyo, yytype, yyvaluep, yylocationp);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp, YYLTYPE *yylsp, int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       yystos[yyssp[yyi + 1 - yynrhs]],
                       &yyvsp[(yyi + 1) - (yynrhs)]
                       , &(yylsp[(yyi + 1) - (yynrhs)])                       );
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, yylsp, Rule); \
} while (0)

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
#ifndef YYINITDEPTH
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
#   define yystrlen(S) (YY_CAST (YYPTRDIFF_T, strlen (S)))
#  else
/* Return the length of YYSTR.  */
static YYPTRDIFF_T
yystrlen (const char *yystr)
{
  YYPTRDIFF_T yylen;
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
static char *
yystpcpy (char *yydest, const char *yysrc)
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
static YYPTRDIFF_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYPTRDIFF_T yyn = 0;
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
            else
              goto append;

          append:
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

  if (yyres)
    return yystpcpy (yyres, yystr) - yyres;
  else
    return yystrlen (yystr);
}
# endif

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return 1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return 2 if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYPTRDIFF_T *yymsg_alloc, char **yymsg,
                yy_state_t *yyssp, int yytoken)
{
  enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat: reported tokens (one for the "unexpected",
     one per "expected"). */
  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
  /* Actual size of YYARG. */
  int yycount = 0;
  /* Cumulated lengths of YYARG.  */
  YYPTRDIFF_T yysize = 0;

  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yytoken != YYEMPTY)
    {
      int yyn = yypact[*yyssp];
      YYPTRDIFF_T yysize0 = yytnamerr (YY_NULLPTR, yytname[yytoken]);
      yysize = yysize0;
      yyarg[yycount++] = yytname[yytoken];
      if (!yypact_value_is_default (yyn))
        {
          /* Start YYX at -YYN if negative to avoid negative indexes in
             YYCHECK.  In other words, skip the first -YYN actions for
             this state because they are default actions.  */
          int yyxbegin = yyn < 0 ? -yyn : 0;
          /* Stay within bounds of both yycheck and yytname.  */
          int yychecklim = YYLAST - yyn + 1;
          int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
          int yyx;

          for (yyx = yyxbegin; yyx < yyxend; ++yyx)
            if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR
                && !yytable_value_is_error (yytable[yyx + yyn]))
              {
                if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
                  {
                    yycount = 1;
                    yysize = yysize0;
                    break;
                  }
                yyarg[yycount++] = yytname[yyx];
                {
                  YYPTRDIFF_T yysize1
                    = yysize + yytnamerr (YY_NULLPTR, yytname[yyx]);
                  if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
                    yysize = yysize1;
                  else
                    return 2;
                }
              }
        }
    }

  switch (yycount)
    {
# define YYCASE_(N, S)                      \
      case N:                               \
        yyformat = S;                       \
      break
    default: /* Avoid compiler warnings. */
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
# undef YYCASE_
    }

  {
    /* Don't count the "%s"s in the final size, but reserve room for
       the terminator.  */
    YYPTRDIFF_T yysize1 = yysize + (yystrlen (yyformat) - 2 * yycount) + 1;
    if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
      yysize = yysize1;
    else
      return 2;
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return 1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yyarg[yyi++]);
          yyformat += 2;
        }
      else
        {
          ++yyp;
          ++yyformat;
        }
  }
  return 0;
}
#endif /* YYERROR_VERBOSE */

/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
{
  YYUSE (yyvaluep);
  YYUSE (yylocationp);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YYUSE (yytype);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}




/* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Location data for the lookahead symbol.  */
YYLTYPE yylloc
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
  = { 1, 1, 1, 1 }
# endif
;
/* Number of syntax errors so far.  */
int yynerrs;


/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       'yyss': related to states.
       'yyvs': related to semantic values.
       'yyls': related to locations.

       Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* The state stack.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss;
    yy_state_t *yyssp;

    /* The semantic value stack.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs;
    YYSTYPE *yyvsp;

    /* The location stack.  */
    YYLTYPE yylsa[YYINITDEPTH];
    YYLTYPE *yyls;
    YYLTYPE *yylsp;

    /* The locations where the error started and ended.  */
    YYLTYPE yyerror_range[3];

    YYPTRDIFF_T yystacksize;

  int yyn;
  int yyresult;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYPTRDIFF_T yymsg_alloc = sizeof yymsgbuf;
#endif

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N), yylsp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  yyssp = yyss = yyssa;
  yyvsp = yyvs = yyvsa;
  yylsp = yyls = yylsa;
  yystacksize = YYINITDEPTH;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY; /* Cause a token to be read.  */
  yylsp[0] = yylloc;
  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    goto yyexhaustedlab;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;
        YYLTYPE *yyls1 = yyls;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yyls1, yysize * YYSIZEOF (*yylsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
        yyls = yyls1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          goto yyexhaustedlab;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
        YYSTACK_RELOCATE (yyls_alloc, yyls);
# undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
      yylsp = yyls + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */

  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = yylex ();
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
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END
  *++yylsp = yylloc;

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
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
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];

  /* Default location. */
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
  yyerror_range[1] = yyloc;
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
         }
#line 1929 "hoaparser.c"
    break;

  case 4:
#line 460 "hoa.y"
              {
                  loadedData->version = (yyvsp[0].string);
                  if (seenHeader[HOAHDR])
                      hdrItemError("HOA:");
                  else
                      seenHeader[HOAHDR] = true;
              }
#line 1941 "hoaparser.c"
    break;

  case 5:
#line 468 "hoa.y"
                         { /* no new item, nothing to check */ }
#line 1947 "hoaparser.c"
    break;

  case 6:
#line 470 "hoa.y"
           {
               if ((yyvsp[0].number) <= 8) {
                   if (seenHeader[(yyvsp[0].number)])
                       hdrItemError(headerStrs[(yyvsp[0].number)]);
                   else
                       seenHeader[(yyvsp[0].number)] = true;
               }
           }
#line 1960 "hoaparser.c"
    break;

  case 7:
#line 479 "hoa.y"
                                               {
                                                 loadedData->noStates = (yyvsp[0].number);
                                                 (yyval.number) = STATES;
                                               }
#line 1969 "hoaparser.c"
    break;

  case 8:
#line 483 "hoa.y"
                                               {
                                                 tempStart =
                                                    concatIntLists(
                                                        tempStart,
                                                        (yyvsp[0].numlist)
                                                    );
                                                 (yyval.number) = START;
                                               }
#line 1982 "hoaparser.c"
    break;

  case 9:
#line 491 "hoa.y"
                                               {
                                                 loadedData->noAPs = (yyvsp[-1].number);
                                                 tempAps = (yyvsp[0].strlist);
                                                 (yyval.number) = AP;
                                               }
#line 1992 "hoaparser.c"
    break;

  case 10:
#line 496 "hoa.y"
                                               {
                                                 tempCntAPs = (yyvsp[0].numlist);
                                                 (yyval.number) = CNTAP;
                                               }
#line 2001 "hoaparser.c"
    break;

  case 11:
#line 500 "hoa.y"
                                               {
                                                 tempAliases =
                                                    prependAliasNode(
                                                        tempAliases,
                                                        (yyvsp[-1].string), (yyvsp[0].tree)
                                                    );
                                                 (yyval.number) = ALIAS;
                                               }
#line 2014 "hoaparser.c"
    break;

  case 12:
#line 508 "hoa.y"
                                               { 
                                                 loadedData->noAccSets = (yyvsp[-1].number);
                                                 loadedData->acc = (yyvsp[0].tree);
                                                 (yyval.number) = ACCEPTANCE;
                                               }
#line 2024 "hoaparser.c"
    break;

  case 13:
#line 513 "hoa.y"
                                               { 
                                                 loadedData->accNameID = (yyvsp[-1].string);
                                                 tempAccNameParameters
                                                    = concatStrLists(
                                                        tempAccNameParameters,
                                                        (yyvsp[0].strlist)
                                                    );
                                                 (yyval.number) = ACCNAME;
                                               }
#line 2038 "hoaparser.c"
    break;

  case 14:
#line 522 "hoa.y"
                                               { (yyval.number) = TOOL; }
#line 2044 "hoaparser.c"
    break;

  case 15:
#line 523 "hoa.y"
                                               {
                                                 loadedData->name = (yyvsp[0].string);
                                                 (yyval.number) = NAME;
                                               }
#line 2053 "hoaparser.c"
    break;

  case 16:
#line 527 "hoa.y"
                                               { 
                                                 tempProperties =
                                                     concatStrLists(
                                                         tempProperties,
                                                         (yyvsp[0].strlist)
                                                     );
                                                 (yyval.number) = PROPERTIES;
                                               }
#line 2066 "hoaparser.c"
    break;

  case 17:
#line 535 "hoa.y"
                                               { free((yyvsp[-1].string)); (yyval.number) = HEADERNAME; }
#line 2072 "hoaparser.c"
    break;

  case 18:
#line 538 "hoa.y"
                                { (yyval.numlist) = newIntNode((yyvsp[0].number)); }
#line 2078 "hoaparser.c"
    break;

  case 19:
#line 539 "hoa.y"
                                { (yyval.numlist) = prependIntNode((yyvsp[0].numlist), (yyvsp[-2].number)); }
#line 2084 "hoaparser.c"
    break;

  case 20:
#line 542 "hoa.y"
                                        { (yyval.tree) = (yyvsp[0].tree); }
#line 2090 "hoaparser.c"
    break;

  case 21:
#line 543 "hoa.y"
                                        { (yyval.tree) = orBTree((yyvsp[-2].tree), (yyvsp[0].tree)); }
#line 2096 "hoaparser.c"
    break;

  case 22:
#line 546 "hoa.y"
                                            { (yyval.tree) = (yyvsp[0].tree); }
#line 2102 "hoaparser.c"
    break;

  case 23:
#line 547 "hoa.y"
                                            { (yyval.tree) = andBTree((yyvsp[-2].tree), (yyvsp[0].tree)); }
#line 2108 "hoaparser.c"
    break;

  case 24:
#line 550 "hoa.y"
                                 { (yyval.tree) = boolBTree((yyvsp[0].boolean)); }
#line 2114 "hoaparser.c"
    break;

  case 25:
#line 551 "hoa.y"
                                 { (yyval.tree) = apBTree((yyvsp[0].number)); }
#line 2120 "hoaparser.c"
    break;

  case 26:
#line 552 "hoa.y"
                                 { (yyval.tree) = aliasBTree((yyvsp[0].string)); }
#line 2126 "hoaparser.c"
    break;

  case 27:
#line 553 "hoa.y"
                                 { (yyval.tree) = notBTree((yyvsp[0].tree)); }
#line 2132 "hoaparser.c"
    break;

  case 28:
#line 554 "hoa.y"
                                 { (yyval.tree) = (yyvsp[-1].tree); }
#line 2138 "hoaparser.c"
    break;

  case 29:
#line 557 "hoa.y"
                                                   { (yyval.tree) = (yyvsp[0].tree); }
#line 2144 "hoaparser.c"
    break;

  case 30:
#line 558 "hoa.y"
                                                   { (yyval.tree) = orBTree((yyvsp[-2].tree), (yyvsp[0].tree)); }
#line 2150 "hoaparser.c"
    break;

  case 31:
#line 561 "hoa.y"
                                               { (yyval.tree) = (yyvsp[0].tree); }
#line 2156 "hoaparser.c"
    break;

  case 32:
#line 562 "hoa.y"
                                               { (yyval.tree) = andBTree((yyvsp[-2].tree), (yyvsp[0].tree)); }
#line 2162 "hoaparser.c"
    break;

  case 33:
#line 565 "hoa.y"
                                       { (yyval.tree) = accidBTree((yyvsp[-3].nodetype), (yyvsp[-1].number), false); }
#line 2168 "hoaparser.c"
    break;

  case 34:
#line 566 "hoa.y"
                                       { (yyval.tree) = accidBTree((yyvsp[-4].nodetype), (yyvsp[-1].number), true); }
#line 2174 "hoaparser.c"
    break;

  case 35:
#line 567 "hoa.y"
                                       { (yyval.tree) = (yyvsp[-1].tree); }
#line 2180 "hoaparser.c"
    break;

  case 36:
#line 568 "hoa.y"
                                       { (yyval.tree) = boolBTree((yyvsp[0].boolean)); }
#line 2186 "hoaparser.c"
    break;

  case 37:
#line 571 "hoa.y"
           { (yyval.nodetype) = NT_FIN; }
#line 2192 "hoaparser.c"
    break;

  case 38:
#line 572 "hoa.y"
           { (yyval.nodetype) = NT_INF; }
#line 2198 "hoaparser.c"
    break;

  case 39:
#line 575 "hoa.y"
                                          { (yyval.strlist) = NULL; }
#line 2204 "hoaparser.c"
    break;

  case 40:
#line 576 "hoa.y"
                                          { 
                                            (yyval.strlist) = (yyvsp[-1].boolean) ? prependStrNode((yyvsp[0].strlist),
                                                                     strdup("True"))
                                                : prependStrNode((yyvsp[0].strlist),
                                                                 strdup("False"));
                                          }
#line 2215 "hoaparser.c"
    break;

  case 41:
#line 582 "hoa.y"
                                          {
                                            char buffer[66];
                                            sprintf(buffer, "%d", (yyvsp[-1].number));
                                            (yyval.strlist) = prependStrNode((yyvsp[0].strlist),
                                                                strdup(buffer));
                                          }
#line 2226 "hoaparser.c"
    break;

  case 42:
#line 588 "hoa.y"
                                          { (yyval.strlist) = prependStrNode((yyvsp[0].strlist), (yyvsp[-1].string)); }
#line 2232 "hoaparser.c"
    break;

  case 47:
#line 595 "hoa.y"
                                                { free((yyvsp[0].string)); }
#line 2238 "hoaparser.c"
    break;

  case 48:
#line 597 "hoa.y"
                                { (yyval.strlist) = NULL; }
#line 2244 "hoaparser.c"
    break;

  case 49:
#line 598 "hoa.y"
                                { (yyval.strlist) = prependStrNode((yyvsp[0].strlist), (yyvsp[-1].string)); }
#line 2250 "hoaparser.c"
    break;

  case 50:
#line 601 "hoa.y"
                            { (yyval.strlist) = NULL; }
#line 2256 "hoaparser.c"
    break;

  case 51:
#line 602 "hoa.y"
                            { (yyval.strlist) = prependStrNode((yyvsp[0].strlist), (yyvsp[-1].string)); }
#line 2262 "hoaparser.c"
    break;

  case 52:
#line 606 "hoa.y"
    { tempStates = (yyvsp[0].statelist); }
#line 2268 "hoaparser.c"
    break;

  case 53:
#line 609 "hoa.y"
                            { (yyval.statelist) = NULL; }
#line 2274 "hoaparser.c"
    break;

  case 54:
#line 611 "hoa.y"
              {
                (yyval.statelist) = prependStateNode((yyvsp[0].statelist), (yyvsp[-2].statelist), (yyvsp[-1].trlist));
              }
#line 2282 "hoaparser.c"
    break;

  case 55:
#line 617 "hoa.y"
          { (yyval.statelist) = newStateNode((yyvsp[-2].number), (yyvsp[-1].string), (yyvsp[-3].tree), (yyvsp[0].numlist)); }
#line 2288 "hoaparser.c"
    break;

  case 56:
#line 620 "hoa.y"
                                { (yyval.tree) = NULL; }
#line 2294 "hoaparser.c"
    break;

  case 57:
#line 621 "hoa.y"
                                { (yyval.tree) = (yyvsp[-1].tree); }
#line 2300 "hoaparser.c"
    break;

  case 58:
#line 624 "hoa.y"
                          { (yyval.string) = NULL; }
#line 2306 "hoaparser.c"
    break;

  case 59:
#line 625 "hoa.y"
                          { (yyval.string) = (yyvsp[0].string); }
#line 2312 "hoaparser.c"
    break;

  case 60:
#line 628 "hoa.y"
                               { (yyval.numlist) = NULL; }
#line 2318 "hoaparser.c"
    break;

  case 61:
#line 629 "hoa.y"
                               { (yyval.numlist) = (yyvsp[-1].numlist); }
#line 2324 "hoaparser.c"
    break;

  case 62:
#line 632 "hoa.y"
                       { (yyval.numlist) = NULL; }
#line 2330 "hoaparser.c"
    break;

  case 63:
#line 633 "hoa.y"
                       { (yyval.numlist) = prependIntNode((yyvsp[0].numlist), (yyvsp[-1].number)); }
#line 2336 "hoaparser.c"
    break;

  case 64:
#line 636 "hoa.y"
                        { (yyval.trlist) = NULL; }
#line 2342 "hoaparser.c"
    break;

  case 65:
#line 638 "hoa.y"
          { (yyval.trlist) = prependTransNode((yyvsp[0].trlist), (yyvsp[-3].tree), (yyvsp[-2].numlist), (yyvsp[-1].numlist)); }
#line 2348 "hoaparser.c"
    break;


#line 2352 "hoaparser.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;
  *++yylsp = yyloc;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYEMPTY : YYTRANSLATE (yychar);

  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
# define YYSYNTAX_ERROR yysyntax_error (&yymsg_alloc, &yymsg, \
                                        yyssp, yytoken)
      {
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = YYSYNTAX_ERROR;
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == 1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = YY_CAST (char *, YYSTACK_ALLOC (YY_CAST (YYSIZE_T, yymsg_alloc)));
            if (!yymsg)
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = 2;
              }
            else
              {
                yysyntax_error_status = YYSYNTAX_ERROR;
                yymsgp = yymsg;
              }
          }
        yyerror (yymsgp);
        if (yysyntax_error_status == 2)
          goto yyexhaustedlab;
      }
# undef YYSYNTAX_ERROR
#endif
    }

  yyerror_range[1] = yylloc;

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
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

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;

  /* Do not reclaim the symbols of the rule whose action triggered
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
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
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

      yyerror_range[1] = *yylsp;
      yydestruct ("Error: popping",
                  yystos[yystate], yyvsp, yylsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  yyerror_range[2] = yylloc;
  /* Using YYLLOC is tempting, but would change the location of
     the lookahead.  YYLOC is available though.  */
  YYLLOC_DEFAULT (yyloc, yyerror_range, 2);
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


#if !defined yyoverflow || YYERROR_VERBOSE
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif


/*-----------------------------------------------------.
| yyreturn -- parsing is finished, return the result.  |
`-----------------------------------------------------*/
yyreturn:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval, &yylloc);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
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
  return yyresult;
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
