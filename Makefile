CFLAGS=-Wall -g -ansi -D_POSIX_C_SOURCE
LDLIBS=-lz

all: hha

clean:

distclean:
	rm -f hha

.PHONY: all clean distclean
