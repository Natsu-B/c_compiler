#ifndef DEBUG_C_COMPILER
#define DEBUG_C_COMPILER
#include "parser.h"
#include "tokenizer.h"
#include "vector.h"

void print_token_str(Vector* vec);
void print_polish_notation();
void print_tokenize_result(Token* token);
void print_parse_result(FuncBlock* node);
void print_definition();
void print_type(Type *type);

#endif