// ------------------------------------------------------------------------------------
// type, enum, union and struct manager
// ------------------------------------------------------------------------------------

#include "include/type.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#include <string.h>
#endif

#include "include/analyzer.h"
#include "include/error.h"
#include "include/eval_constant.h"
#include "include/parser.h"
#include "include/tokenizer.h"
#include "include/vector.h"

static Vector* OrdinaryNamespaceList;  // 変数名 関数名 列挙体のメンバ名
static Vector* TagNamespaceList;       // 構造体 共用体 列挙体のタグ名
static size_t tag_id;                  // 構造体 共用体 列挙体につけられる番号
static Vector* EnumStructList;  // 構造体、共用体、列挙体のリスト tag_idの順番

typedef struct
{
  enum
  {
    typedef_name,
    enum_member,
    function_definition_name,
  } ordinary_kind;
  Token* name;
  Type* type;
  size_t enum_number;
} ordinary_data_list;

typedef struct
{
  Token* name;
  Type* type;
  // structの先頭から何byte離れているか enumだったらその数字
  size_t offset;
} tag_data_list;

typedef struct
{
  enum
  {
    is_struct,
    is_union,
    is_enum,
  } tagkind;                // unionかstructかenum
  Token* name;              // 名前 なかったらNULL
  Vector* data_list;        // structの中身 前方宣言のみならNULL
  Type* type;               // struct union enumのtype
  size_t struct_size;       // structのサイズ
  size_t struct_alignment;  // structのアライメント
} tag_list;

bool is_type_specifier(Token* token)
{
  if (peek("void", TK_IDENT) || peek("char", TK_IDENT) ||
      peek("short", TK_IDENT) || peek("int", TK_IDENT) ||
      peek("long", TK_IDENT) || peek("signed", TK_IDENT) ||
      peek("unsigned", TK_IDENT) || peek("bool", TK_IDENT) ||
      peek("_Bool", TK_IDENT) || peek("const", TK_IDENT) ||
      peek("struct", TK_IDENT) || peek("union", TK_IDENT) ||
      peek("enum", TK_IDENT))
    return true;

  // typedefにないか調べる
  for (size_t i = 1; i <= vector_size(OrdinaryNamespaceList); i++)
  {
    Vector* namespace_list = vector_peek_at(OrdinaryNamespaceList, i);
    for (size_t j = 1; j <= vector_size(namespace_list); j++)
    {
      ordinary_data_list* name = vector_peek_at(namespace_list, j);
      if (name->ordinary_kind == typedef_name && name->name &&
          name->name->len == token->len &&
          !strncmp(name->name->str, token->str, token->len))
        return true;
    }
  }
  return false;
}

