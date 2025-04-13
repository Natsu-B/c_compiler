#ifndef FILE_C_COMPILER
#define FILE_C_COMPILER

#include <stdio.h>

extern char* openfile(char* filename);
extern char* file_read(FILE* fin);

#endif  // FILE_C_COMPILER