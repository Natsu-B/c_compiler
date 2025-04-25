#ifndef TYPE_C_COMPILER
#define TYPE_C_COMPILER

#include <stdbool.h>
#include <stddef.h>

typedef struct Type Type;

// サポートしている変数の型
typedef enum
{
  TYPE_INT,       // int型 signed 32bit
  TYPE_BOOL,      // bool型 8 bit
  TYPE_CHAR,      // char型 8bit
  TYPE_LONG,      // long型 32bit
  TYPE_LONGLONG,  // long long型 signed 64bit
  TYPE_SHORT,     // short型 16bit
  TYPE_VOID,      // void型
  TYPE_STR,       // char文字列
  TYPE_STRUCT,    // struct、union で定義されている型
  TYPE_PTR,       // 型へのポインタ
  TYPE_ARRAY,     // 配列
  TYPE_NULL,      // 失敗時等に返す 実際の型ではない
} TypeKind;

// 変数の型を管理するstruct
struct Type
{
  TypeKind type;
  Type *ptr_to;  // TYPE_PTR, TYPE_ARRAYのとき利用
  union
  {
    bool is_signed;      // TYPE_INT等の整数型のとき利用
    size_t size;         // TYPE_ARRAYのとき利用
    size_t struct_type;  // TYPE_STRUCTのとき利用
  };
};

Type *alloc_type(TypeKind kind);
Type *is_type();
size_t size_of(Type *type);
size_t align_of(Type *type);
void init_types();
void new_nest_type();
void exit_nest_type();

#endif