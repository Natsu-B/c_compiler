// ------------------------------------------------------------------------------------
// type, enum, union and struct manager
// ------------------------------------------------------------------------------------

#include "include/type.h"

#include "include/error.h"
#include "include/parser.h"
#include "include/vector.h"

static Vector* TypedefList;
static Vector* StructList;

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
    case TYPE_STR:
    case TYPE_LONG:
    case TYPE_LONGLONG:
    case TYPE_PTR:
    case TYPE_ARRAY: return 8;
    // case TYPE_STRUCT:
    // {
    // }
    default: break;
  }
  unreachable();
  return 0;
}

void init_types()
{
  TypedefList = vector_new();
  StructList = vector_new();
}

void new_nest_type()
{
}

void exit_nest_type()
{
}