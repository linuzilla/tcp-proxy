#

CC	= gcc
#MYSQL_CONFIG = mariadb_config
MYSQL_CONFIG = mysql_config
CFLAGS  = -Wall -O3 -fPIC -g -Wno-unused-result -DMYSQL_VERSION=$(shell $(MYSQL_CONFIG) --version | sed 's/\([0-9]*\).*/\1/')

VER := $(shell sed -e 's/.*\"\(.*\)\"/\1/' ../VERSION)

CFLAGS += $(shell $(MYSQL_CONFIG)  --cflags)
CFLAGS += -DAPP_VERSION=\"$(VER)\"
CFLAGS += -I../include

SRC := $(wildcard *.c)
SRC := $(filter-out lex.yy.c y.tab.c, $(SRC))

OBJ = $(SRC:.c=.o)

LEXYACCTMP = lex.yy.c y.tab.c y.tab.h y.output y.tab.o lex.yy.o
CLEANFILES = app $(OBJ) $(LEXYACCTMP)

LOPT = -lpthread -L/usr/local/lib
LOPT += -lpcre2-8 -lreadline -ldl
LOPT +=  $(shell $(MYSQL_CONFIG)   --libs)
LOPT +=  $(shell pkg-config --libs openssl)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

all: app

lex.yy.c:	lexer.l
	flex lexer.l

y.tab.c:	parser.y
	bison -t -d -y parser.y

app:	$(OBJ) lex.yy.c y.tab.c
	@rm -f $@
	$(CC) $(CFLAGS) -Wno-unused-function -o $@ $(OBJ) lex.yy.c y.tab.c $(LOPT)

clean:
	rm -f $(CLEANFILES)

fmt:
	astyle --style=google --pad-comma --pad-oper --pad-first-paren-out --keep-one-line-statements --quiet *.c
