// ------------------------------------------------------------------------------------
// type, enum, union and struct manager
// ------------------------------------------------------------------------------------

#include "include/type.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdint.h>
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
static Vector* GlobalVariablesList;  // global変数のVarリスト

typedef struct
{
  enum member_name ordinary_kind;
  Token* name;
  Type* type;
  size_t enum_number;  // enumのとき利用
  Var* variables;      // 変数のとき利用
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

static Type* find_typedef_type(Token* token)
{
  if (!token)
    return NULL;
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
        return tmp->type;
      }
    }
  }
  return NULL;
}

static ordinary_data_list* find_local_var_in_current_nested_block(Token* token)
{
  for (size_t i = 1; i < vector_size(vector_peek(OrdinaryNamespaceList)); i++)
  {
    ordinary_data_list* var =
        vector_peek_at(vector_peek(OrdinaryNamespaceList), i);
    if (var->name->len == token->len &&
        !strncmp(var->name->str, token->str, token->len))
      return var;
  }
  return NULL;
}

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

  return find_typedef_type(token) != NULL;
}

bool is_typedef(uint8_t storage_class_specifier)
{
  if (storage_class_specifier & 1)
    return true;
  return false;
}

#define ASSERT_STORAGE_SPECIFIER                                              \
  (!*storage_class_specifier || (error_at(get_token()->str, get_token()->len, \
                                          "invalid storage class specifier"), \
                                 false))

#define CONSUMED_TRUE (consumed = true)

