#ifndef ANALYZER_C_COMPILER
#define ANALYZER_C_COMPILER

#include "parser.h"

bool is_equal_type(Type *lhs, Type *rhs);
FuncBlock *analyzer(FuncBlock *funcblock);

#endif