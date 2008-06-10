#include "common.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

size_t copy_deflated(FILE *dst, FILE *src, size_t size_in)
{
    z_stream zs;
    unsigned char buf_in[4096], buf_out[4096];
    size_t chunk, size_out;
    int res;

    size_out = 0;
    memset(&zs, 0, sizeof(zs));
    res = inflateInit2(&zs, -15);
    assert(res == Z_OK);
    while (size_in > 0)
    {
        chunk = size_in > sizeof(buf_in) ? sizeof(buf_in) : size_in;
        if (fread(buf_in, 1, chunk, src) != chunk)
        {
            perror("Read failed");
            abort();
        }
        size_in -= chunk;

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
            if (fwrite(buf_out, 1, chunk, dst) != chunk)
            {
                perror("Write failed");
                abort();
            }
            size_out += chunk;
        } while (zs.avail_out == 0);
    }
    if (res != Z_STREAM_END)
    {
        fprintf(stderr, "WARNING: inflate ended prematurely\n");
    }
end:
    inflateEnd(&zs);
    return size_out;
}
