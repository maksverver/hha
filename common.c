#include "common.h"
#include <stdio.h>
#include <stdlib.h>

size_t copy_uncompressed(FILE *dst, FILE *src, size_t size_in)
{
    char buffer[4096];
    size_t chunk, size_out;

    size_out = 0;
    while (size_in > 0)
    {
        chunk = size_in > sizeof(buffer) ? sizeof(buffer) : size_in;
        if (fread(buffer, 1, chunk, src) != chunk)
        {
            perror("Read failed");
            abort();
        }
        if (fwrite(buffer, 1, chunk, dst) != chunk)
        {
            perror("Write failed");
            abort();
        }
        size_in  -= chunk;
        size_out += chunk;
    }

    return size_out;
}
