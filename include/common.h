#ifndef COMMON_C_COMPILER
#define COMMON_C_COMPILER

#include "parser.h"
#include "type.h"
#include "vector.h"

#ifdef SELF_HOST
#include "../test/compiler_header.h"
#else
#include <stdbool.h>
#endif

#include "vector.h"

typedef struct Var Var;
typedef struct FuncBlock FuncBlock;

typedef enum
{
  // function instructions
  IR_CALL,
  IR_FUNC_PROLOGUE,
  IR_FUNC_EPILOGUE,
  IR_RET,

  // built in functions
  IR_BUILTIN_ASM,
  IR_BUILTIN_VA_LIST,
  IR_BUILTIN_VA_ARGS,

  // memory move
  IR_MOV,

  // arithmetic operator
  IR_ADD,      // +
  IR_SUB,      // -
  IR_MUL,      // *
  IR_OP_DIV,   // /
  IR_OP_IDIV,  // %

  // compare
  IR_EQ,   // ==
  IR_NEQ,  // !=
  IR_LT,   // <
  IR_LTE,  // <=

  // jump instruction
  IR_JMP,  // jmp
  IR_JNE,  // jmp if not equal
  IR_JE,   // jmp if equal

  // memory access
  IR_LOAD,
  IR_STORE,
  IR_STORE_ARG,
  IR_LEA,

  // bitwise operations
  IR_AND,      // &
  IR_OR,       // |
  IR_XOR,      // ~
  IR_NOT,      // !
  IR_BIT_NOT,  // ~
  IR_SHL,      // << unsigned
  IR_SHR,      // >> unsigned
  IR_SAL,      // << signed
  IR_SAR,      // >> signed

  // unary
  IR_NEG,  // negate

  // label
  IR_LABEL,  // jump label

  // string
  IR_STRING,  // string literal
} IRKind;

typedef struct IR IR;
struct IR
{
  IRKind kind;
  union
  {
    // IR_CALL
    struct
    {
      char *func_name;
      size_t func_name_size;
      Vector *args;  // Vector of virtual registers
      int dst_reg;
    } call;

    // IR_BUILTIN_VA_LIST
    struct
    {
      int va_reg;  // virtual register for va_list
    } va_list;

    // IR_MOV
    struct
    {
      int dst_reg;
      int src_reg;
      long long imm_val;  // for immediate value
      bool is_imm;
    } mov;

    // Arithmetic, bitwise, compare operators
    struct
    {
      int dst_reg;
      int lhs_reg;
      int rhs_reg;
      size_t lhs_size;
      size_t rhs_size;
    } bin_op;

    // Unary operators
    struct
    {
      int dst_reg;
      int src_reg;
    } un_op;

    // IR_JMP, IR_JNE, IR_JE
    struct
    {
      char *label;
      int cond_reg;  // not used for IR_JMP
    } jmp;

    // IR_STORE_ARG
    struct
    {
      int dst_reg;
      int arg_index;
      size_t size;
    } store_arg;

    // IR_LOAD, IR_STORE
    struct
    {
      int reg;
      int mem_reg;  // register holding memory address
      int offset;
      size_t size;  // Size of the data being accessed
    } mem;

    // IR_LEA
    struct
    {
      int dst_reg;
      bool is_local;
      bool is_static;
      char *var_name;
      size_t var_name_len;
      size_t var_offset;
    } lea;

    // IR_RET
    struct
    {
      int src_reg;
    } ret;

    // IR_LABEL
    struct
    {
      char *name;
    } label;

    // IR_STRING
    struct
    {
      char *name;
      size_t len;
      char *str;
    } string;

    struct
    {
      char *asm_str;
      size_t asm_len;
    } builtin_asm;
  };
};

typedef struct
{
  Vector *IRs;  // IR instructions
} IR_Blocks;

typedef struct
{
  enum function_type builtin_func;
  Vector *IR_Blocks;  // Basic blocks of IR instructions
  union
  {
    struct
    {
      char *function_name;
      size_t function_name_size;
      bool is_static;
      size_t stack_size;        // Total stack size for local variables
      size_t num_virtual_regs;  // Number of virtual registers used
    } user_defined;
  };
} IRFunc;

typedef struct
{
  char *var_name;
  size_t var_name_len;
  size_t var_size;
  bool is_static;
  union
  {
    long long init_val;  // Used when how2_init is init_val
    struct
    {
      char *var_name;
      size_t var_name_len;
    } assigned_var;      // Used when how2_init is init_pointer
    char *literal_name;  // Used when how2_init is init_string
  };
  enum
  {                // How to initialize when it's a global variable
    reserved,      // To avoid using 0
    init_zero,     // Zero-clear
    init_val,      // Initial value as a number
    init_pointer,  // Initialize with a pointer
    init_string,   // Initialize with a string
  } how2_init;
} GlobalVar;

typedef struct
{
  Vector *global_vars;  // Global variables IR_GLOBAL_VAR, IR_STRING_LITERAL
  Vector *functions;    // Linked list of the function
  Vector *strings;      // String literals
} IRProgram;

#endif