#ifndef FILE_C_COMPILER
#define FILE_C_COMPILER

#ifdef SELF_HOST
#include "../test/compiler_header.h"
#else
#include <stdio.h>
#endif

char* openfile(char* filename);
char* file_read(FILE* fin);

#endif  // FILE_C_COMPILER