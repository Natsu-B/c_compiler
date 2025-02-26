#include <stdlib.h>
#include <ctype.h>

void alloc(__uint64_t **p, __uint64_t x, __uint64_t y)
{
    *p = malloc(2 * sizeof(__uint64_t));
    (*p)[0] = x;
    (*p)[1] = y;
}