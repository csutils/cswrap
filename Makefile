CFLAGS ?= -std=gnu99 -Wall -Wextra -pedantic -O2 -g
abscc: abscc.o
clean:
	rm -f abscc abscc.o
