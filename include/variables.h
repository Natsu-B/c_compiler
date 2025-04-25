#ifndef VARIABLES_C_COMPILER
#define VARIABLES_C_COMPILER

#include "parser.h"
#include "type.h"

void init_variables();
NestedBlockVariables *new_nest_variables();
void exit_nest_variables();
Var *add_variables(Token *token, Type *type);
char *add_string_literal(Token *token);
Var *get_global_var();

#endif