%{
#include <stdio.h>
%}

%x C_COMMENT

%%
\/\*                     { BEGIN(C_COMMENT); }
<C_COMMENT>\*\/          { BEGIN(INITIAL); }
<C_COMMENT>.|\n          { }

\"(\.|[^\"])*\"          printf("STRING ");
0|[1-9][0-9]*            printf("INT ");
[tf]                     printf("BOOLEAN ");
[a-zA-Z_][0-9a-zA-Z_-]*  printf("IDENTIFIER ");
@[0-9a-zA-Z_-]+          printf("ANAME ");
[a-zA-Z_][0-9a-zA-Z_-]*: printf("HEADERNAME ");
--BODY--                 printf("BEGINBODY ");
--END--                  printf("ENDBODY ");
[ \t\n\r]+                  /* ignore whitespace */;
%%