Type* declaration_specifiers(uint8_t* storage_class_specifier)
{
  size_t long_count = 0;
  size_t signed_count = 0;
  size_t int_count = 0;
  size_t bool_count = 0;
  size_t char_count = 0;
  size_t short_count = 0;
  size_t unsigned_count = 0;
  size_t void_count = 0;
  size_t const_count = 0;  // 読み飛ばす
  size_t restrict_count = 0;
  size_t volatile_count = 0;  // 読み飛ばす
  size_t inline_count = 0;
  Token* old = get_token();
  Type* type = NULL;

  for (;;)
  {
    Token* token = get_token();
    bool consumed = false;

    if (consume("typedef", TK_IDENT) && ASSERT_STORAGE_SPECIFIER &&
        CONSUMED_TRUE)
      *storage_class_specifier = 1 << 0;
    else if (consume("extern", TK_IDENT) && ASSERT_STORAGE_SPECIFIER &&
             CONSUMED_TRUE)
      *storage_class_specifier = 1 << 1;
    else if (consume("static", TK_IDENT) && ASSERT_STORAGE_SPECIFIER &&
             CONSUMED_TRUE)
      *storage_class_specifier = 1 << 2;
    else if (consume("auto", TK_IDENT) && ASSERT_STORAGE_SPECIFIER &&
             CONSUMED_TRUE)
      *storage_class_specifier = 1 << 3;
    else if (consume("register", TK_IDENT) && ASSERT_STORAGE_SPECIFIER &&
             CONSUMED_TRUE)
      *storage_class_specifier = 1 << 4;
    else if (consume("const", TK_IDENT) && CONSUMED_TRUE)
      const_count++;
    else if (consume("restrict", TK_IDENT) && CONSUMED_TRUE)
      restrict_count++;
    else if (consume("volatile", TK_IDENT) && CONSUMED_TRUE)
      volatile_count++;
    else if (consume("inline", TK_IDENT) && CONSUMED_TRUE)
      if (inline_count)
        error_at(token->str, token->len,
                 "multiple function-specifier(inline) "
                 "declaration");
      else
        inline_count++;
    else if (type == NULL &&
             (peek("struct", TK_IDENT) || peek("union", TK_IDENT) ||
              peek("enum", TK_IDENT)))
    {
      if (long_count || signed_count || unsigned_count || int_count ||
          bool_count || char_count || short_count || void_count)
        error_at(token->str, token->len, "invalid type specifier");
      consumed = true;
      bool is_struct = consume("struct", TK_IDENT);
      bool is_union = !is_struct && consume("union", TK_IDENT);
      bool is_enum = !is_struct && !is_union && consume("enum", TK_IDENT);

      Token* tag_name = consume_ident();
      tag_list* new = NULL;
      if (tag_name)
      {
        for (size_t i = 1; i <= vector_size(TagNamespaceList); i++)
        {
          Vector* tag_nest = vector_peek_at(TagNamespaceList, i);
          for (size_t j = 1; j <= vector_size(tag_nest); j++)
          {
            tag_list* tmp = vector_peek_at(tag_nest, j);
            if (tmp->name && tmp->name->len == tag_name->len &&
                !strncmp(tmp->name->str, tag_name->str, tag_name->len))
            {
              if ((is_struct && tmp->tagkind != is_struct) ||
                  (is_union && tmp->tagkind != is_union) ||
                  (is_enum && tmp->tagkind != is_enum))
                error_at(tag_name->str, tag_name->len, "invalid type");
              new = tmp;
              break;
            }
          }
          if (new)
            break;
        }
      }

      bool is_definition = consume("{", TK_RESERVED);
      if (is_definition && new && new->data_list)
      {
        bool is_same_nest = false;
        for (size_t i = 1; i <= vector_size(vector_peek(TagNamespaceList)); i++)
        {
          tag_list* tmp = vector_peek_at(vector_peek(TagNamespaceList), i);
          if (tmp->name && tmp->name->len == tag_name->len &&
              !strncmp(tmp->name->str, tag_name->str, tag_name->len))
            is_same_nest = true;
        }
        if (is_same_nest)
          error_at(get_old_token()->str, get_old_token()->len,
                   "multiple definition");
      }
      if (!new)
      {
        new = calloc(1, sizeof(tag_list));
        if (is_struct)
          new->tagkind = is_struct;
        else if (is_union)
          new->tagkind = is_union;
        else
          new->tagkind = is_enum;
        new->name = tag_name;
        new->type = alloc_type(is_enum ? TYPE_ENUM : TYPE_STRUCT);
        new->type->type_num = ++tag_id;
        vector_push(EnumStructList, new);
        vector_push(vector_peek(TagNamespaceList), new);
      }

      if (is_definition)
      {
        new->data_list = vector_new();
        if (is_enum)
        {
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
            ordinary_data_list* new_enum_member_name =
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
            new_enum_member_name->ordinary_kind = enum_member_name;
            new_enum_member_name->enum_number = enum_num;
            new_enum_member_name->name = identifier;
            new_enum_member_name->type = alloc_type(TYPE_INT);
            vector_push(vector_peek(OrdinaryNamespaceList),
                        new_enum_member_name);
            enum_num++;
            if (consume(",", TK_RESERVED))
              is_comma = true;
            if (consume("}", TK_RESERVED))
              break;
            else if (is_comma)
              continue;
            error_at(get_token()->str, get_token()->len,
                     "invalid enum definition");
          }
        }
        else
        {
          size_t struct_size = 0;
          size_t struct_alignment = 0;
          for (;;)
          {
            uint8_t storage_class_specifier = 0;
            Type* child_type = declaration_specifiers(&storage_class_specifier);
            Node* node =
                declarator_no_side_effect(&child_type, storage_class_specifier);
            if (!node || !node->type)
              error_at(get_token()->str, get_token()->len,
                       "invalid struct definition");
            tag_data_list* new_data = malloc(sizeof(tag_data_list));
            new_data->name = node->token;
            new_data->type = node->type;
            new_data->offset = struct_size;
            vector_push(new->data_list, new_data);
            size_t alignment = align_of(node->type);
            if (new->tagkind == is_struct)
              struct_size = (struct_size % alignment
                                 ? (struct_size / alignment + 1) * alignment
                                 : struct_size) +
                            size_of(node->type);
            else if (struct_size < size_of(node->type))
              struct_size = size_of(node->type);
            if (struct_alignment < alignment)
              struct_alignment = alignment;
            expect(";", TK_RESERVED);
            if (consume("}", TK_RESERVED))
              break;
          }
          struct_size = (struct_size + struct_alignment - 1) /
                        struct_alignment * struct_alignment;
          new->struct_size = struct_size;
          new->struct_alignment = struct_alignment;
        }
      }
      type = new->type;
    }
    else if (type == NULL)
    {
      if (consume("long", TK_IDENT) && CONSUMED_TRUE)
        long_count++;
      else if (consume("signed", TK_IDENT) && CONSUMED_TRUE)
        signed_count++;
      else if (consume("unsigned", TK_IDENT) && CONSUMED_TRUE)
        unsigned_count++;
      else if (consume("int", TK_IDENT) && CONSUMED_TRUE)
        int_count++;
      else if ((consume("bool", TK_IDENT) || consume("_Bool", TK_IDENT)) &&
               CONSUMED_TRUE)
        bool_count++;
      else if (consume("char", TK_IDENT) && CONSUMED_TRUE)
        char_count++;
      else if (consume("short", TK_IDENT) && CONSUMED_TRUE)
        short_count++;
      else if (consume("void", TK_IDENT) && CONSUMED_TRUE)
        void_count++;
      else if (!(long_count || signed_count || unsigned_count || int_count ||
                 bool_count || char_count || short_count || void_count))
      {
        Token* ident = peek_ident();
        if (ident)
        {
          Type* found_type = NULL;
          for (size_t i = vector_size(OrdinaryNamespaceList); i >= 1; i--)
          {
            Vector* typedef_nest = vector_peek_at(OrdinaryNamespaceList, i);
            for (size_t j = 1; j <= vector_size(typedef_nest); j++)
            {
              ordinary_data_list* tmp = vector_peek_at(typedef_nest, j);
              if (tmp->ordinary_kind == typedef_name && tmp->name &&
                  tmp->name->len == ident->len &&
                  !strncmp(tmp->name->str, ident->str, ident->len))
              {
                found_type = tmp->type;
                goto found_typedef_in_loop;
              }
            }
          }
        found_typedef_in_loop:
          if (found_type)
          {
            type = found_type;
            consume_ident();
            consumed = true;
          }
        }
      }
    }

    if (!consumed)
      break;
  }

  if (type)
  {
    if (long_count || signed_count || unsigned_count || int_count ||
        bool_count || char_count || short_count || void_count)
      error_at(old->str, get_token()->str - old->str + get_token()->len,
               "invalid type specifier");
    return type;
  }

  if (!(long_count || signed_count || unsigned_count || int_count ||
        bool_count || short_count || char_count || void_count))
  {
    return NULL;
  }

  if (long_count > 2 ||
      (long_count && (bool_count || char_count || short_count || void_count)) ||
      signed_count + unsigned_count > 1 ||
      int_count + bool_count + char_count + short_count + void_count > 1 ||
      (signed_count | unsigned_count) & (void_count | bool_count))
    error_at(old->str, get_token()->str - old->str + get_token()->len,
             "invalid type specifier");

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
  else if (int_count || unsigned_count || signed_count)
    kind = TYPE_INT;
  else
    unreachable();
  type = alloc_type(kind);
  if (!unsigned_count)
    type->is_signed = true;
  return type;
}

