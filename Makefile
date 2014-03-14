CFLAGS ?= -std=gnu99 -Wall -Wextra -pedantic -O2 -g
LDFLAGS ?= -pthread

.PHONY: all clean

all: cswrap cswrap.1

cswrap: cswrap.o

cswrap.1: cswrap.txt
	a2x -f manpage -v $<

clean:
	rm -f cswrap cswrap.o cswrap.1 cswrap.xml
