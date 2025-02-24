#include <ctype.h>

__uint64_t func(__uint64_t x);

__uint64_t fibonacci(__uint64_t a, __uint64_t b)
{
    return a + b;
}

int main()
{
    int x = 0;
    int y = 1;
    int i = 0;
    while (i != 10)
    {
        i++;
        x = fibonacci(x, y);
        if (x != func(x))
            return 1;
        y = fibonacci(x, y);
        if (y != func(y))
            return 1;
    }
    return y;
}