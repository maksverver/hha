#include "common.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <LzmaDec.h>
#include <LzmaEnc.h>

/* If nonzero, the LZMA header does not contain compressed size data.
   This provides compatibility with older versions of the HHA file format. */
char lzma_omit_uncompressed_size = 0;

static long long decode_int64(unsigned char buf[8])
{
    long long res;
    int i;

    res = 0;
    for (i = 0; i < 8; ++i) res |= (long long)buf[i] << 8*i;
    return res;
}

static void encode_int64(long long value, unsigned char buf[8])
{
    int i;

    for (i = 0; i < 8; ++i) buf[i] = (value >> 8*i)&0xff;
}

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
    chunk = fread(buf, 1, chunk, reader->fp);
    *size = chunk;
    reader->left -= chunk;

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
    size_t chunk, pos_in, avail_in, avail_out, size_out, max_out;
    unsigned char lzma_props[LZMA_PROPS_SIZE], uncompressed_size[8];
    CLzmaDec ld;
    ELzmaFinishMode finish_mode;
    ELzmaStatus status;
    int res;

    /* Read LZMA properties */
    if (size_in < LZMA_PROPS_SIZE ||
        fread(lzma_props, 1, LZMA_PROPS_SIZE, src) != LZMA_PROPS_SIZE)
    {
        perror("Could not read LZMA properties");
        abort();
    }
    size_in -= LZMA_PROPS_SIZE;
    size_out = 0;
    if (lzma_omit_uncompressed_size)
    {
        max_out = ~0;
    }
    else
    {
        if (size_in < 8 || fread(uncompressed_size, 8, 1, src) != 1)
        {
            perror("Could not read LZMA uncompressed size");
            abort();
        }
        size_in -= 8;
        max_out = decode_int64(uncompressed_size);
    }

    /* Allocate decompressor */
    LzmaDec_Construct(&ld);
    res = LzmaDec_Allocate(&ld, lzma_props, LZMA_PROPS_SIZE, &szalloc);
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
            avail_in = chunk - pos_in;
            if (sizeof(buf_out) < max_out - size_out)
            {
                avail_out   = sizeof(buf_out);
                finish_mode = LZMA_FINISH_ANY;
            }
            else
            {
                avail_out   = max_out - size_out;
                finish_mode = LZMA_FINISH_END; 
            }

            if (LzmaDec_DecodeToBuf( &ld, buf_out, &avail_out,
                buf_in + pos_in, &avail_in, finish_mode, &status ) != SZ_OK)
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
    Byte props_data[LZMA_PROPS_SIZE];
    SizeT props_size;
    unsigned char uncompressed_size[8];

    /* Select default encoder properties */
    LzmaEncProps_Init(&props);
    props.writeEndMark = 1;
    LzmaEncProps_Normalize(&props);

    /* Allocate compressor */
    leh = LzmaEnc_Create(&szalloc);
    assert(leh != NULL);
    res = LzmaEnc_SetProps(leh, &props);
    assert(res == SZ_OK);

    /* Initialize input/output streams */
    lfr.in.Read    = lzma_read;
    lfr.fp         = src;
    lfr.left       = size;
    lfw.out.Write  = lzma_write;
    lfw.fp         = dst;
    lfw.written    = 0;

    /* Write properties to file */
    props_size = LZMA_PROPS_SIZE;
    res = LzmaEnc_WriteProperties(leh, props_data, &props_size);
    assert(res == SZ_OK);
    if (fwrite(props_data, props_size, 1, dst) != 1)
    {
        perror("Write failed");
        abort();
    }
    lfw.written += props_size;

    if (!lzma_omit_uncompressed_size)
    {
        encode_int64(size, uncompressed_size);
        if (fwrite(uncompressed_size, 8, 1, dst) != 1)
        {
            perror("Write failed");
            abort();
        }
        lfw.written += 8;
    }

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
