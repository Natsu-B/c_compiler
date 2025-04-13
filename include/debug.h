#ifndef DEBUG_C_COMPILER
#define DEBUG_C_COMPILER
#include "parser.h"
#include "tokenizer.h"

void print_tokenize_result(Token* token);
void print_parse_result(FuncBlock* node);
void print_definition();

#endif