CFLAGS ?= -std=gnu99 -Wall -Wextra -pedantic -O2 -g
LDFLAGS ?= -lrt
cswrap: cswrap.o
clean:
	rm -f cswrap cswrap.o
