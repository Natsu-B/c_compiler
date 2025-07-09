#ifndef TYPE_C_COMPILER
#define TYPE_C_COMPILER

#ifdef SELF_HOST
#include "../test/compiler_header.h"
#else
#include <stdbool.h>
#include <stddef.h>
#endif

#include "vector.h"

typedef struct Type Type;
typedef struct Token Token;

// サポートしている変数の型
typedef enum
{
  TYPE_INT,       // int型 signed 32bit
  TYPE_BOOL,      // bool型 8 bit
  TYPE_CHAR,      // char型 8bit
  TYPE_LONG,      // long型 64bit
  TYPE_LONGLONG,  // long long型 signed 64bit
  TYPE_SHORT,     // short型 16bit
  TYPE_VOID,      // void型
  TYPE_STR,       // char文字列
  TYPE_STRUCT,    // struct、union で定義されている型
  TYPE_PTR,       // 型へのポインタ
  TYPE_ARRAY,     // 配列
  TYPE_FUNC,      // 関数
  TYPE_TYPEDEF,   // typedef
  TYPE_ENUM,      // enum
  TYPE_NULL,      // 失敗時等に返す 実際の型ではない
} TypeKind;

// 変数の型を管理するstruct
struct Type
{
  TypeKind type;
  Type *ptr_to;  // TYPE_PTR, TYPE_ARRAY, TYPE_TYPEDEFのとき利用
  // union
  // {
    bool is_signed;      // TYPE_INT等の整数型のとき利用
    size_t size;         // TYPE_ARRAYのとき利用
    size_t type_num;     // TYPE_STRUCTのとき利用
    Vector *param_list;  // TYPE_FUNCのとき利用 1つ目の引数は返り値の型
                         // その他は引数の型
  // };
};

typedef struct Node Node;  // parser.hをincludeできないため定義だけしておく

Type *alloc_type(TypeKind kind);
Type *declaration_specifiers();
bool add_function_name(Vector *function_list, Token *name);

enum member_name
{
  enum_member_name,
  function_name,
  none_of_them
};
enum member_name is_enum_or_function_name(Token *token, size_t *number);

size_t size_of(Type *type);
size_t align_of(Type *type);
void add_typedef();
Type *find_struct_child(Node *parent, Node *child, size_t *offset);
void init_types();
void new_nest_type();
void exit_nest_type();

#endif