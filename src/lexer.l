%{

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "parser.h"
#include "y.tab.h"

int     lineno = 1;
char    linebuf[512];

%}

ws		[ \t]+
comment		\/\/.*|#.*
id		[_a-zA-Z][\-_a-zA-Z0-9]*
string		\"[^\"\n]*\"

%%

{ws}			/* do nothing */ ;
{comment}		/* do nothing */ ;
^.*			{ strncpy (linebuf, yytext, 511); yyless(0); }
\n			{ lineno++;                  }
\x1a|\r			{ /* *** ignore *** */       }
on			{ return RW_FLAG_ON;         }
off			{ return RW_FLAG_OFF;        }
{id}			{ yylval = strdup(yytext); return IDENTIFIER; }
{string}		{ yylval = strdup(yytext); return QSTRING;    }
[0-9]+			{ yylval = strdup(yytext); return DIGIT;      }
.			{ return yytext[0];       }

%%

void yyerror(const char *msg) {
    fprintf(stderr, "line %d: %s: %s\n", lineno, msg, linebuf);
}
