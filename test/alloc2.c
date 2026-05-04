#include <stdlib.h>

void alloc(int **p, int x, int y)
{
    *p = malloc(2 * sizeof(int));
    (*p)[0] = x;
    (*p)[1] = y;
}