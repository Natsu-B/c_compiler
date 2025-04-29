// ------------------------------------------------------------------------------------
// type, enum, union and struct manager
// ------------------------------------------------------------------------------------

#include "include/type.h"

#include <stdio.h>
#include <string.h>

#include "include/error.h"
#include "include/parser.h"
#include "include/tokenizer.h"
#include "include/vector.h"

static Vector* TypedefList;
static Vector* StructList;

typedef struct
{
  Token* name;
  Type* type;
} typedef_list;

Type* declaration_specifiers()
{
  size_t long_count = 0;
  size_t signed_count = 0;
  size_t unsigned_count = 0;
  size_t int_count = 0;
  size_t bool_count = 0;
  size_t char_count = 0;
  size_t short_count = 0;
  size_t void_count = 0;
  Token* old = get_token();
  bool is_typedef = false;
  if (consume("typedef", TK_IDENT))
    is_typedef = true;

  for (;;)
  {
    if (consume("long", TK_IDENT))
      long_count++;
    else if (consume("signed", TK_IDENT))
      signed_count++;
    else if (consume("unsigned", TK_IDENT))
      unsigned_count++;
    else if (consume("int", TK_IDENT))
      int_count++;
    else if (consume("bool", TK_IDENT))
      bool_count++;
    else if (consume("char", TK_IDENT))
      char_count++;
    else if (consume("short", TK_IDENT))
      short_count++;
    else if (consume("void", TK_IDENT))
      void_count++;
    else
      break;
  }
  if (long_count > 2 ||
      (long_count & (bool_count + char_count + short_count + void_count)) ||
      signed_count + unsigned_count > 1 ||
      int_count + bool_count + char_count + short_count + void_count > 1 ||
      (signed_count | unsigned_count) & (void_count | bool_count) ||
      !(long_count | signed_count | unsigned_count | int_count | bool_count |
        short_count | char_count | void_count) &
          is_typedef)
    error_at(old->str, get_token()->str - old->str + get_token()->len,
             "invalid type specifier");

  if (!(long_count | signed_count | unsigned_count | int_count | bool_count |
        short_count | char_count | void_count))
  {
    // typedefにあるか調べる
    Token* token = consume_ident();
    if (token)
    {
      for (size_t i = 1; i <= vector_size(TypedefList); i++)
      {
        Vector* typedef_nest = vector_peek_at(TypedefList, i);
        for (size_t j = 1; j <= vector_size(typedef_nest); j++)
        {
          typedef_list* tmp = vector_peek_at(typedef_nest, j);
          if (tmp->name->len == token->len &&
              !strncmp(tmp->name->str, token->str, token->len))
            return tmp->type;
        }
      }
      set_token(token);
    }
    return NULL;
  }

  size_t ref_count = 0;
  for (;;)
  {
    if (consume("*", TK_RESERVED))
      ref_count++;
    else
      break;
  }

  TypeKind kind;
  if (long_count == 2)
    kind = TYPE_LONGLONG;
  else if (long_count)
    kind = TYPE_LONG;
  else if (bool_count)
    kind = TYPE_BOOL;
  else if (char_count)
    kind = TYPE_CHAR;
  else if (void_count)
    kind = TYPE_VOID;
  else if (short_count)
    kind = TYPE_SHORT;
  else if (int_count | unsigned_count | signed_count)
    kind = TYPE_INT;
  else
    unreachable();
  Type* type = calloc(1, sizeof(Type));
  type->type = kind;
  if (!unsigned_count)
    type->is_signed = true;
  while (ref_count--)
  {
    Type* new = alloc_type(TYPE_PTR);
    new->ptr_to = type;
    type = new;
  }

  if (is_typedef)
  {
    Type* new = alloc_type(TYPE_TYPEDEF);
    new->ptr_to = type;
    type = new;
  }
  return type;
}

void add_typedef(Token* token, Type* type)
{
  // TYPE_ARRRAYの場合 TYPE_ARRAYがTYPE_DEFより上にくるのを修正
  Type* tmp = type;
  Type* old = NULL;
  while (tmp->type == TYPE_ARRAY)
  {
    old = tmp;
    tmp = tmp->ptr_to;
  }
  if (tmp->type != TYPE_TYPEDEF)
    unreachable();
  tmp = tmp->ptr_to;
  if (old)
    old->ptr_to = tmp;
  else
    type = tmp;

  // 名前とTypeを関連付ける
  typedef_list* new = malloc(sizeof(typedef_list));
  new->name = token;
  new->type = type;
  vector_push(vector_peek(TypedefList), new);
}

// Typeを作成する関数
Type* alloc_type(TypeKind kind)
{
  Type* new = calloc(1, sizeof(Type));
  new->type = kind;
  return new;
}

// TYPE_INT TYPE_ARRAY 等を受け取ってその大きさを返す関数
size_t size_of(Type* type)
{
  switch (type->type)
  {
    case TYPE_INT: return 4;
    case TYPE_BOOL:
    case TYPE_CHAR: return 1;
    case TYPE_SHORT: return 2;
    case TYPE_STR:
    case TYPE_LONG:
    case TYPE_LONGLONG:
    case TYPE_PTR:
    case TYPE_ARRAY: return 8;
    case TYPE_VOID: return 0;
    case TYPE_STRUCT:
    {
    }
    default: break;
  }
  unreachable();
  return 0;
}

size_t align_of(Type* type)
{
  switch (type->type)
  {
    case TYPE_STR:
    case TYPE_ARRAY: return 8;
    case TYPE_STRUCT: unimplemented(); break;
    default: return size_of(type);
  }
  unreachable();
  return 0;
}

void init_types()
{
  Vector* root_typedef = vector_new();
  TypedefList = vector_new();
  vector_push(TypedefList, root_typedef);
  StructList = vector_new();
}

void new_nest_type()
{
  Vector* new = vector_new();
  vector_push(TypedefList, new);
}

void exit_nest_type()
{
  vector_pop(TypedefList);
}