#ifndef PREPROCESSOR_C_COMPILER
#define PREPROCESSOR_C_COMPILER

#include "vector.h"
#include "tokenizer.h"

extern Vector *Conditional_Inclusion_List;
void token_void(Token *token);
Token *preprocess(char *input, char *file_name, Token *token);
void preprocessed_file_writer(Token *token, char *output_filename);
void init_preprocessor();

#endif