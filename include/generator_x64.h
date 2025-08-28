#ifndef GENERATOR_X64_C_COMPILER
#define GENERATOR_X64_C_COMPILER

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
  X64_UNUSED,  // uninitialized
  // Data Transfer Instructions
  X64_MOV,     // Move
  X64_PUSH,    // Push onto stack
  X64_POP,     // Pop from stack
  X64_LEA,     // Load Effective Address
  X64_MOVSX,   // Move with Sign-Extension
  X64_MOVSXD,  // Move with Sign-Extend Doubleword
  X64_MOVZX,   // Move with Zero-Extension

  // Arithmetic Instructions
  X64_ADD,   // Add
  X64_SUB,   // Subtract
  X64_IMUL,  // Signed Multiply (RDX:RAX / operand)
  X64_MUL,   // Unsigned Multiply (RDX:RAX / operand)
  X64_IDIV,  // Signed Divide (RDX:RAX / operand)
  X64_DIV,   // Unsigned Divide (RDX:RAX / operand)
  X64_CQO,   // Convert Quadword to Octoword (Sign-extend RAX into RDX)
  X64_NEG,   // Negate (Two's Complement)

  // Logical Instructions
  X64_OR,   // Logical OR
  X64_XOR,  // Logical XOR
  X64_AND,  // Logical AND
  X64_NOT,  // Logical NOT (One's Complement)
  X64_SHL,  // Shift Left
  X64_SHR,  // Shift Right (Logical)
  X64_SAL,  // Shift Arithmetic Left
  X64_SAR,  // Shift Arithmetic Right

  // Control Flow Instructions
  X64_JMP,    // Jump
  X64_JZ,     // Jump if Zero
  X64_JE,     // Jump if Equal
  X64_JNE,    // Jump if Not Equal
  X64_JNG,    // Jump if Not Greater
  X64_JNGE,   // Jump if Not Greater or Equal
  X64_CALL,   // Call Procedure
  X64_RET,    // Return from Procedure
  X64_LEAVE,  // High Level Procedure Exit

  // Comparison and Conditional Instructions
  X64_CMP,    // Compare
  X64_SETE,   // Set byte if Equal
  X64_SETNE,  // Set byte if Not Equal
  X64_SETL,   // Set byte if Less (signed)
  X64_SETB,   // Set byte if Less (unsigned)
  X64_SETLE,  // Set byte if Less or Equal (signed)
  X64_SETBE,  // Set byte if Less or Equal (unsigned)

  // Other
  X64_LABEL,  // Assembler Directive

  // Virtual instruction
  X64_RETURN,       // Return instruction: expands to `leave`
                    // (if the function uses a stack frame) followed by `ret`
  X64_BUILTIN_ASM,  // __asm__ Note: expression operands are not allowed
} X64_ASMKind;

typedef enum
{
  OP_RESERVED,
  OP_REG,
  OP_IMM,
  OP_MEM,
  OP_MEM_RELATIVE,  // rip relative addr
} X64_OperandKind;

typedef struct
{
  enum
  {
    regs_reserved,
    virtual_regs,
    real_regs,
  } reg_type;
  OperandSize size;
  size_t reg_id;  // Each virtual_reg and real_reg has a unique number (ID)
  struct
  {
    enum register_name real_reg;
    bool is_reserved;
  };
} X64_REG;

typedef struct
{
  X64_OperandKind kind;
  union
  {
    X64_REG* reg;
    long long imm;
    struct
    {
      // [base + displacement + index * scale]
      union
      {
        X64_REG* base;  // OP_MEM
        struct          // OP_MEM_RELATIVE
        {
          char* var_name;
          size_t var_name_len;
        };
      };
      int displacement;  // 32bit
      X64_REG* index;
      OperandSize scale;
      OperandSize mov_size;
    } mem;
  };
} X64_Operand;

// TODO: IMUL 3 operand instruction
#define MAX_OPERANDS 2

typedef struct
{
  X64_ASMKind kind;
  union
  {
    struct
    {
      X64_Operand operands[MAX_OPERANDS];
      char* jump_target_label;
      // Bitmask: the bit corresponding to enum register_name
      // 1 if the register is in use, 0 if unused
      unsigned int implicit_used_registers;  // 32bit
    };
    struct
    {
      size_t asm_len;
      char* asm_str;
    } builtin_asm;
  };
} X64_ASM;

typedef struct X64_Blocks
{
  Vector* asm_list;
  Vector* parent;
  struct X64_Blocks* lhs;  // child
  struct X64_Blocks* rhs;  // child
  Vector* in;              // X64_REG
  Vector* out;             // X64_REG
} X64_Blocks;

typedef struct
{
  // Bitmask: the bit corresponding to enum register_name
  // 1 if the register is in use, 0 if unused
  unsigned int used_registers;  // 32bit
  Vector* asm_blocks;           // X64_Blocks
  char* function_name;
  size_t function_name_size;
  bool is_static;
  bool stack_used;
  size_t stack_size;        // Total stack size for local variables
  size_t num_virtual_regs;  // Number of virtual registers used
  Vector* virtual_regs;  // array of X64_REG Access with IR register_number + 1
} X64_FUNC;

void generate_x64(IRFunc* program);

#endif