void add_typedef(Token* token, Type* type)
{
  // 名前とTypeを関連付ける
  ordinary_data_list* new = malloc(sizeof(ordinary_data_list));
  new->ordinary_kind = typedef_name;
  new->name = token;
  new->type = type;
  vector_push(vector_peek(OrdinaryNamespaceList), new);
}

Var* add_variables(Token* token, Type* type, uint8_t storage_class_specifier)
{
  if (storage_class_specifier & 1 << 2)
    unimplemented();  // static にはまだ対応していない
  if (!token || !type)
    return NULL;
  if (type && is_typedef(storage_class_specifier))
  {
    add_typedef(token, type);
    return NULL;
  }

  // 変数名が同じものが以前あったかどうかを調査
  ordinary_data_list* same = find_local_var_in_current_nested_block(token);
  if (same && type)
    error_at(token->str, token->len, "同じ名前の変数がすでに存在します");

  // 新規変数の場合
  Var* var = calloc(1, sizeof(Var));
  var->token = token;
  var->name = token->str;
  var->len = token->len;
  var->type = type;
  if (vector_size(OrdinaryNamespaceList) == 1)
  {
    var->is_local = false;  // 変数はグローバル変数
    vector_push(GlobalVariablesList, var);
  }
  else
    var->is_local = true;  // 変数はローカル変数
  var->storage_class_specifier = storage_class_specifier;
  ordinary_data_list* new = calloc(1, sizeof(ordinary_data_list));
  new->ordinary_kind = variables_name;
  new->name = token;
  new->variables = var;
  vector_push(vector_peek(OrdinaryNamespaceList), new);
  return var;
}

typedef struct literal_list literal_list;

struct literal_list
{
  literal_list* next;
  char* literal_name;
  char* name;
  size_t len;
};

