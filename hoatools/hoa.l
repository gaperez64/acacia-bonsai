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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>    /* Must go BEFORE hoaparser.h */

#include "simplehoa.h"  /* Must go BEFORE hoaparser.h */
#include "hoaparser.h"

char* getString(char* s) {
    int newLen = strlen(s) - 1;
    char* str = (char*) malloc(sizeof(char) * newLen);
    strncpy(str, s + 1, newLen - 1);
    str[newLen - 1] = '\0';
    return str;
}

char* getAlias(char* s) {
    char* str = (char*) malloc(sizeof(char) * strlen(s));
    strcpy(str, s + 1);
    return str;
}
%}

%option yylineno

%x C_COMMENT

%%
\/\*                     { BEGIN(C_COMMENT); }
<C_COMMENT>\*\/          { BEGIN(INITIAL); }
<C_COMMENT>.|\n          { }

\"(\.|[^\"])*\"          { yylval.string = getString(yytext); return STRING; }
HOA:                     return HOAHDR;
Start:                   return START;
States:                  return STATES;
controllable-AP:         return CNTAP;
AP:                      return AP;
Alias:                   return ALIAS;
Acceptance:              return ACCEPTANCE;
acc-name:                return ACCNAME;
tool:                    return TOOL;
name:                    return NAME;
properties:              return PROPERTIES;
State:                   return STATEHDR;
[a-zA-Z_][0-9a-zA-Z_-]*: { yylval.string = strdup(yytext); return HEADERNAME; }
0|[1-9][0-9]*            { yylval.number = atoi(yytext); return INT; }
[tf]                     { yylval.boolean = !((bool)strcmp(yytext, "t")); return BOOL; }
Fin                      return FIN;
Inf                      return INF;
--BODY--                 return BEGINBODY;
--END--                  return ENDBODY;
[a-zA-Z_][0-9a-zA-Z_-]*  { yylval.string = strdup(yytext); return IDENTIFIER; }
@[0-9a-zA-Z_-]+          { yylval.string = getAlias(yytext); return ANAME; }
[ \t\n\r]+               /* ignore whitespace */;
\(                       return LPAR;
\)                       return RPAR;
\{                       return LBRACE;
\}                       return RBRACE;
\[                       return LSQBRACE;
\]                       return RSQBRACE;
&                        return BOOLAND;
\|                       return BOOLOR;
!                        return BOOLNOT;
%%
