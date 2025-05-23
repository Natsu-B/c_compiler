#ifndef PREPROCESSOR_C_COMPILER
#define PREPROCESSOR_C_COMPILER

#include "tokenizer.h"
#include "vector.h"

extern bool gcc_compatible;
extern Vector *Conditional_Inclusion_List;
void line_count();
Token *token_next_not_ignorable(Token *token);
Token *token_next_not_ignorable_void(Token *token);

void token_void(Token *token);
Token *preprocess(char *input, char *end, char *file_name, Token *token);
void preprocessed_file_writer(Token *token, char *output_filename);
void init_preprocessor();

#endif