static literal_list* literal_top;
static size_t literal_counter;

// ポインタへの文字列リテラルの代入の際利用
char* add_string_literal(Token* token)
{
  pr_debug2("string literal found");
  // 同じ文字列がすでに存在した場合、それを使う
  // 仕様として文字列の変更が認められていないため可能
  for (literal_list* pointer = literal_top; pointer; pointer = pointer->next)
    if (token->len == pointer->len &&
        !strncmp(token->str, pointer->name, pointer->len))
      return pointer->literal_name;
  literal_list* new = calloc(1, sizeof(literal_list));
  if (literal_top)
    literal_top->next = new;
  literal_top = new;
  literal_top->name = token->str;
  literal_top->len = token->len;
  // 文字列リテラルの名前を決める
  // 9999個まで文字列リテラルが存在可能
  char* literal_name = malloc(8);
  int snprintf_return = snprintf(literal_name, 8, ".LC%lu", literal_counter++);
  if (snprintf_return < 3 || snprintf_return > 7)
    unreachable();
  pr_debug2("literal_name: %s", literal_name);
  literal_top->literal_name = literal_name;
  // グローバル変数に存在しないか一応確かめる
  for (size_t i = 1; i <= vector_size(vector_peek_at(OrdinaryNamespaceList, 1));
       i++)
  {
    ordinary_data_list* tmp =
        vector_peek_at(vector_peek_at(OrdinaryNamespaceList, 1), i);
    if (tmp->ordinary_kind == variables_name && tmp->variables->len &&
        !strncmp(tmp->variables->name, token->str, token->len))
      unreachable();
  }
  Var* new_var = calloc(1, sizeof(Var));
  new_var->token = token;
  new_var->name = literal_name;
  new_var->len = snprintf_return;
  new_var->type = alloc_type(TYPE_STR);
  new_var->is_local = false;
  new_var->how2_init = init_string;
  vector_push(vector_peek_at(OrdinaryNamespaceList, 1), new_var);
  vector_push(GlobalVariablesList, new_var);
  return literal_name;
}

bool add_function_name(Vector* function_list, Token* name,
                       uint8_t storage_class_specifier)
{
  if (storage_class_specifier & ~(1 << 2))
    error_at(name->str, name->len, "invalid storage class specifier");
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
        if (tmp->ordinary_kind == function_name &&
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
        if (!is_same || tmp->ordinary_kind != function_name)
          return false;
      }
    }
  }
  // なければ追加する
  ordinary_data_list* new = calloc(1, sizeof(ordinary_data_list));
  new->ordinary_kind = function_name;
  new->name = name;
  new->type = alloc_type(TYPE_FUNC);
  new->type->param_list = function_list;
  vector_push(vector_peek(OrdinaryNamespaceList), new);
  return true;
}

enum member_name is_enum_or_function_or_typedef_or_variables_name(
    Token* token, size_t* number, Type** type, Var** var)
{
  for (size_t i = vector_size(OrdinaryNamespaceList); i > 0; i--)
  {
    Vector* typedef_nest = vector_peek_at(OrdinaryNamespaceList, i);
    for (size_t j = 1; j <= vector_size(typedef_nest); j++)
    {
      ordinary_data_list* tmp = vector_peek_at(typedef_nest, j);
      if (tmp->name->len == token->len &&
          !strncmp(tmp->name->str, token->str, token->len))
      {
        if (type)
          *type = tmp->type;
        switch (tmp->ordinary_kind)
        {
          case enum_member_name:
            if (number)
              *number = tmp->enum_number;
            return enum_member_name;
          case variables_name:
            if (var)
              *var = tmp->variables;
            return variables_name;
          default: return tmp->ordinary_kind;
        }
      }
    }
  }
  return none_of_them;
}

Vector* get_global_var()
{
  return GlobalVariablesList;
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
  Type* type = parent->type;
  if (type->type == TYPE_PTR || type->type == TYPE_ARRAY)
    type = type->ptr_to;
  for (size_t i = 1; i <= vector_size(TagNamespaceList); i++)
  {
    Vector* struct_nest = vector_peek_at(TagNamespaceList, i);
    for (size_t j = 1; j <= vector_size(struct_nest); j++)
    {
      tag_list* tmp = vector_peek_at(struct_nest, j);
      if (tmp->type == type)
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
  GlobalVariablesList = vector_new();
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
