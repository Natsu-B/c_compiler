#ifndef BUILTIN_C_COMPILER
#define BUILTIN_C_COMPILER

#ifdef SELF_HOST
#include "../test/compiler_header.h"
#else
#include <stdbool.h>
#endif

#include "parser.h"
#include "tokenizer.h"

bool is_builtin_function(Node** node, Token* token, bool is_root);
char* parse_string_literal(char* str, size_t* len);

#endif