#include "common.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <LzmaDec.h>
#include <LzmaEnc.h>

struct LzmaFileReader
{
    ISeqInStream in;
    FILE *fp;
    size_t left;
};

struct LzmaFileWriter
{
    ISeqOutStream out;
    FILE *fp;
    size_t written;
};

static SRes lzma_read(void *p, void *buf, size_t *size)
{
    struct LzmaFileReader *reader = (struct LzmaFileReader *)p;
    size_t chunk;

    if (ferror(reader->fp)) return SZ_ERROR_READ;
    chunk = *size < reader->left ? *size : reader->left;
    *size = fread(buf, 1, chunk, reader->fp);
    reader->left -= *size;

    return SZ_OK;
}

static size_t lzma_write(void *p, const void *buf, size_t size)
{
    struct LzmaFileWriter *w = (struct LzmaFileWriter *)p;
    size_t chunk;

    chunk = fwrite(buf, 1, size, w->fp);
    w->written += chunk;

    return (SRes)chunk;
}

static void *lzma_alloc(void *p, size_t size)
{
    void *res;

    (void)p;
    res = malloc(size);
    assert(res != NULL);

    return res;
}

static void lzma_free(void *p, void *addr)
{
    (void)p;
    free(addr);
}

static ISzAlloc szalloc = { lzma_alloc, lzma_free };


size_t copy_lzmad(FILE *dst, FILE *src, size_t size_in)
{
    unsigned char buf_in[4096], buf_out[4096];
    size_t chunk, pos_in, avail_in, avail_out, size_out;
    unsigned char header[LZMA_PROPS_SIZE + 8];
    CLzmaDec ld;
    ELzmaStatus status;
    int res;

    /* Initialize header (set unknown size; read other properties from file) */
    memset(header, 0xff, sizeof(header));
    if (fread(header, 1, LZMA_PROPS_SIZE, src) != LZMA_PROPS_SIZE)
    {
        perror("Could not read LZMA properties");
        abort();
    }
    size_in -= LZMA_PROPS_SIZE;
    size_out = 0;

    /* Allocate decompressor */
    LzmaDec_Construct(&ld);
    res = LzmaDec_Allocate(&ld, header, LZMA_PROPS_SIZE, &szalloc);
    assert(res == SZ_OK);

    LzmaDec_Init(&ld);
    do {
        /* Read more input data */
        pos_in = 0;
        chunk = size_in > sizeof(buf_in) ? sizeof(buf_in) : size_in;
        if (fread(buf_in, 1, chunk, src) != chunk)
        {
            perror("Read failed");
            abort();
        }
        size_in -= chunk;

        do {
            /* Decode available input */
            avail_in  = chunk - pos_in;
            avail_out = sizeof(buf_out);
            if (LzmaDec_DecodeToBuf( &ld, buf_out, &avail_out,
                buf_in + pos_in, &avail_in, LZMA_FINISH_ANY, &status ) != SZ_OK)
            {
                fprintf(stderr, "WARNING: LZMA decompression failed!\n");
                goto end;
            }
            pos_in += avail_in;

            /* Write output */
            if (fwrite(buf_out, 1, avail_out, dst) != avail_out)
            {
                perror("Write failed");
                abort();
            }
            size_out += avail_out;

        } while (status == LZMA_STATUS_NOT_FINISHED);

        /* Check for end-of-data */
        if (size_in == 0 && status == LZMA_STATUS_NEEDS_MORE_INPUT)
        {
            fprintf(stderr, "WARNING: premature end of LZMA input data\n");
            goto end;
        }

    } while (status == LZMA_STATUS_NEEDS_MORE_INPUT);

end:
    LzmaDec_Free(&ld, &szalloc);

    return size_out;
}

size_t copy_lzmac(FILE *dst, FILE *src, size_t size)
{
    struct LzmaFileReader lfr;
    struct LzmaFileWriter lfw;
    CLzmaEncProps props;
    CLzmaEncHandle leh;
    int res;

    /* Initialize input/output streams */
    lfr.in.Read    = lzma_read;
    lfr.fp         = src;
    lfr.left       = size;
    lfw.out.Write  = lzma_write;
    lfw.fp         = dst;
    lfw.written    = 0;

    /* Select default encoder properties */
    LzmaEncProps_Init(&props);
    props.level = 7;
    LzmaEncProps_Normalize(&props);

    /* Allocate ccompressor */
    leh = LzmaEnc_Create(&szalloc);
    assert(leh != NULL);
    res = LzmaEnc_SetProps(leh, &props);
    assert(res == SZ_OK);

    /* Compress */
    res = LzmaEnc_Encode(leh, &lfw.out, &lfr.in, NULL, &szalloc, &szalloc);
    if (res != SZ_OK || lfr.left != 0)
    {
        perror("LZMA compression failed");
        abort();
    }

    LzmaEnc_Destroy(leh, &szalloc, &szalloc);

    return lfw.written;
}
