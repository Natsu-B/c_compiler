#ifndef DEBUG_C_COMPILER
#define DEBUG_C_COMPILER
#include "common.h"
#include "parser.h"
#include "tokenizer.h"
#include "vector.h"

void print_token_str(Vector *vec);
void print_polish_notation();
void print_tokenize_result(Token *token);
void print_parse_result(FuncBlock *node);
void print_mermaid_result(FuncBlock *node, char *output_file_name);
void print_definition();
void print_type(Type *type);
void dump_ir(IRProgram *program, char *path);
void dump_ir_fp(IRProgram *program, FILE *fp);
void dump_cfg(IRProgram *program, FILE *fp);

#endif
