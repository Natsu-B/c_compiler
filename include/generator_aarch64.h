#ifndef GENERATOR_AARCH64_C_COMPILER
#define GENERATOR_AARCH64_C_COMPILER

#include "common.h"

// AArch64 Register Names
typedef enum {
  a64_x0 = 0,
  a64_x1,
  a64_x2,
  a64_x3,
  a64_x4,
  a64_x5,
  a64_x6,
  a64_x7,
  a64_x8,
  a64_x9,
  a64_x10,
  a64_x11,
  a64_x12,
  a64_x13,
  a64_x14,
  a64_x15,
  a64_x16,
  a64_x17,
  a64_x18,
  a64_x19,
  a64_x20,
  a64_x21,
  a64_x22,
  a64_x23,
  a64_x24,
  a64_x25,
  a64_x26,
  a64_x27,
  a64_x28,
  a64_x29, // FP
  a64_x30, // LR
  a64_sp,  // SP pseudo (x31)
  a64_reg_reserved
} a64_register_name;

typedef enum {
  A64_UNUSED = 0,
  // Data movement
  A64_MOV,
  A64_LDR,
  A64_STR,
  A64_ADRP,
  A64_ADD_ADDR, // add dst, dst, :lo12:symbol or add dst, fp, #imm

  // Arithmetic/logic/shift (3-operand)
  A64_ADD,
  A64_SUB,
  A64_MUL,
  A64_SDIV,
  A64_UDIV,
  A64_AND,
  A64_ORR,
  A64_EOR,
  A64_LSLV,
  A64_LSRV,
  A64_ASRV,
  A64_MSUB, // r = a - b*c (for remainder)
  A64_NEG,
  A64_MVN,

  // Extend (unary)
  A64_SXTB, // sign-extend byte  -> 32/64
  A64_SXTH, // sign-extend half  -> 32/64
  A64_SXTW, // sign-extend word  -> 64
  A64_UXTB, // zero-extend byte  -> 32/64
  A64_UXTH, // zero-extend half  -> 32/64
  A64_UXTW, // zero-extend word  -> 64

  // Compare + set
  A64_CMP,
  A64_CSET_EQ,
  A64_CSET_NE,
  A64_CSET_LT,  // signed <
  A64_CSET_LO,  // unsigned < (carry clear)
  A64_CSET_LE,  // signed <=
  A64_CSET_LS,  // unsigned <=

  // Control flow
  A64_B,
  A64_BEQ,
  A64_BNE,
  A64_CALL,
  A64_LABEL,

  // Virtual
  A64_RETURN,
  A64_BUILTIN_ASM,
} A64_ASMKind;

typedef enum {
  A64_OP_RESERVED,
  A64_OP_REG,
  A64_OP_IMM,
  A64_OP_MEM,          // [base + #disp]
  A64_OP_MEM_RELATIVE, // symbol + #disp
} A64_OperandKind;

typedef struct {
  enum { a64_regs_reserved, a64_virtual_regs, a64_real_regs } reg_type;
  OperandSize size; // SIZE_BYTE..SIZE_QWORD
  size_t reg_id;    // virtual id
  struct {
    a64_register_name real_reg;
    bool is_reserved;
  };
} A64_REG;

typedef struct {
  A64_OperandKind kind;
  union {
    A64_REG* reg;
    long long imm;
    struct {
      union {
        A64_REG* base; // A64_OP_MEM
        struct {        // A64_OP_MEM_RELATIVE
          char* var_name;
          size_t var_name_len;
        };
      };
      int displacement; // #imm
      OperandSize mov_size; // for loads/stores
    } mem;
  };
  struct {
    bool require_register;
    bool has_fixed_register;
    a64_register_name fixed_register;
    unsigned int reg_class_mask; // unused for now
  } constraint;
} A64_Operand;

#define A64_MAX_OPERANDS 3

typedef struct {
  A64_ASMKind kind;
  union {
    struct {
      A64_Operand operands[A64_MAX_OPERANDS];
      char* jump_target_label;
      unsigned int implicit_used_registers;
      size_t insn_id;
    };
    struct {
      size_t asm_len;
      char* asm_str;
    } builtin_asm;
  };
} A64_ASM;

typedef struct A64_Blocks {
  Vector* asm_list; // A64_ASM*
  Vector* parent;   // A64_Blocks*
  struct A64_Blocks* lhs;
  struct A64_Blocks* rhs;
  Vector* in;  // A64_REG*
  Vector* out; // A64_REG*
} A64_Blocks;

typedef struct A64_FUNC {
  unsigned int used_registers; // bitmask by a64_register_name
  Vector* asm_blocks;          // A64_Blocks*
  char* function_name;
  size_t function_name_size;
  bool is_static;
  bool stack_used;
  size_t stack_size;
  size_t num_virtual_regs;
  Vector* virtual_regs; // A64_REG* by IR reg + 1

  // Regalloc results
  Vector* vreg_real;        // size_t* of a64_register_name
  Vector* vreg_stack_slot;  // size_t* (rbp-relative offset in bytes)
  Vector* vreg_real_end;    // size_t* program point where mapping expires
  size_t spill_stack_size;  // total spill bytes

  // Rematerialization hints (immediates only for now)
  Vector* vreg_is_remat_imm;     // size_t* as bool
  Vector* vreg_remat_imm_value;  // long long*
} A64_FUNC;

// Backend driver
void generate_aarch64(IRFunc* func);

#endif // GENERATOR_AARCH64_C_COMPILER
