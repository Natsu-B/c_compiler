#ifndef VARIABLES_C_COMPILER
#define VARIABLES_C_COMPILER

#include "parser.h"

void init_variables();
NestedBlockVariables* new_nest();
void exit_nest();
Var* add_variables(Token* token, TypeKind kind, size_t pointer_counter);
char* add_string_literal(Token* token);

#endif