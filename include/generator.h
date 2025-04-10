#ifndef GENERATOR_C_COMPILER
#define GENERATOR_C_COMPILER

#include "parser.h"
#include "analyzer.h"

extern size_t size_of(TypeKind kind);
extern void generator(FuncBlock *parsed, char *output_filename);

#endif // GENERATOR_C_COMPILER