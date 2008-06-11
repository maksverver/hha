#ifndef COMMON_H_INCLUDED
#define COMMON_H_INCLUDED

#include <stdint.h>
#include <stdio.h>

#pragma pack(push,1)

struct Header
{
    uint32_t unknown1;
    uint16_t unknown2;
    uint16_t unknown3;
    uint32_t strings_size;
    uint32_t index_entries;
};

struct IndexEntry
{
    uint32_t dir_name;
    uint32_t file_name;
    uint32_t compression;
    uint32_t offset;
    uint32_t size;
    uint32_t stored_size;
};

#pragma pack(pop)

enum Compression
{
    COM_NONE    = 0,
    COM_DEFLATE = 1,
    COM_LZMA    = 2
};

typedef struct Header Header;
typedef struct IndexEntry IndexEntry;
typedef enum Compression Compression;


/* Compression/decompression functions

   Convert the first ``size'' bytes from src writing the result into ``dst''.
   The number of bytes written is returned.
*/
size_t copy_uncompressed(FILE *dst, FILE *src, size_t size);
size_t copy_deflated(FILE *dst, FILE *src, size_t size);
size_t copy_deflatec(FILE *dst, FILE *src, size_t size);
size_t copy_lzmad(FILE *dst, FILE *src, size_t size);
size_t copy_lzmac(FILE *dst, FILE *src, size_t size);

/* Archive creation */
void create_archive( const char *archive_path,
                     const char * const *dirs_begin,
                     const char * const *dirs_end,
                     Compression com );


#endif /* ndef COMMON_H_INCLUDED */
