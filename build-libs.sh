#!/bin/sh

source win32-env.sh

ZLIBOPT="-O3"
LZMAOPT="-O2"
LZMASRC="7zBuf.c 7zCrc.c Alloc.c Bcj2.c Bra86.c Bra.c BraIA64.c LzFind.c \
         LzmaDec.c LzmaEnc.c LzmaLib.c"


build_zlib() {

cd zlib-1.2.3 || { echo 'Missing directory zlib-1.2.3'; exit 1; }

echo 'Building zlib for Linux64'
make clean
CFLAGS="$ZLIBOPT -m64" ./configure
make libz.a
cp libz.a ../libs/linux64/

echo 'Building zlib for Linux32'
make clean
CFLAGS="$ZLIBOPT -m32 -march=i686" ./configure
make libz.a
cp libz.a ../libs/linux32/

echo 'Building zlib for Win32'
make clean
CFLAGS="$ZLIBOPT -m32 -march=i686" CC=i386-mingw32msvc-gcc ./configure
make libz.a
i386-mingw32msvc-ranlib libz.a
cp libz.a ../libs/win32/

cd ..

}


build_lzma() {

cd lzma-4.5.8/C || { echo 'Missing directory lzma-4.5.8/C'; exit 1; }

echo 'Building LZMA for Linux64'
rm -f *.o lzma.a
for file in $LZMASRC; do gcc $LZMAOPT -m64 -c $file; done
ar cr lzma.a *.o
cp lzma.a ../../libs/linux64/

echo 'Building LZMA for Linux32'
rm -f *.o lzma.a
for file in $LZMASRC; do gcc $LZMAOPT -m32 -march=i686 -c $file; done
ar cr lzma.a *.o
cp lzma.a ../../libs/linux32/

echo 'Building LZMA for Win32'
rm -f *.o lzma.a
for file in $LZMASRC; do i386-mingw32msvc-gcc $LZMAOPT -m32 -march=i686 -c $file; done
i386-mingw32msvc-ar cr lzma.a *.o
i386-mingw32msvc-ranlib lzma.a
cp lzma.a ../../libs/win32/

cd ../..

}


[ "x$@" = "x" ] && libs="zlib lzma" || libs = "$@"
for lib in $libs; do "build_$lib"; done 

