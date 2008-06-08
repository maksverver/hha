#ifndef COMMON_H_INCLUDED
#define COMMON_H_INCLUDED

#include <stdint.h>

#pragma pack(push,1)

typedef struct Header Header;
typedef struct IndexEntry IndexEntry;

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

#endif /* ndef COMMON_H_INCLUDED */
