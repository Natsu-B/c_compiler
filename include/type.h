#ifndef TYPE_C_COMPILER
#define TYPE_C_COMPILER

#ifdef SELF_HOST
#include "../test/compiler_header.h"
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

#include "parser.h"
#include "vector.h"

typedef struct Var Var;
typedef struct Type Type;

// 変数を管理するstruct
struct Var
{
  char *name;    // 変数名
  size_t len;    // 変数名 長さ
  Type *type;    // 型
  Token *token;  // 変数に対応するトークン

  // 0bit目がtypedefかどうか
  // 1bit目がexternかどうか
  // 2bit目がstaticかどうか
  // 3bit目がautoかどうか
  // 4bit目がregisterかどうか
  uint8_t storage_class_specifier;
  bool is_local;  // ローカル変数かグローバル変数か
  enum
  {                // global変数のとき 初期化をどうするか
    reserved,      // 0を使わないように
    init_zero,     // ゼロクリア
    init_val,      // 数字での初期値
    init_pointer,  // ポインタでの初期化
    init_string,   // 文字列での初期化
  } how2_init;
  char *global_name;  // グローバル変数の変数名
                      // static等で元の名前とは変更になったりするため
  size_t offset;      // ローカル変数の場合 RBP - offsetの位置に変数がある
};

Var *add_variables(Token *token, Type *type, uint8_t storage_class_specifier);
char *add_string_literal(Token *token);
Vector *get_global_var();

typedef struct Type Type;
typedef struct Token Token;

// サポートしている変数の型
typedef enum
{
  TYPE_NULL,       // 失敗時等に返す 実際の型ではない
  TYPE_INT,        // int型 signed 32bit
  TYPE_BOOL,       // bool型 8 bit
  TYPE_CHAR,       // char型 8bit
  TYPE_LONG,       // long型 64bit
  TYPE_LONGLONG,   // long long型 signed 64bit
  TYPE_SHORT,      // short型 16bit
  TYPE_VOID,       // void型
  TYPE_STR,        // char文字列
  TYPE_STRUCT,     // struct、union で定義されている型
  TYPE_PTR,        // 型へのポインタ
  TYPE_ARRAY,      // 配列
  TYPE_FUNC,       // 関数
  TYPE_ENUM,       // enum
  TYPE_VARIABLES,  // 可変長引数
} TypeKind;

// 変数の型を管理するstruct
struct Type
{
  TypeKind type;
  Type *ptr_to;        // TYPE_PTR, TYPE_ARRAY, TYPE_TYPEDEFのとき利用
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

bool is_type_specifier(Token *token);
bool is_typedef(uint8_t storage_class_specifier);
Type *alloc_type(TypeKind kind);
Type *declaration_specifiers(uint8_t *storage_class_specifier);
bool add_function_name(Vector *function_list, Token *name,
                       uint8_t storage_class_specifier);

enum member_name
{
  enum_member_name,
  function_name,
  typedef_name,
  variables_name,
  none_of_them
};
enum member_name is_enum_or_function_or_typedef_or_variables_name(
    Token *token, size_t *number, Type **type, Var **var);
size_t size_of(Type *type);
size_t align_of(Type *type);
void add_typedef();
Type *find_struct_child(Node *parent, Node *child, size_t *offset);
void init_types();
void new_nest_type();
void exit_nest_type();

#endif