CFLAGS=-Wall -Wextra -g -ansi -D_POSIX_C_SOURCE
LDLIBS=-lz

all: hha

clean:

distclean:
	rm -f hha hha-linux32 hha-win32

dist: hha-linux32 hha-win32.exe

hha-linux32: hha.c libs/linux32/libz.a
	$(CC) $(CFLAGS) -o "$@" -m32 -Os $^
	strip "$@"

hha-win32.exe: hha.c libs/win32/libz.a
	i386-mingw32msvc-gcc -o "$@" -I include/win32 -L libs/win32 $^
	strip "$@"

.PHONY: all clean dist distclean
