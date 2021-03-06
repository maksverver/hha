#include "common.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PATH_LEN 1024


int ftruncate(int fd, off_t length);


/* Global variables -- used while creating an archive */

struct Header header;
FILE *fp;
size_t pos;

/* Index table */
struct IndexEntry *entries;
size_t entries_size, entries_capacity;

/* String table */
char *strings;
size_t strings_size, strings_capacity;

static void free_entries()
{
    free(entries);
    entries = NULL;
    entries_size = entries_capacity = 0;
}

static void free_strings()
{
    free(strings);
    strings = NULL;
    strings_size = strings_capacity = 0;
}

static uint32_t alloc_string(const char *str)
{
    size_t pos, len;

    len = strlen(str);

    /* Allocate space (if required) */
    while (strings_capacity - strings_size < len + 1)
    {
        strings_capacity = strings_capacity > 0 ? 2*strings_capacity : 64;
        assert((uint32_t)strings_capacity == strings_capacity);
        strings = realloc(strings, strings_capacity);
        assert(strings != NULL);
        memset(strings + strings_size, 0, strings_capacity - strings_size);
    }

    /* Copy string */
    pos = strings_size;
    strings_size += len + 1;
    memcpy(strings + pos, str, len);

    return (uint32_t)pos;
}

static void alloc_entry(const char *dir, const char *file, size_t file_size)
{
    IndexEntry *e;

    /* Allocate space (if required) */
    if (entries_size == entries_capacity)
    {
        entries_capacity = entries_capacity > 0 ? 2*entries_capacity : 8;
        entries = realloc(entries, sizeof(IndexEntry)*entries_capacity);
        assert(entries != NULL);
    }

    e = &entries[entries_size++];
    if (entries_size > 1 && strcmp(strings + e[-1].dir_name, dir) == 0)
    {
        /* Directory is the same as last entry; use the same address. */
        e->dir_name     = e[-1].dir_name;
    }
    else
    {
        e->dir_name     = alloc_string(dir);
    }
    e->file_name    = alloc_string(file);
    e->compression  = 0;
    e->offset       = 0;
    e->size         = file_size;
    e->stored_size  = 0;
}

static void walk(char path[PATH_LEN], size_t path_len)
{
    DIR *dir;
    struct dirent *de;
    struct stat st;
    size_t len;

    dir = opendir(path);
    if (dir == NULL)
    {
        perror(path);
        return;
    }
    path[path_len] = '/';
    while ((de = readdir(dir)) != NULL)
    {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
        {
            continue;
        }

        /* Check length */
        len = strlen(de->d_name);
        if (path_len + 1 + len + 1 > PATH_LEN)
        {
            fprintf(stderr, "%s/%s: path too long; skipped.\n",
                            path, de->d_name);
            continue;
        }

        /* Append name to path */
        memcpy(path + path_len + 1, de->d_name, len);
        path[path_len + 1 + len] = '\0';
        if (stat(path, &st) != 0)
        {
            perror(path);
            continue;
        }

        if (S_ISREG(st.st_mode))
        {
            if ((uint32_t)st.st_size != st.st_size)
            {
                printf("%s: file too big; skipped.\n", path);
                continue;
            }
            path[path_len] = '\0';
            alloc_entry(path, path + path_len + 1, (size_t)st.st_size);
            path[path_len] = '/';
        }
        else
        if (S_ISDIR(st.st_mode))
        {
            walk(path, path_len + 1 + len);
        }
        else
        {
            fprintf(stderr, "%s: unsupported file type; skipped.\n", path);
        }
    }
    path[path_len] = '\0';
    closedir(dir);
}

static void write_padding()
{
    static const char padding[16] = { 0 };
    size_t len;

    if (pos%16 != 0)
    {
        len = 16 - pos%16;
        if (fwrite(padding, len, 1, fp) != 1)
        {
            perror("Could not write padding\n");
            abort();
        }
        pos += len;
    }
}

static void write_headers()
{
    /* Write header, strings table, and preliminary index table */
    pos = 0;
    if (fwrite(&header, sizeof(header), 1, fp) != 1)
    {
        perror("Could not write archive header");
        abort();
    }
    pos += sizeof(header);

    if (fwrite(strings, 1, header.strings_size, fp) != header.strings_size)
    {
        perror("Could not write string table");
        abort();
    }
    pos += header.strings_size;

    if (fwrite(entries, sizeof(IndexEntry), entries_size, fp) != entries_size)
    {
        perror("Could not write index");
        abort();
    }
    pos += header.index_entries*sizeof(IndexEntry);

    write_padding();
}

