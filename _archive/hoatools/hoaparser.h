/* A Bison parser, made by GNU Bison 3.5.  */

/* Bison interface for Yacc-like parsers in C

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

/* Undocumented macros, especially those whose name start with YY_,
   are private implementation details.  Do not rely on them.  */

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

#line 103 "hoaparser.h"

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
