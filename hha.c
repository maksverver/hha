#include "common.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#ifdef WIN32
#define mkdir(path,mode) mkdir(path)
#endif

static enum { LIST, EXTRACT } arg_mode; /* Mode of operation */
static char *arg_path;                  /* Path to archive */

static FILE *fp;                /* File pointer */
static size_t file_size;        /* Size of file */

static char *strings;           /* String table */
static size_t strings_size;     /* Size of string table */

static IndexEntry *index;       /* Index entries */
static size_t index_size;       /* Number of index entries */

static IndexEntry *new_index;
static size_t new_index_size;

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

static void seekto(size_t pos)
{
    if (fseek(fp, (long)pos, SEEK_SET) == -1)
    {
        perror("Seek failed");
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

static void write_uint32(uint32_t arg)
{
    uint8_t bytes[4] = { arg&255, (arg>>8)&255, (arg>>16)&255, (arg>>24)&255 };

    if (fwrite(bytes, 4, 1, fp) != 1)
    {
        perror("Write failed");
        abort();
    }
}

static void open_archive(const char *path)
{
    long lpos;

    /* Open file */
    fp = fopen(path, arg_mode == LIST ? "rb" : "r+b");
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
    index_size   = read_uint32();
    assert( strings_size <= file_size - sizeof(Header) );
    assert( index_size   <= (file_size - strings_size - sizeof(Header)) /
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
    index = malloc(sizeof(IndexEntry)*index_size);
    if (index == NULL)
    {
        perror("Could not allocate memory for string table");
        abort();
    }
    if (fread(index, sizeof(IndexEntry), index_size, fp) != index_size)
    {
        perror("Could not read index");
        abort();
    }

    new_index = malloc(sizeof(IndexEntry)*index_size);
    new_index_size = 0;
    if (new_index == NULL)
    {
        perror("Could not allocate memory for string table");
        abort();
    }
    memset(new_index, 0, sizeof(IndexEntry)*index_size);
}

static const char *strat(size_t pos)
{
    assert(pos <= strings_size);
    return strings + pos;
}

static void copy_uncompressed(FILE *dest, size_t offset, size_t size)
{
    char buffer[4096];
    size_t chunk;

    seekto(offset);
    while (size > 0)
    {
        chunk = size > sizeof(buffer) ? sizeof(buffer) : size;
        if (fread(buffer, 1, chunk, fp) != chunk)
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

static void copy_deflated(FILE *dest, size_t offset, size_t size)
{
    z_stream zs;
    unsigned char buf_in[4096], buf_out[4096];
    size_t chunk;
    int res;

    memset(&zs, 0, sizeof(zs));
    res = inflateInit2(&zs, -15);
    assert(res == Z_OK);
    seekto(offset);
    while (size > 0)
    {
        chunk = size > sizeof(buf_in) ? sizeof(buf_in) : size;
        if (fread(buf_in, 1, chunk, fp) != chunk)
        {
            perror("Read failed");
            abort();
        }
        size -= chunk;

        zs.next_in  = buf_in;
        zs.avail_in = chunk;
        do {
            zs.next_out  = buf_out;
            zs.avail_out = sizeof(buf_out);
            res = inflate(&zs, Z_SYNC_FLUSH);
            if (res != Z_OK && res != Z_STREAM_END)
            {
                fprintf(stderr, "WARNING: inflate failed!\n");
                goto end;
            }
            chunk = sizeof(buf_out) - zs.avail_out;
            if (fwrite(buf_out, 1, chunk, dest) != chunk)
            {
                perror("Write failed");
                abort();
            }
        } while (zs.avail_out == 0 && zs.avail_in > 0);
    }
    if (res != Z_STREAM_END)
    {
        fprintf(stderr, "WARNING: inflate ended prematurely\n");
    }
end:
    inflateEnd(&zs);
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

    for (i = 0; i < index_size; ++i)
    {
        dir_name  = strat(index[i].dir_name);
        file_name = strat(index[i].file_name);

        assert(strlen(dir_name) + 1 + strlen(file_name) < sizeof(path));
        sprintf(path, "%s/%s", dir_name, file_name);
        printf( "%10ld  %10ld  %10ld  %10ld   %s\n",
                (long)index[i].compression, (long)index[i].offset,
                (long)index[i].size, (long)index[i].stored_size, path );
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

    for (i = 0; i < index_size; ++i)
    {
        dir_name  = strat(index[i].dir_name);
        file_name = strat(index[i].file_name);

        if (index[i].compression > 1)
        {
            printf("Skipping %s/%s (compression type %d unknown)\n",
                   dir_name, file_name, index[i].compression);
            new_index[new_index_size++] = index[i]; /* keep file in archive */
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
            new_index[new_index_size++] = index[i]; /* keep file in archive */
            continue;
        }

        switch (index[i].compression)
        {
        case 0:
            printf("Extracting %s/%s (uncompressed)\n", dir_name, file_name);
            copy_uncompressed(fp_new, index[i].offset, index[i].size);
            break;

        case 1:
            printf("Extracting %s/%s (deflated)\n", dir_name, file_name);
            copy_deflated(fp_new, index[i].offset, index[i].size);
            break;

        default:
            assert(0);
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

static void write_new_index()
{
    seekto(16 + strings_size);
    if (fwrite(new_index, sizeof(IndexEntry), new_index_size, fp)
        != new_index_size)
    {
        perror("Could not write new index to file");
        abort();
    }
    seekto(12);
    write_uint32((uint32_t)new_index_size);
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
        write_new_index();
        break;

    }
    fclose(fp);

    return 0;
}
