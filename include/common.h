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
  IR_FUNC_PROLOGUE,  // LEADER
  IR_FUNC_EPILOGUE,  // TERMINATOR
  IR_RET,            // TERMINATOR

  // built in functions
  IR_BUILTIN_ASM,
  IR_BUILTIN_VA_LIST,
  IR_BUILTIN_VA_ARGS,

  // memory move
  IR_MOV,

  // arithmetic operator
  IR_ADD,   // +
  IR_SUB,   // -
  IR_MUL,   // * (signed)
  IR_MULU,  // * (unsigned)
  IR_DIV,   // / (signed)
  IR_DIVU,  // / (unsigned)
  IR_REM,   // % (signed)
  IR_REMU,  // % (unsigned)

  // compare
  IR_EQ,    // ==
  IR_NEQ,   // !=
  IR_LT,    // < (signed)
  IR_LTU,   // < (unsigned)
  IR_LTE,   // <= (signed)
  IR_LTEU,  // <= (unsigned)

  // jump instruction
  IR_JMP,  // TERMINATOR: jmp
  IR_JNE,  // TERMINATOR: jmp if not equal
  IR_JE,   // TERMINATOR: jmp if equal

  // memory access
  IR_LOAD,
  IR_STORE,
  IR_STORE_ARG,
  IR_LEA,

  // memory size
  IR_SIGN_EXTEND,
  IR_ZERO_EXTEND,
  IR_TRUNCATE,

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

  // phi
  IR_PHI,

  // label
  IR_LABEL,  // LEADER: jump label

  // string
  IR_STRING,  // string literal
} IRKind;

typedef enum
{
  SIZE_RESERVED,
  SIZE_BYTE,   // 1byte
  SIZE_WORD,   // 2byte
  SIZE_DWORD,  // 4byte
  SIZE_QWORD,  // 8byte
} OperandSize;

typedef struct
{
  size_t reg_num;
  OperandSize reg_size;
  Vector *used_list;  // list of IR which is used the IR_REG
} IR_REG;

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
      IR_REG *dst_reg;
    } call;

    // IR_BUILTIN_VA_LIST
    struct
    {
      IR_REG *va_reg;  // virtual register for va_list
    } va_list;

    // IR_MOV
    struct
    {
      IR_REG *dst_reg;
      IR_REG *src_reg;
      long long imm_val;  // for immediate value
      bool is_imm;
    } mov;

    // memory size
    struct
    {
      IR_REG *dst_reg;
      IR_REG *src_reg;
    } memsize;

    // Arithmetic, bitwise, compare operators
    struct
    {
      IR_REG *dst_reg;
      IR_REG *lhs_reg;
      IR_REG *rhs_reg;
    } bin_op;

    // phi
    struct
    {
      IR_REG *dst_reg;
      IR_REG *lhs_reg;
      IR_REG *rhs_reg;
    } phi;

    // Unary operators
    struct
    {
      IR_REG *dst_reg;
      IR_REG *src_reg;
    } un_op;

    // IR_JMP, IR_JNE, IR_JE
    struct
    {
      char *label;
      IR_REG *cond_reg;  // not used for IR_JMP
    } jmp;

    // IR_STORE_ARG
    struct
    {
      IR_REG *dst_reg;
      size_t arg_index;
    } store_arg;

    // IR_LOAD, IR_STORE
    struct
    {
      IR_REG *reg;
      IR_REG *mem_reg;  // register holding memory address
      size_t offset;
      size_t size;  // Size of the data being accessed
    } mem;

    // IR_LEA
    struct
    {
      IR_REG *dst_reg;
      bool is_local;
      bool is_static;
      char *var_name;
      size_t var_name_len;
      size_t var_offset;
    } lea;

    // IR_RET
    struct
    {
      IR_REG *src_reg;
      bool return_void;
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

    // IR_BUILTIN_ASM
    struct
    {
      char *asm_str;
      size_t asm_len;
    } builtin_asm;
  };
};

typedef struct IR_Blocks
{
  Vector *IRs;            // IR instructions
  Vector *parent;         // parent list (IR_Blocks vector)
  struct IR_Blocks *lhs;  // child
  struct IR_Blocks *rhs;  // child
  Vector *reg_in;         // reg num (size_t*)
  Vector *reg_use;
  Vector *reg_def;
  Vector *reg_out;
} IR_Blocks;

typedef struct
{
  enum function_type builtin_func;
  Vector *IR_Blocks;  // Basic blocks of IR instructions
  Vector *labels;     // jump label list
  union
  {
    struct
    {
      char *function_name;
      size_t function_name_size;
      bool is_static;
      size_t stack_size;         // Total stack size for local variables
      Vector *num_virtual_regs;  // List of virtual registers
    } user_defined;
  };
} IRFunc;

typedef struct
{
  union
  {
    size_t zero_len;  // Used when how2_init is init_zero
    struct
    {
      long long init_val;
      size_t value_size;
    } value;  // Used when how2_init is init_val
    struct
    {
      char *var_name;
      size_t var_name_len;
    } assigned_var;      // Used when how2_init is init_pointer
    char *literal_name;  // Used when how2_init is init_string
    Vector *init_list;   // Used when how2_init is init_list
  };
  enum
  {                // How to initialize when it's a global variable
    reserved,      // To avoid using 0
    init_zero,     // Zero-clear
    init_val,      // Initial value as a number
    init_pointer,  // Initialize with a pointer
    init_string,   // Initialize with a string
  } how2_init;
} GVarInitializer;

typedef struct
{
  char *var_name;
  size_t var_name_len;
  size_t var_size;
  bool is_static;
  IR_Blocks *initializer;
} GlobalVar;

typedef struct
{
  Vector *global_vars;  // Global variables IR_GLOBAL_VAR, IR_STRING_LITERAL
  Vector *functions;    // Linked list of the function
  Vector *strings;      // String literals
} IRProgram;

#endif