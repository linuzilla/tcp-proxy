%{
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
#include "parser.h"
#include "sysconf.h"
%}

%token RW_PROXYARP RW_WITH    RW_FLAG_ON RW_FLAG_OFF
%token IDENTIFIER  DIGIT      IPSTRING   MACSTRING   QSTRING FQSTRING
%token '=' ';' ',' '[' ']'

%%

full_definition		: system_definitions
			;

system_definitions	: variable_definition
			| variable_definition system_definitions
			;

variable_definition	: IDENTIFIER '=' DIGIT ';'       { system_config->addentry_integer ($1, $3); }
			| IDENTIFIER '=' QSTRING ';'     { system_config->addentry_string ($1, $3); }
			| IDENTIFIER '=' RW_FLAG_ON  ';' { system_config->addentry_flag_on  ($1); }
			| IDENTIFIER '=' RW_FLAG_OFF ';' { system_config->addentry_flag_off ($1); }
			| IDENTIFIER '=' '[' comma_list_data ']' ';' { system_config->add_prepared_list($1); }
			;

comma_list_data		: comma_list_number { system_config->set_list_as_integer(); }
			| comma_list_string { system_config->set_list_as_string(); }
			;

comma_list_number	: DIGIT { system_config->list_append_int($1); }
			| DIGIT ',' comma_list_number { system_config->list_append_int($1); }
			;

comma_list_string	: QSTRING { system_config->list_append_str($1); }
			| QSTRING ',' comma_list_string { system_config->list_append_str($1); }
			;

%%


#ifdef __cplusplus
extern "C" 
#endif
int yywrap (void) { return 1; }
