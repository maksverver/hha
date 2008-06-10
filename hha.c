#include "common.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef WIN32
#define mkdir(path,mode) mkdir(path)
#endif

static enum { LIST, EXTRACT } arg_mode; /* Mode of operation */
static char *arg_path;                  /* Path to archive */

static FILE *fp;                /* File pointer */
static size_t file_size;        /* Size of file */

static char *strings;           /* String table */
static size_t strings_size;     /* Size of string table */

static IndexEntry *entries;     /* Index entries */
static size_t entries_size;     /* Number of index entries */

void seekto(size_t pos)
{
    if (fseek(fp, (long)pos, SEEK_SET) == -1)
    {
        perror("Seek failed");
        abort();
    }
}

static void create_dir(char *path)
{
    char *p;

    for (p = path + 1; *p != '\0'; ++p)
    {
        if (*p == '/')
        {
            *p = '\0';
            if (mkdir(path, 0755) != 0 && errno != EEXIST)
            {
                perror("Could not create directory");
                abort();
            }
            *p = '/';
        }
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
    {
        perror("Could not create directory");
        abort();
    }
}

static uint32_t read_uint32()
{
    uint8_t bytes[4];

    if (fread(bytes, 4, 1, fp) != 1)
    {
        perror("Read failed");
        abort();
    }

    return ((uint32_t)bytes[0] <<  0) | ((uint32_t)bytes[1] <<  8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void open_archive(const char *path)
{
    long lpos;

    /* Open file */
    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        perror("Could not open archive file");
        abort();
    }

    /* Seek to end to determine file size */
    if (fseek(fp, 0, SEEK_END) == -1 || (lpos = ftell(fp)) == -1)
    {
        perror("Could not determine file size");
        abort();
    }
    fseek(fp, 0, SEEK_SET);
    file_size = (size_t)lpos;
}

static void process_header()
{
    seekto(8);
    strings_size = read_uint32();
    entries_size = read_uint32();
    assert( strings_size <= file_size - sizeof(Header) );
    assert( entries_size <= (file_size - strings_size - sizeof(Header)) /
                             sizeof(IndexEntry) );

    /* Allocate and read string table */
    strings = malloc(strings_size + 1);
    if (strings == NULL)
    {
        perror("Could not allocate memory for string table");
        abort();
    }
    if (fread(strings, 1, strings_size, fp) != strings_size)
    {
        perror("Could not read string table");
        abort();
    }
    strings[strings_size] = '\0';   /* zero-terminate strings table */

    /* Allocate and read index */
    entries = malloc(sizeof(IndexEntry)*entries_size);
    if (entries == NULL)
    {
        perror("Could not allocate memory for string table");
        abort();
    }
    if (fread(entries, sizeof(IndexEntry), entries_size, fp) != entries_size)
    {
        perror("Could not read index entries");
        abort();
    }
}

static const char *strat(size_t pos)
{
    assert(pos <= strings_size);
    return strings + pos;
}

static void copy_uncompressed(FILE *src, FILE *dest, size_t size)
{
    char buffer[4096];
    size_t chunk;

    while (size > 0)
    {
        chunk = size > sizeof(buffer) ? sizeof(buffer) : size;
        if (fread(buffer, 1, chunk, src) != chunk)
        {
            perror("Read failed");
            abort();
        }
        if (fwrite(buffer, 1, chunk, dest) != chunk)
        {
            perror("Write failed");
            abort();
        }
        size -= chunk;
    }
}

static void list_entries()
{
    size_t i;
    char path[1024];
    const char *dir_name, *file_name;

    printf( "Compression   Offset       Size     Stored Size "
            " File path                    \n" );

    printf( "----------- ----------- ----------- ----------- "
            "------------------------------\n" );

    for (i = 0; i < entries_size; ++i)
    {
        dir_name  = strat(entries[i].dir_name);
        file_name = strat(entries[i].file_name);

        assert(strlen(dir_name) + 1 + strlen(file_name) < sizeof(path));
        sprintf(path, "%s/%s", dir_name, file_name);
        printf( "%10ld  %10ld  %10ld  %10ld   %s\n",
                (long)entries[i].compression, (long)entries[i].offset,
                (long)entries[i].size, (long)entries[i].stored_size, path );
    }

    printf( "----------- ----------- ----------- ----------- "
            "------------------------------\n" );
}

static void extract_entries()
{
    size_t i;
    char path[1024];
    const char *dir_name, *file_name;
    FILE *fp_new;

    for (i = 0; i < entries_size; ++i)
    {
        dir_name  = strat(entries[i].dir_name);
        file_name = strat(entries[i].file_name);

        if (entries[i].compression > 2)
        {
            printf("Skipping %s/%s (compression type %d unknown)\n",
                   dir_name, file_name, entries[i].compression);
            continue;
        }

        assert(strlen(dir_name) + 1 + strlen(file_name) < sizeof(path));
        strncpy(path, dir_name, sizeof(path));
        create_dir(path);
        strncat(path, "/", sizeof(path));
        strncat(path, file_name, sizeof(path));

        fp_new = fopen(path, "wb");
        if (fp_new == NULL)
        {
            perror("Could not open file");
            continue;
        }

        seekto(entries[i].offset);

        switch (entries[i].compression)
        {
        case 0:
            printf("Extracting %s/%s (uncompressed)\n", dir_name, file_name);
            copy_uncompressed(fp, fp_new, entries[i].stored_size);
            break;

        case 1:
            printf("Extracting %s/%s (deflated)\n", dir_name, file_name);
            copy_deflated(fp, fp_new, entries[i].stored_size);
            break;

        case 2:
            printf("Extracting %s/%s (LZMA compressed)\n", dir_name, file_name);
            copy_lzmad(fp, fp_new, entries[i].stored_size);
            break;
        }

        fclose(fp_new);
    }
}

static void usage()
{
    printf ("Usage:\n"
            "  hha list <file>      "
            "Lists the contents of <file>\n"
            "  hha extract <file>   "
            "Extracts all supported files from the archive into the\n"
            "                       "
            "current directory AND REMOVES THEM FROM THE ARCHIVE!\n");
    exit(0);
}

void parse_args(int argc, char *argv[])
{
    if (argc != 3) usage();

    if (strcmp(argv[1], "list") == 0 || strcmp(argv[1], "l") == 0)
    {
        arg_mode = LIST;
        arg_path = argv[2];
    }
    else
    if (strcmp(argv[1], "extract") == 0 || strcmp(argv[1], "x") == 0)
    {
        arg_mode = EXTRACT;
        arg_path = argv[2];
    }
    else
    {
        usage();
    }
}

int main(int argc, char *argv[])
{
    assert(sizeof(Header)     == 16);
    assert(sizeof(IndexEntry) == 24);

    parse_args(argc, argv);

    open_archive(arg_path);
    process_header();
    switch (arg_mode)
    {
    case LIST:
        list_entries();
        break;

    case EXTRACT:
        extract_entries();
        break;
    }
    fclose(fp);

    return 0;
}
