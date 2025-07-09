#ifndef GENERATOR_C_COMPILER
#define GENERATOR_C_COMPILER

#include "analyzer.h"
#include "parser.h"
#include "type.h"

void generator(FuncBlock *parsed, char *output_filename);

#endif  // GENERATOR_C_COMPILER