static size_t write_file(const char *path, size_t size_in, Compression *max_com)
{
    FILE *fp_in;
    Compression best_com;
    size_t size, best_size;

    fp_in = fopen(path, "rb");
    assert(fp_in != NULL);

    /* Initially, assume no compression is best. */
    best_com  = COM_NONE;
    best_size = size_in;

    if (*max_com >= COM_DEFLATE)
    {
        /* Try deflate compression */
        fseek(fp, pos, SEEK_SET);
        rewind(fp_in);
        size = copy_deflatec(fp, fp_in, size_in);
        if (size < best_size)
        {
            best_size = size;
            best_com  = COM_DEFLATE;
        }
    }

    if (*max_com >= COM_LZMA)
    {
        /* Try LZMA compression */
        fseek(fp, pos, SEEK_SET);
        rewind(fp_in);
        size = copy_lzmac(fp, fp_in, size_in);
        if (size < best_size)
        {
            best_size = size;
            best_com  = COM_LZMA;
        }
    }

    if (best_com == COM_NONE)
    {
        fseek(fp, pos, SEEK_SET);
        rewind(fp_in);
        size = copy_uncompressed(fp, fp_in, size_in);
    }

    if (best_com == COM_DEFLATE && *max_com > COM_DEFLATE)
    {
        fseek(fp, pos, SEEK_SET);
        rewind(fp_in);
        size = copy_deflatec(fp, fp_in, size_in);
    }

    fclose(fp_in);

    *max_com = best_com;
    assert(size == best_size);
    return best_size;
}

static void write_files(Compression max_com)
{
    size_t i;
    char path[PATH_LEN];
    Compression com;
    size_t stored_size;

    for (i = 0; i < entries_size; ++i)
    {
        strcpy(path, strings + entries[i].dir_name);
        strcat(path, "/");
        strcat(path, strings + entries[i].file_name);

        printf("Adding %s...\n", path);

        com = max_com;
        stored_size = write_file(path, entries[i].size, &com);

        entries[i].compression = com;
        entries[i].offset      = pos;
        entries[i].stored_size = stored_size;

        pos += entries[i].stored_size;
        write_padding();
    }

    /* Truncate to remove extra data */
    ftruncate(fileno(fp), pos);
}

static void rewrite_index()
{
    if (fseek(fp, sizeof(Header) + header.strings_size, SEEK_SET) != 0)
    {
        perror("Could not seek to archive index");
        abort();
    }
    if (fwrite(entries, sizeof(IndexEntry), entries_size, fp) != entries_size)
    {
        perror("Could not rewrite index");
        abort();
    }
}

/* NOTE: this function is NOT re-entrant! */
void create_archive( const char *archive_path,
                     const char * const *dirs_begin,
                     const char * const *dirs_end,
                     Compression com )
{
    const char * const *p;
    size_t len;
    char path[PATH_LEN];

    assert(sizeof(Header)     == 16);
    assert(sizeof(IndexEntry) == 24);

    /* Open output archive */
    fp = fopen(archive_path, "w+b");
    if (fp == NULL)
    {
        perror(archive_path);
        abort();
    }

    /* Find all files to process by walking the directory trees */
    for (p = dirs_begin; p != dirs_end; ++p)
    {
        printf("Searching for files in directory %s...\n", *p);

        len = strlen(*p);
        while (len > 0 && (*p)[len - 1] == '/') --len;
        if (len + 1 > PATH_LEN)
        {
            fprintf(stderr, "%s: path too long; skipped.\n", *p);
            continue;
        }
        memcpy(path, *p, len);
        path[len] = '\0';
        walk(path, len);
    }

    /* Create header */
    header.unknown1         = (uint32_t)0xac2ff34ful;
    header.unknown2         = 0;
    header.unknown3         = 1;
    header.strings_size     = (uint32_t)strings_size;
    header.index_entries    = (uint32_t)entries_size;

    /* Pad string table size to 16 byte boundary */
    while (header.strings_size%16 != 0) ++header.strings_size;

    /* Write headers */
    write_headers();
    write_files(com);
    rewrite_index();

    free_entries();
    free_strings();
    fclose(fp);
}
