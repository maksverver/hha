#include "common.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef WIN32
#define mkdir(path,mode) mkdir(path)
#endif

enum Mode { LIST, EXTRACT, CREATE };

static enum Mode arg_mode;              /* Mode of operation */
static char *arg_archive;               /* Path to archive */
static char **arg_files_begin,          /* List of filesto process */
            **arg_files_end;
static Compression arg_com;             /* Compression to use */

static FILE *fp;                /* File pointer */
static size_t file_size;        /* Size of file */

static char *strings;           /* String table */
static size_t strings_size;     /* Size of string table */

static IndexEntry *entries;     /* Index entries */
static size_t entries_size;     /* Number of index entries */

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

static void close_archive()
{
    fclose(fp);
}

static void process_header()
{
    if (read_uint32() != 0xac2ff34ful)
    {
        fprintf(stderr, "The specified file does not seem to be a "
                        "Hothead Archive file.\n");
        exit(1);
    }
    (void)read_uint32();
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

static void list_entries()
{
    size_t i;
    char path[1024];
    const char *dir_name, *file_name;

    printf( "Com   Offset       Size     Stored Size "
            " File path                    \n" );

    printf( "--- ----------- ----------- ----------- "
            "---------------------------------------\n" );

    for (i = 0; i < entries_size; ++i)
    {
        dir_name  = strat(entries[i].dir_name);
        file_name = strat(entries[i].file_name);

        assert(strlen(dir_name) + 1 + strlen(file_name) < sizeof(path));
        sprintf(path, "%s/%s", dir_name, file_name);
        printf( " %ld  %10ld  %10ld  %10ld   %s\n",
                (long)entries[i].compression, (long)entries[i].offset,
                (long)entries[i].size, (long)entries[i].stored_size, path );
    }

    printf( "--- ----------- ----------- ----------- "
            "---------------------------------------\n" );
}

static void extract_entries()
{
    size_t i;
    char path[1024];
    const char *dir_name, *file_name;
    FILE *fp_new;
    size_t size_new;

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
        strncat(path, "/", sizeof(path) - 1);
        strncat(path, file_name, sizeof(path) - 1);

        fp_new = fopen(path, "wb");
        if (fp_new == NULL)
        {
            perror("Could not open file");
            continue;
        }

        if (fseek(fp, (long)entries[i].offset, SEEK_SET) == -1)
        {
            perror("Seek failed");
            abort();
        }

        switch (entries[i].compression)
        {
        case COM_NONE:
            printf("Extracting %s/%s (uncompressed)\n", dir_name, file_name);
            size_new = copy_uncompressed(fp_new, fp, entries[i].stored_size);
            break;

        case COM_DEFLATE:
            printf("Extracting %s/%s (deflated)\n", dir_name, file_name);
            size_new = copy_deflated(fp_new, fp, entries[i].stored_size);
            break;

        case COM_LZMA:
            printf("Extracting %s/%s (LZMA compressed)\n", dir_name, file_name);
            size_new = copy_lzmad(fp_new, fp, entries[i].stored_size);
            break;

        default:
            size_new = 0;
        }

        fclose(fp_new);

        if (size_new != entries[i].size)
        {
            fprintf(stderr, "WARNING: extracted size (%ld bytes) differs from "
                            "recorded size (%ld bytes)\n",
                            size_new, (long)entries[i].size);
        }
    }
}

static void usage()
{
    printf ("Hothead Archive tool v0.3\n"
"\n"
"Usage:\n"
"\n"
"  hha list <file>                    -- List the contents of <file>.\n"
"  hha t <file>\n"
"\n"
"  hha extract <file>                 -- Extract all files from the archive\n"
"  hha x <file>                          into the current working directory.\n"
"\n"
"  hha create [opts] <file> <dir>+    -- Pack the specified directories into a\n"
"  hha c [opts] <file> <dir>+            new archive.\n"
"\n"
"  Compression options:\n"
"    -0  No compression\n"
"    -1  Deflate compression\n"
"    -2  LZMA compression (default)\n"
"  Note that these values specify maximum compression; a lower value may be\n"
"  selected if it yields an equal or smaller size.\n");

    exit(0);
}

static void parse_args(int argc, char *argv[])
{
    struct stat st;

    if (argc < 3) usage();

    if (strcmp(argv[1], "list") == 0 || strcmp(argv[1], "t") == 0)
    {
        if (argc > 3) usage();
        arg_mode    = LIST;
        arg_archive = argv[2];
    }
    else
    if (strcmp(argv[1], "extract") == 0 || strcmp(argv[1], "x") == 0)
    {
        if (argc > 3) usage();
        arg_mode    = EXTRACT;
        arg_archive = argv[2];
    }
    else
    if (strcmp(argv[1], "create") == 0 || strcmp(argv[1], "c") == 0)
    {
        char **p;

        if (argc < 4) usage();

        arg_mode    = CREATE;
        arg_com     = COM_LZMA;

        if (argv[2][0] == '-')
        {
            /* Parse options */
            while (*++argv[2] != '\0')
            {
                switch (*argv[2])
                {
                case '0': arg_com = COM_NONE;    break;
                case '1': arg_com = COM_DEFLATE; break;
                case '2': arg_com = COM_LZMA;    break;
                default:  usage();
                }
            }
            arg_archive = argv[3];
            arg_files_begin = &argv[4];
            arg_files_end   = &argv[argc];
        }
        else
        {
            arg_archive = argv[2];
            arg_files_begin = &argv[3];
            arg_files_end   = &argv[argc];
        }

        if (arg_files_begin == arg_files_end) usage();

        /* Ensure that arguments are indeed directories */
        for (p = arg_files_begin; p != arg_files_end; ++p)
        {
            if (stat(*p, &st) != 0)
            {
                perror(*p);
                exit(1);
            }
            if (!S_ISDIR(st.st_mode))
            {
                fprintf(stderr, "%s: not a directory.\n", *p);
                exit(1);
            }
        }

    }
    else
    {
        usage();
    }

    /* Verify that archive exists */
    if (arg_mode == LIST || arg_mode == EXTRACT)
    {
        if (stat(arg_archive, &st) != 0)
        {
            perror(arg_archive);
            exit(1);
        }
        if (!S_ISREG(st.st_mode))
        {
            fprintf(stderr, "%s: not a regular file.\n", arg_archive);
            exit(1);
        }
    }
}

int main(int argc, char *argv[])
{
    assert(sizeof(Header)     == 16);
    assert(sizeof(IndexEntry) == 24);

    parse_args(argc, argv);

    switch (arg_mode)
    {
    case LIST:
        open_archive(arg_archive);
        process_header();
        list_entries();
        close_archive();
        break;

    case EXTRACT:
        open_archive(arg_archive);
        process_header();
        extract_entries();
        close_archive();
        break;

    case CREATE:
        create_archive(arg_archive, (const char**)arg_files_begin,
                                    (const char**)arg_files_end, arg_com);
        break;
    }

    return 0;
}
