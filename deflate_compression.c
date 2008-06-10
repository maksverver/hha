#include "common.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

void copy_deflated(FILE *src, FILE *dest, size_t size)
{
    z_stream zs;
    unsigned char buf_in[4096], buf_out[4096];
    size_t chunk;
    int res;

    memset(&zs, 0, sizeof(zs));
    res = inflateInit2(&zs, -15);
    assert(res == Z_OK);
    while (size > 0)
    {
        chunk = size > sizeof(buf_in) ? sizeof(buf_in) : size;
        if (fread(buf_in, 1, chunk, src) != chunk)
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
            if (res == Z_BUF_ERROR) break;
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
        } while (zs.avail_out == 0);
    }
    if (res != Z_STREAM_END)
    {
        fprintf(stderr, "WARNING: inflate ended prematurely\n");
    }
end:
    inflateEnd(&zs);
}
