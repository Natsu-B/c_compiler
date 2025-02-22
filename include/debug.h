#ifndef DEBUG_C_COMPILER
#define DEBUG_C_COMPILER
#include "tokenizer.h"
#include "parser.h"

void print_tokenize_result(Token* token, char** tokenkindlist);
void print_parse_result(Node **node, int i,char **nodekindlist);

#endif