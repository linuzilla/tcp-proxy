#ifndef __PARSER_H_
#define __PARSER_H_

#include <sys/types.h>
#include <netinet/in.h>
#include <stdio.h>

typedef char *  char_ptr;
#define YYSTYPE char_ptr
#define YY_NO_UNPUT

#define MAX_IPMASK	(1024)

typedef unsigned short       u_int16;
typedef unsigned int         u_int32;
typedef unsigned long long   u_int64;

extern FILE     *yyin;
extern void     yyerror (const char *);
extern int      yylex   (void);
extern int      yydebug;
extern int      yyparse (void);
extern YYSTYPE	yylval;


#endif
