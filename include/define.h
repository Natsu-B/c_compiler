#ifndef DEFINE_C_COMPILER
#define DEFINE_C_COMPILER

#include "tokenizer.h"
#include "vector.h"

extern Vector *object_like_macro_list;
extern Vector *function_like_macro_list;

typedef struct object_like_macro_storage object_like_macro_storage;

struct object_like_macro_storage
{
  Token *identifier;
  Vector *token_string;
};

typedef struct function_like_macro_storage function_like_macro_storage;

struct function_like_macro_storage
{
  Token *identifier;
  Vector *arguments;
  Vector *token_string;
};

int find_macro_name_all(Token *identifier);
void add_object_like_macro(Vector *token);
void add_function_like_macro(Vector *token);
bool ident_replacement(Token *token);
void undef_macro(Token *token);

#endif