Type* _declaration_specifiers(bool* is_typedef)
{
  size_t long_count = 0;
  size_t signed_count = 0;
  size_t unsigned_count = 0;
  size_t int_count = 0;
  size_t bool_count = 0;
  size_t char_count = 0;
  size_t short_count = 0;
  size_t void_count = 0;
  size_t struct_count = 0;
  size_t union_count = 0;
  size_t enum_count = 0;
  size_t const_count = 0;  // 読み飛ばす
  Token* old = get_token();

  for (;;)
  {
    if (consume("typedef", TK_IDENT))
      *is_typedef = true;
    if (consume("long", TK_IDENT))
      long_count++;
    else if (consume("signed", TK_IDENT))
      signed_count++;
    else if (consume("unsigned", TK_IDENT))
      unsigned_count++;
    else if (consume("int", TK_IDENT))
      int_count++;
    else if (consume("bool", TK_IDENT) || consume("_Bool", TK_IDENT))
      bool_count++;
    else if (consume("char", TK_IDENT))
      char_count++;
    else if (consume("short", TK_IDENT))
      short_count++;
    else if (consume("void", TK_IDENT))
      void_count++;
    else if (consume("struct", TK_IDENT))
      struct_count++;
    else if (consume("union", TK_IDENT))
      union_count++;
    else if (consume("enum", TK_IDENT))
      enum_count++;
    else if (consume("const", TK_IDENT))
      const_count++;
    else
      break;
  }
  if (long_count > 2 ||
      (long_count & (bool_count + char_count + short_count + void_count)) ||
      signed_count + unsigned_count > 1 ||
      int_count + bool_count + char_count + short_count + void_count +
              struct_count + union_count >
          1 ||
      (signed_count | unsigned_count) & (void_count | bool_count) ||
      (long_count | signed_count | unsigned_count | int_count | bool_count |
       short_count | char_count | void_count) &
          (struct_count | union_count | enum_count))
    error_at(old->str, get_token()->str - old->str + get_token()->len,
             "invalid type specifier");

  if (struct_count | union_count)
  {  // struct または union
    Token* token = consume_ident();
    tag_list* new = NULL;
    if (token)
    {
      for (size_t i = 1; i <= vector_size(TagNamespaceList); i++)
      {
        Vector* tag_nest = vector_peek_at(TagNamespaceList, i);
        for (size_t j = 1; j <= vector_size(tag_nest); j++)
        {
          tag_list* tmp = vector_peek_at(tag_nest, j);
          if (tmp->name && tmp->name->len == token->len &&
              !strncmp(tmp->name->str, token->str, token->len))
          {
            if (tmp->tagkind == is_enum ||
                (tmp->tagkind == is_struct && union_count) ||
                (tmp->tagkind == is_union && struct_count))
              error_at(token->str, token->len, "invalid type");
            new = tmp;
          }
        }
      }
    }
    bool is_definition = consume("{", TK_RESERVED);
    if (is_definition && new && new->data_list)
      error_at(get_old_token()->str, get_old_token()->len,
               "multiple struct/union definition");
    // struct {} のような無名structの場合や未定義の場合
    if (!new)
    {
      new = calloc(1, sizeof(tag_list));
      new->tagkind = struct_count ? is_struct : is_union;
      new->name = token;
      Type* type = alloc_type(TYPE_STRUCT);
      type->type_num = ++tag_id;
      new->type = type;
      vector_push(EnumStructList, new);
    }
    if (is_definition)
    {
      new->data_list = vector_new();
      size_t struct_size = 0;
      size_t struct_alignment = 0;
      for (;;)
      {
        Type* type = declaration_specifiers();
        Token* token = consume_ident();
        if (!type || type->type == TYPE_TYPEDEF || !token)
          error_at(get_token()->str, get_token()->len,
                   "invalid struct definition");
        tag_data_list* new_data = malloc(sizeof(tag_data_list));
        new_data->name = token;
        new_data->type = type;
        vector_push(new->data_list, new_data);
        size_t alignment = align_of(type);
        if (new->tagkind == is_struct)
          struct_size = (struct_size % alignment
                             ? (struct_size / alignment + 1) * alignment
                             : struct_size) +
                        size_of(type);
        else if (struct_size < size_of(type))
          struct_size = size_of(type);
        if (struct_alignment < alignment)
          struct_alignment = alignment;
        expect(";", TK_RESERVED);
        if (consume("}", TK_RESERVED))
          break;
      }
      struct_size = (struct_size + struct_alignment - 1) / struct_alignment *
                    struct_alignment;
      new->struct_size = struct_size;
      new->struct_alignment = struct_alignment;
    }
    vector_push(vector_peek(TagNamespaceList), new);
    if (consume("typedef", TK_IDENT))
      *is_typedef = true;
    return new->type;
  }

  if (enum_count)
  {
    Token* enum_name = consume_ident();
    tag_list* new = NULL;
    // 以前enum列に追加されてないかを調べる 名前空間はstructと一緒
    if (enum_name)
    {
      for (size_t i = 1; i <= vector_size(TagNamespaceList); i++)
      {
        Vector* tag_nest = vector_peek_at(TagNamespaceList, i);
        for (size_t j = 1; j <= vector_size(tag_nest); j++)
        {
          tag_list* tmp = vector_peek_at(tag_nest, j);
          if (tmp->name && tmp->name->len == enum_name->len &&
              !strncmp(tmp->name->str, enum_name->str, enum_name->len))
          {
            if (tmp->tagkind == is_struct || tmp->tagkind == is_union)
              error_at(enum_name->str, enum_name->len, "invalid type");
            new = tmp;
          }
        }
      }
    }
    bool is_definition = consume("{", TK_RESERVED);
    if (is_definition && new && new->data_list)
      error_at(get_old_token()->str, get_old_token()->len,
               "multiple enum definition");
    if (!new)
    {
      new = calloc(1, sizeof(tag_list));
      new->tagkind = is_enum;
      new->name = enum_name;
      new->type = alloc_type(TYPE_ENUM);
      new->type->type_num = ++tag_id;
      vector_push(EnumStructList, new);
    }
    if (is_definition)
    {
      new->data_list = vector_new();
      size_t enum_num = 0;
      for (;;)
      {
        bool is_comma = false;
        Token* identifier = consume_ident();
        if (!identifier)
          error_at(get_token()->str, get_token()->len,
                   "invalid enum definition");
        tag_data_list* child = calloc(1, sizeof(tag_data_list));
        child->name = identifier;
        child->type = alloc_type(TYPE_INT);
        ordinary_data_list* new_enum_member =
            calloc(1, sizeof(ordinary_data_list));
        if (consume("=", TK_RESERVED))
        {
          long num = eval_constant_expression();
          if (num < 0 || (size_t)num < enum_num)
            error_at(get_old_token()->str, get_old_token()->len,
                     "invalid enum value");
          enum_num = num;
        }
        child->offset = enum_num;
        vector_push(new->data_list, child);
        new_enum_member->ordinary_kind = enum_member;
        new_enum_member->enum_number = enum_num;
        new_enum_member->name = identifier;
        new_enum_member->type = alloc_type(TYPE_INT);
        vector_push(vector_peek(OrdinaryNamespaceList), new_enum_member);
        enum_num++;
        if (consume(",", TK_RESERVED))
          is_comma = true;
        if (consume("}", TK_RESERVED))
          break;
        else if (is_comma)
          continue;
        error_at(get_token()->str, get_token()->len, "invalid enum definition");
      }
    }
    vector_push(vector_peek(TagNamespaceList), new);
    if (consume("typedef", TK_IDENT))
      *is_typedef = true;
    return new->type;
  }

  if (!(long_count | signed_count | unsigned_count | int_count | bool_count |
        short_count | char_count | void_count))
  {
    // typedefにあるか調べる
    Token* token = peek_ident();
    if (token)
    {
      for (size_t i = vector_size(OrdinaryNamespaceList); i >= 1; i--)
      {
        Vector* typedef_nest = vector_peek_at(OrdinaryNamespaceList, i);
        for (size_t j = 1; j <= vector_size(typedef_nest); j++)
        {
          ordinary_data_list* tmp = vector_peek_at(typedef_nest, j);
          if (tmp->ordinary_kind == typedef_name && tmp->name &&
              tmp->name->len == token->len &&
              !strncmp(tmp->name->str, token->str, token->len))
          {
            consume_ident();
            if (consume("typedef", TK_IDENT))
              *is_typedef = true;
            return tmp->type;
          }
        }
      }
    }
    return NULL;
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
  Type* type = alloc_type(kind);
  if (!unsigned_count)
    type->is_signed = true;
  return type;
}

Type* declaration_specifiers()
{
  bool is_typedef = false;
  Type* type = _declaration_specifiers(&is_typedef);
  if (type)
  {
    size_t ref_count = 0;
    for (;;)
    {
      if (consume("*", TK_RESERVED))
        ref_count++;
      else
        break;
    }
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
  ordinary_data_list* new = malloc(sizeof(ordinary_data_list));
  new->ordinary_kind = typedef_name;
  new->name = token;
  new->type = type;
  vector_push(vector_peek(OrdinaryNamespaceList), new);
}

bool add_function_name(Vector* function_list, Token* name)
{
  // 名前空間に以前から同じ名前で異なるものがないかを判別する
  for (size_t i = 1; i <= vector_size(OrdinaryNamespaceList); i++)
  {
    Vector* typedef_nest = vector_peek_at(OrdinaryNamespaceList, i);
    for (size_t j = 1; j <= vector_size(typedef_nest); j++)
    {
      ordinary_data_list* tmp = vector_peek_at(typedef_nest, j);
      if (tmp->name->len == name->len &&
          !strncmp(tmp->name->str, name->str, name->len))
      {
        bool is_same = false;
        if (tmp->ordinary_kind == function_definition_name &&
            vector_size(function_list) == vector_size(tmp->type->param_list))
        {
          for (size_t k = 1; k <= vector_size(tmp->type->param_list); k++)
          {
            if (!is_equal_type(vector_peek_at(function_list, k),
                               vector_peek_at(tmp->type->param_list, k)))
              goto end;
          }
          is_same = true;
        end:
        }
        if (!is_same || tmp->ordinary_kind != function_definition_name)
          return false;
      }
    }
  }
  // なければ追加する
  ordinary_data_list* new = calloc(1, sizeof(ordinary_data_list));
  new->ordinary_kind = function_definition_name;
  new->name = name;
  new->type = alloc_type(TYPE_FUNC);
  new->type->param_list = function_list;
  vector_push(vector_peek(OrdinaryNamespaceList), new);
  return true;
}

enum member_name is_enum_or_function_name(Token* token, size_t* number)
{
  for (size_t i = 1; i <= vector_size(OrdinaryNamespaceList); i++)
  {
    Vector* typedef_nest = vector_peek_at(OrdinaryNamespaceList, i);
    for (size_t j = 1; j <= vector_size(typedef_nest); j++)
    {
      ordinary_data_list* tmp = vector_peek_at(typedef_nest, j);
      if (tmp->name->len == token->len &&
          !strncmp(tmp->name->str, token->str, token->len))
      {
        if (tmp->ordinary_kind == enum_member)
        {
          *number = tmp->enum_number;
          return enum_member_name;
        }
        else if (tmp->ordinary_kind == function_definition_name)
          return function_name;
        else
          return none_of_them;
      }
    }
  }
  return none_of_them;
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
    case TYPE_ENUM:
    case TYPE_INT: return 4;
    case TYPE_BOOL:
    case TYPE_CHAR: return 1;
    case TYPE_SHORT: return 2;
    case TYPE_STR:
    case TYPE_LONG:
    case TYPE_LONGLONG:
    case TYPE_PTR: return 8;
    case TYPE_ARRAY: return size_of(type->ptr_to) * type->size;
    case TYPE_VOID: error_exit("void"); break;
    case TYPE_STRUCT:
    {
      tag_list* peek_type = vector_peek_at(EnumStructList, type->type_num);
      if (type->type_num != peek_type->type->type_num)
        unreachable();
      return peek_type->struct_size;
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
    case TYPE_STRUCT:
    {
      for (size_t i = 1; i <= vector_size(TagNamespaceList); i++)
      {
        Vector* struct_nest = vector_peek_at(TagNamespaceList, i);
        for (size_t j = 1; j <= vector_size(struct_nest); j++)
        {
          tag_list* tmp = vector_peek_at(struct_nest, j);
          if (tmp->type == type)
          {
            if (tmp->struct_alignment)
              return tmp->struct_alignment;
            error_at(tmp->name->str, tmp->name->len, "struct not defined");
          }
        }
      }
      error_exit("struct not found");
    }
    break;
    default: return size_of(type);
  }
  unreachable();
  return 0;
}

Type* find_struct_child(Node* parent, Node* child, size_t* offset)
{  // structのchildを探してその型を返す
  // また、そのchildのオフセットについても返す
  for (size_t i = 1; i <= vector_size(TagNamespaceList); i++)
  {
    Vector* struct_nest = vector_peek_at(TagNamespaceList, i);
    for (size_t j = 1; j <= vector_size(struct_nest); j++)
    {
      tag_list* tmp = vector_peek_at(struct_nest, j);
      if (tmp->type == parent->type)
      {
        if (tmp->struct_size)
        {
          Vector* child_list = tmp->data_list;
          for (size_t k = 1; k <= vector_size(child_list); k++)
          {
            tag_data_list* child_data = vector_peek_at(child_list, k);
            if (child->token->len == child_data->name->len &&
                !strncmp(child->token->str, child_data->name->str,
                         child->token->len))
            {
              *offset = child_data->offset;
              return child_data->type;
            }
          }
          error_at(child->token->str, child->token->len,
                   "unknown child name (parent: %.*s)", (int)parent->token->len,
                   parent->token->str);
        }
        error_at(tmp->name->str, tmp->name->len, "struct not defined");
      }
    }
  }
  error_at(parent->token->str, parent->token->len, "unknown struct type");
  return NULL;
}

void init_types()
{
  Vector* root_typedef = vector_new();
  OrdinaryNamespaceList = vector_new();
  vector_push(OrdinaryNamespaceList, root_typedef);
  Vector* root_struct = vector_new();
  TagNamespaceList = vector_new();
  vector_push(TagNamespaceList, root_struct);
  EnumStructList = vector_new();
}

void new_nest_type()
{
  Vector* new = vector_new();
  vector_push(OrdinaryNamespaceList, new);
  new = vector_new();
  vector_push(TagNamespaceList, new);
}

void exit_nest_type()
{
  vector_pop(OrdinaryNamespaceList);
  vector_pop(TagNamespaceList);
}