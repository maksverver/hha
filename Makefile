BASE_CFLAGS=-ansi -D_POSIX_C_SOURCE -Os
SOURCES=hha.c lzma_compression.c deflate_compression.c create_archive.c
OBJECTS=hha.o lzma_compression.o deflate_compression.o create_archive.o

# Local config:
CFLAGS=$(BASE_CFLAGS) -Wall -Wextra -g -Iinclude/linux64
LDLIBS=libs/linux64/lzma.a libs/linux64/libz.a

all: hha

hha: $(OBJECTS)
	$(CC) $(LDFLAGS) -o "$@" $^ $(LDLIBS)

clean:
	rm -f $(OBJECTS)

distclean: clean
	rm -f hha hha-linux32 hha-win32.exe

dist: hha-linux32 hha-win32.exe

hha-linux32: $(SOURCES) libs/linux32/libz.a libs/linux32/lzma.a
	$(CC) -m32 $(BASE_CFLAGS) -Iinclude/linux32 -o "$@" $^
	strip "$@"

hha-win32.exe: $(SOURCES) libs/win32/libz.a libs/win32/lzma.a
	i386-mingw32msvc-gcc -DWIN32 -m32 $(BASE_CFLAGS) -Iinclude/win32 -Llibs/win32 -o "$@" $^
	strip "$@"

.PHONY: all clean dist distclean
