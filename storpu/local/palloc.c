#include <stdlib.h>
#include <string.h>
#include <stddef.h>

void* palloc(size_t size) { return malloc(size); }

void* palloc0(size_t size)
{
    void* ret = malloc(size);
    if (ret) memset(ret, 0, size);
    return ret;
}

void pfree(void* ptr) { free(ptr); }
