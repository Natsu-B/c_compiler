#ifndef DEBUG_C_COMPILER
#define DEBUG_C_COMPILER
#include "tokenizer.h"
#include "parser.h"

void print_tokenize_result(Token* token);
void print_parse_result(FuncBlock *node);

#endif