#ifndef GENERATOR_X86_C_COMPILER
#define GENERATOR_X86_C_COMPILER

#include "common.h"

enum register_name
{
  rdi,
  rsi,
  rdx,
  rcx,
  r8,
  r9,
  r10,
  r11,
  rax,  // Caller Saved
  rbx,  // Callee Saved
  rsp,
  rbp,
  r12,
  r13,
  r14,
  r15,
  register_reserved,
};

typedef enum
{
  // Data Transfer Instructions
  X86_MOV,     // Move
  X86_PUSH,    // Push onto stack
  X86_POP,     // Pop from stack
  X86_LEA,     // Load Effective Address
  X86_MOVSX,   // Move with Sign-Extension
  X86_MOVSXD,  // Move with Sign-Extend Doubleword
  X86_MOVZX,   // Move with Zero-Extension

  // Arithmetic Instructions
  X86_ADD,   // Add
  X86_SUB,   // Subtract
  X86_IMUL,  // Signed Multiply (RDX:RAX / operand)
  X86_MUL,   // Unsigned Multiply (RDX:RAX / operand)
  X86_IDIV,  // Signed Divide (RDX:RAX / operand)
  X86_DIV,   // Unsigned Divide (RDX:RAX / operand)
  X86_CQO,   // Convert Quadword to Octoword (Sign-extend RAX into RDX)
  X86_NEG,   // Negate (Two's Complement)

  // Logical Instructions
  X86_OR,   // Logical OR
  X86_XOR,  // Logical XOR
  X86_AND,  // Logical AND
  X86_NOT,  // Logical NOT (One's Complement)
  X86_SHL,  // Shift Left
  X86_SHR,  // Shift Right (Logical)
  X86_SAL,  // Shift Arithmetic Left
  X86_SAR,  // Shift Arithmetic Right

  // Control Flow Instructions
  X86_JMP,    // Jump
  X86_JZ,     // Jump if Zero
  X86_JE,     // Jump if Equal
  X86_JNE,    // Jump if Not Equal
  X86_JNG,    // Jump if Not Greater
  X86_JNGE,   // Jump if Not Greater or Equal
  X86_CALL,   // Call Procedure
  X86_RET,    // Return from Procedure
  X86_LEAVE,  // High Level Procedure Exit

  // Comparison and Conditional Instructions
  X86_CMP,    // Compare
  X86_SETE,   // Set byte if Equal
  X86_SETNE,  // Set byte if Not Equal
  X86_SETL,   // Set byte if Less
  X86_SETLE,  // Set byte if Less or Equal
  X86_SETG,   // Set byte if Greater
  X86_SETGE,  // Set byte if Greater or Equal

  // Other
  X86_LABEL,  // Assembler Directive
} X86_ASMKind;

typedef enum
{
  OP_REG,
  OP_IMM,
  OP_MEM,
} X86_OperandKind;

typedef struct
{
  enum
  {
    virtual_regs,
    real_regs,
  } reg_type;
  union
  {
    size_t* virtual_reg;
    struct
    {
      enum register_name real_reg;
      bool is_reserved;
    };
  };
} X86_REG;

typedef struct
{
  X86_OperandKind kind;
  union
  {
    X86_REG reg;
    long long imm;
    struct
    {
      X86_REG base;
      X86_REG index;
      size_t scale;
      size_t displacement;
    } mem;
  };
} X86_Operand;

// TODO: IMUL 3 operand instruction
#define MAX_OPERANDS 2

typedef struct
{
  X86_ASMKind kind;
  OperandSize size;
  X86_Operand operands[MAX_OPERANDS];
  char* jump_target_label;
  // Bitmask: the bit corresponding to enum register_name
  // 1 if the register is in use, 0 if unused
  unsigned int used_registers;  // 32bit
} X86_ASM;

typedef struct X86_Blocks
{
  Vector* asm_list;
  Vector* parent;
  struct X86_Blocks* lhs;  // child
  struct X86_Blocks* rhs;  // child
  Vector* in;              // X86_REG
  Vector* out;             // X86_REG
} X86_Blocks;

typedef struct
{
  // Bitmask: the bit corresponding to enum register_name
  // 1 if the register is in use, 0 if unused
  unsigned int used_registers;  // 32bit
  Vector* asm_blocks;           // X86_Blocks
  size_t stack_size;
} X86_FUNC;

void generate_x86(IRFunc* program);

#endif