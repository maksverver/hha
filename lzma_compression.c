#include "common.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <LzmaDec.h>

static void *LzmaAlloc(void *p, size_t size)
{
    void *res;

    (void)p;
    res = malloc(size);
    assert(res != NULL);

    return res;
}

static void LzmaFree(void *p, void *addr)
{
    (void)p;
    free(addr);
}

void copy_lzmad(FILE *src, FILE *dest, size_t size)
{
    unsigned char buf_in[4096], buf_out[4096];
    size_t chunk, pos_in, avail_in, avail_out;
    unsigned char header[LZMA_PROPS_SIZE + 8];
    ISzAlloc alloc = { LzmaAlloc, LzmaFree };
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
    size -= LZMA_PROPS_SIZE;

    /* Allocate decompressor */
    LzmaDec_Construct(&ld);
    res = LzmaDec_Allocate(&ld, header, LZMA_PROPS_SIZE, &alloc);
    assert(res == SZ_OK);

    LzmaDec_Init(&ld);
    do {
        /* Read more input data */
        pos_in = 0;
        chunk = size > sizeof(buf_in) ? sizeof(buf_in) : size;
        if (fread(buf_in, 1, chunk, src) != chunk)
        {
            perror("Read failed");
            abort();
        }
        size -= chunk;

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
            if (fwrite(buf_out, 1, avail_out, dest) != avail_out)
            {
                perror("Write failed");
                abort();
            }
        } while (status == LZMA_STATUS_NOT_FINISHED);

        /* Check for end-of-data */
        if (size == 0 && status == LZMA_STATUS_NEEDS_MORE_INPUT)
        {
            fprintf(stderr, "WARNING: premature end of LZMA input data\n");
            goto end;
        }

    } while (status == LZMA_STATUS_NEEDS_MORE_INPUT);

end:
    LzmaDec_Free(&ld, &alloc);
}
