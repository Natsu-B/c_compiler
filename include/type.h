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

// Struct to manage variables
struct Var
{
  char *name;    // Variable name
  size_t len;    // Length of variable name
  Type *type;    // Type
  Token *token;  // Token corresponding to the variable

  // Whether the 0th bit is typedef
  // Whether the 1st bit is extern
  // Whether the 2nd bit is static
  // Whether the 3rd bit is auto
  // Whether the 4th bit is register
  uint8_t storage_class_specifier;
  bool is_local;  // Whether it is a local or global variable
  enum
  {                // How to initialize when it's a global variable
    reserved,      // To avoid using 0
    init_zero,     // Zero-clear
    init_val,      // Initial value as a number
    init_pointer,  // Initialize with a pointer
    init_string,   // Initialize with a string
  } how2_init;
  char *global_name;  // Variable name for global variable
                      // May change from original name due to static, etc.
  size_t offset;      // For local variables, the variable is at RBP - offset
};

Var *add_variables(Token *token, Type *type, uint8_t storage_class_specifier);
char *add_string_literal(Token *token);
Vector *get_global_var();

typedef struct Type Type;
typedef struct Token Token;

// Supported variable types
typedef enum
{
  TYPE_NULL,       // Returns on failure, not an actual type
  TYPE_INT,        // int type signed 32bit
  TYPE_BOOL,       // bool type 8 bit
  TYPE_CHAR,       // char type 8bit
  TYPE_LONG,       // long type 64bit
  TYPE_LONGLONG,   // long long type signed 64bit
  TYPE_SHORT,      // short type 16bit
  TYPE_VOID,       // void type
  TYPE_STR,        // char string
  TYPE_STRUCT,     // struct, union defined type
  TYPE_PTR,        // Pointer to type
  TYPE_ARRAY,      // Array
  TYPE_FUNC,       // Function
  TYPE_ENUM,       // Enum
  TYPE_VARIABLES,  // Variadic arguments
} TypeKind;

// Struct to manage variable types
struct Type
{
  TypeKind type;
  Type *ptr_to;        // Used for TYPE_PTR, TYPE_ARRAY, TYPE_TYPEDEF
                       // union
                       // {
  bool is_signed;      // Used for integer types like TYPE_INT
  size_t size;         // Used for TYPE_ARRAY
  size_t type_num;     // Used for TYPE_STRUCT
  Vector *param_list;  // Used for TYPE_FUNC, first argument is return type, others are argument types
  // };
};

typedef struct Node Node;  // Define only to include parser.h

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