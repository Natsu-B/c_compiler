// ------------------------------------------------------------------------------------
// generator
// ------------------------------------------------------------------------------------

#include "include/generator.h"

#include "include/common.h"
#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#include "include/builtin.h"
#include "include/error.h"
#include "include/ir_generator.h"
#include "include/type.h"
#include "include/vector.h"

#define error_exit_with_guard(fmt, ...)                                     \
  do                                                                        \
  {                                                                         \
    output_file("error detected at %s:%d:%s() exit...", __FILE__, __LINE__, \
                __func__);                                                  \
    error_exit(fmt, ##__VA_ARGS__);                                         \
  } while (0)

#if DEBUG
#define output_debug(fmt, ...) \
  output_file("# %s:%d:%s()" fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__);
#define output_debug2(fmt, ...) \
  output_file("# %s:%d:%s()" fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__);
#elif defined(DEBUG)
#define output_debug(fmt, ...) output_file("# " fmt, ##__VA_ARGS__);
#define output_debug2(fmt, ...) output_file("# " fmt, ##__VA_ARGS__);
#else
#define output_debug(fmt, ...)
#define output_debug2(fmt, ...)
#endif

static FILE *fout;

#define output_file(fmt, ...)               \
  do                                        \
  {                                         \
    fprintf(fout, fmt "\n", ##__VA_ARGS__); \
  } while (0)

// Creates the access size specifier
char *access_size_specifier(int size)
{
  char *tmp;
  switch (size)
  {
    case 1: tmp = "BYTE PTR"; break;
    case 2: tmp = "WORD PTR"; break;
    case 4: tmp = "DWORD PTR"; break;
    case 8: tmp = "QWORD PTR"; break;
    default: error_exit("unknown access size specifier: %d", size); break;
  }
  return tmp;
}

enum register_name
{
  rdi,
  rsi,
  rdx,
  rcx,
  r8,
  r9,
  rax,
  rbx,
  rsp,
  rbp,
  r10,
  r11,
  r12,
  r13,
  r14,
  r15,
};

// Registers used for general purpose, arguments, and return values
static char *regs_64[] = {"rdi", "rsi", "rdx", "rcx", "r8",  "r9",
                          "rax", "rbx", "rsp", "rbp", "r10", "r11",
                          "r12", "r13", "r14", "r15"};
static char *regs_32[] = {"edi",  "esi",  "edx",  "ecx", "r8d",  "r9d",
                          "eax",  "ebx",  "esp",  "ebp", "r10d", "r11d",
                          "r12d", "r13d", "r14d", "r15d"};
static char *regs_16[] = {"di",   "si",   "dx",   "cx",  "r8w",  "r9w",
                          "ax",   "bx",   "sp",   "bp",  "r10w", "r11w",
                          "r12w", "r13w", "r14w", "r15w"};
static char *regs_8[] = {"dil",  "sil",  "dl",   "cl",  "r8b",  "r9b",
                         "al",   "bl",   "spl",  "bpl", "r10b", "r11b",
                         "r12b", "r13b", "r14b", "r15b"};

size_t function_variables_stack_size;
size_t function_max_stack_size;

// Simple stack-based allocation: all virtual registers are on the stack.
// Get the stack offset for a given virtual register.
static int get_stack_offset(int virtual_reg)
{
  // +1 because rbp points to the old rbp, stack grows downwards.
  if (function_variables_stack_size + (virtual_reg + 1) * 8 >
      function_max_stack_size)
    unreachable();
  return function_variables_stack_size + (virtual_reg + 1) * 8;
}

char *get_physical_reg_name(int reg_type, int size)
{
  // This function is now repurposed to get argument register names
  // A better name would be get_arg_reg_name
  switch (size)
  {
    case 1: return regs_8[reg_type];
    case 2: return regs_16[reg_type];
    case 4: return regs_32[reg_type];
    case 8: return regs_64[reg_type];
    default: error_exit_with_guard("Unsupported register size: %d", size);
  }
  return NULL;  // Should not reach here
}

int call_id = 0;  // function call id

// Function to generate x64 assembly for a single IR instruction
void gen_ir_instruction(IR *ir)
{
  // In this simple model, most operations use rax and rcx as scratch registers.
  switch (ir->kind)
  {
    case IR_MOV:
    {
      if (ir->mov.is_imm)
      {
        output_file("    mov rax, %lld", ir->mov.imm_val);
      }
      else
      {
        output_file("    mov rax, [rbp-%d]", get_stack_offset(ir->mov.src_reg));
      }
      output_file("    mov [rbp-%d], rax", get_stack_offset(ir->mov.dst_reg));
      break;
    }
    case IR_ADD:
    case IR_SUB:
    case IR_MUL:
    case IR_OR:
    case IR_XOR:
    case IR_AND:
    case IR_SHL:
    case IR_SHR:
    case IR_SAL:
    case IR_SAR:
    case IR_OP_DIV:
    case IR_OP_IDIV:
    case IR_EQ:
    case IR_NEQ:
    case IR_LT:
    case IR_LTE:
    {
      output_file("    mov rax, [rbp-%d]",
                  get_stack_offset(ir->bin_op.lhs_reg));
      output_file("    mov rcx, [rbp-%d]",
                  get_stack_offset(ir->bin_op.rhs_reg));

      switch (ir->kind)
      {
        case IR_ADD: output_file("    add rax, rcx"); break;
        case IR_SUB: output_file("    sub rax, rcx"); break;
        case IR_MUL: output_file("    imul rax, rcx"); break;
        case IR_OR: output_file("    or rax, rcx"); break;
        case IR_XOR: output_file("    xor rax, rcx"); break;
        case IR_AND: output_file("    and rax, rcx"); break;
        case IR_SHL:
          output_file("    shl %s, cl",
                      get_physical_reg_name(rax, ir->bin_op.lhs_size));
          break;
        case IR_SHR:
          output_file("    shr %s, cl",
                      get_physical_reg_name(rax, ir->bin_op.lhs_size));
          break;
        case IR_SAL:
          output_file("    sal %s, cl",
                      get_physical_reg_name(rax, ir->bin_op.lhs_size));
          break;
        case IR_SAR:
          output_file("    sar %s, cl",
                      get_physical_reg_name(rax, ir->bin_op.lhs_size));
          break;
        case IR_OP_DIV:
        case IR_OP_IDIV:
          output_file("    cqo");  // Sign-extend RAX to RDX:RAX
          output_file("    idiv rcx");
          if (ir->kind == IR_OP_IDIV)
          {
            output_file("    mov rax, rdx");  // Remainder in RDX
          }
          break;
        case IR_EQ:
          output_file("    cmp rax, rcx");
          output_file("    sete al");
          output_file("    movzx rax, al");
          break;
        case IR_NEQ:
          output_file("    cmp rax, rcx");
          output_file("    setne al");
          output_file("    movzx rax, al");
          break;
        case IR_LT:
          output_file("    cmp rax, rcx");
          output_file("    setl al");
          output_file("    movzx rax, al");
          break;
        case IR_LTE:
          output_file("    cmp rax, rcx");
          output_file("    setle al");
          output_file("    movzx rax, al");
          break;
        default:
          error_exit_with_guard("Unhandled binary operation IR kind: %d",
                                ir->kind);
      }
      output_file("    mov [rbp-%d], rax",
                  get_stack_offset(ir->bin_op.dst_reg));
      break;
    }
    case IR_NEG:
    {
      output_file("    mov rax, [rbp-%d]", get_stack_offset(ir->un_op.src_reg));
      output_file("    neg rax");
      output_file("    mov [rbp-%d], rax", get_stack_offset(ir->un_op.dst_reg));
      break;
    }
    case IR_BIT_NOT:
    {
      output_file("    mov rax, [rbp-%d]", get_stack_offset(ir->un_op.src_reg));
      output_file("    not rax");
      output_file("    mov [rbp-%d], rax", get_stack_offset(ir->un_op.dst_reg));
      break;
    }
    case IR_RET:
    {
      output_file("    mov rax, [rbp-%d]", get_stack_offset(ir->ret.src_reg));
      output_file("    leave");
      output_file("    ret");
      break;
    }
    case IR_FUNC_PROLOGUE:
    case IR_FUNC_EPILOGUE:
      // These are handled in the main generator loop.
      break;
    case IR_CALL:
    {
      size_t num_args = vector_size(ir->call.args);
      size_t stack_arg_count = (num_args > 6) ? (num_args - 6) : 0;

      // Load arguments 1-6 into registers
      for (size_t i = 0; i < (num_args < 6 ? num_args : 6); i++)
      {
        int arg_vreg = *(int *)vector_peek_at(ir->call.args, i + 1);
        output_file("    mov %s, [rbp-%d]", get_physical_reg_name(i, 8),
                    get_stack_offset(arg_vreg));
      }

      // Align stack to 16 bytes before call
      output_file("    mov rax, rsp");
      output_file("    and rax, 15");
      output_file("    push r12");
      output_file("    mov r12, 0");
      output_file("    jz .Lcall_aligned_%d",
                  call_id);  // Use a unique id for the label
      output_file("    mov r12, 8");
      output_file("    sub rsp, 8");
      output_file(".Lcall_aligned_%d:", call_id);
      // Push arguments 7+ onto the stack in reverse order
      if (stack_arg_count)
      {
        for (size_t i = 0; i < stack_arg_count; i++)
        {
          int arg_vreg = *(int *)vector_peek_at(ir->call.args, num_args - i);
          output_file("    push QWORD PTR [rbp-%d]",
                      get_stack_offset(arg_vreg));
        }
      }
      output_file("    mov rax, 0");  // Number of XMM registers used
      output_file("    call %.*s", (int)ir->call.func_name_size,
                  ir->call.func_name);
      output_file("    add rsp, r12");
      if (stack_arg_count)  // Clean up stack arguments
        output_file("    add rsp, %lu", stack_arg_count * 8);
      output_file("    pop r12");
      call_id++;
      // Store return value
      output_file("    mov [rbp-%d], rax", get_stack_offset(ir->call.dst_reg));
      break;
    }
    case IR_JMP:
    {
      output_file("    jmp %s", ir->jmp.label);
      break;
    }
    case IR_JE:
    case IR_JNE:
    {
      output_file("    mov rax, [rbp-%d]", get_stack_offset(ir->jmp.cond_reg));
      output_file("    cmp rax, 0");
      if (ir->kind == IR_JE)
        output_file("    je %s", ir->jmp.label);
      else
        output_file("    jne %s", ir->jmp.label);
      break;
    }
    case IR_LABEL:
    {
      output_file("%s:", ir->label.name);
      break;
    }
    case IR_LOAD:
    {
      // Get address from memory register's stack slot
      output_file("    mov rax, [rbp-%d]", get_stack_offset(ir->mem.mem_reg));
      // Load value from that address
      // Note: The size of the value loaded is determined by the size of the
      // register we move it to. We use rax for 8-byte, eax for 4-byte etc. A
      // more robust implementation would use different registers based on size.
      switch (ir->mem.size)
      {
        case 1:
          output_file("    movsx rax, BYTE PTR [rax + %d]", ir->mem.offset);
          break;
        case 2:
          output_file("    movsx rax, WORD PTR [rax + %d]", ir->mem.offset);
          break;
        case 4:
          output_file("    movsxd rax, DWORD PTR [rax + %d]", ir->mem.offset);
          break;
        case 8:
          output_file("    mov rax, QWORD PTR [rax + %d]", ir->mem.offset);
          break;
        default:
          error_exit_with_guard("Unsupported load size: %d", ir->mem.size);
      }
      // Store it in the destination register's stack slot
      output_file("    mov [rbp-%d], rax", get_stack_offset(ir->mem.reg));
      break;
    }
    case IR_STORE:
    {
      // Get address from memory register's stack slot
      output_file("    mov rax, [rbp-%d]", get_stack_offset(ir->mem.mem_reg));
      // Get value from source register's stack slot
      output_file("    mov rcx, [rbp-%d]", get_stack_offset(ir->mem.reg));
      // Store value at the address
      output_file("    mov [rax + %d], %s", ir->mem.offset,
                  get_physical_reg_name(rcx, ir->mem.size));
      break;
    }
    case IR_LEA:
    {
      if (ir->lea.is_local)
        output_file("    lea rax, [rbp-%lu]", ir->lea.var_offset);
      else
        output_file("    lea rax, [rip+%.*s]", (int)ir->lea.var_name_len,
                    ir->lea.var_name);
      output_file("    mov [rbp-%d], rax", get_stack_offset(ir->lea.dst_reg));
      break;
    }
    case IR_STORE_ARG:
    {
      if (ir->store_arg.arg_index >= 6)
      {
        // Arguments 7+ are on the caller's stack.
        // Calculate offset: 16 (for ret addr and old rbp) + (index - 6) * 8
        int offset = 16 + (ir->store_arg.arg_index - 6) * 8;
        // Move argument from caller's stack to rax
        output_file("    mov rax, [rbp+%d]", offset);
        // Get the address of the local variable (where we store the arg) into
        // rcx
        output_file("    mov rcx, [rbp-%d]",
                    get_stack_offset(ir->store_arg.dst_reg));
        // Store the argument value into the local variable's address
        output_file("    mov [rcx], %s",
                    get_physical_reg_name(rax, ir->store_arg.size));
      }
      else
      {
        // Arguments 1-6 are in registers.
        // Get the address of the local variable from its stack slot into rax
        output_file("    mov rax, [rbp-%d]",
                    get_stack_offset(ir->store_arg.dst_reg));
        // Store the argument register's value into that address
        output_file(
            "    mov [rax], %s",
            get_physical_reg_name(ir->store_arg.arg_index, ir->store_arg.size));
      }
      break;
    }
    case IR_BUILTIN_ASM:
    {
      size_t len = ir->builtin_asm.asm_len;
      char *str = parse_string_literal(ir->builtin_asm.asm_str, &len);
      output_file("%.*s", (int)len, str);
      break;
    }
    // TODO: Implement other IR kinds (bitwise, unary, builtins)
    default: error_exit_with_guard("Unhandled IR kind: %d", ir->kind);
  }
}

void generator(IRProgram *program, char *output_filename)
{
  pr_debug("start generator");

  pr_debug("output filename: %.*s", strlen(output_filename), output_filename);
  fout = fopen(output_filename, "w");
  if (fout == NULL)
    error_exit("cannot written to file");
  pr_debug("output file open");
  output_file("    .intel_syntax noprefix\n");

  // Global variables
  if (vector_size(program->global_vars))
  {
    for (size_t i = 0; i < vector_size(program->global_vars); i++)
    {
      GlobalVar *gvar = vector_peek_at(program->global_vars, i + 1);
      if (gvar->how2_init == init_zero)
        output_file("    .section .bss");
      else
        output_file("    .section .data");
      if (!gvar->is_static)
        output_file("    .globl %.*s", (int)gvar->var_name_len, gvar->var_name);
      output_file("    .type %.*s, @object", (int)gvar->var_name_len,
                  gvar->var_name);
      output_file("    .size %.*s, %lu", (int)gvar->var_name_len,
                  gvar->var_name, gvar->var_size);
      output_file("%.*s:", (int)gvar->var_name_len, gvar->var_name);
      switch (gvar->how2_init)
      {
        case init_zero: output_file("    .zero %zu", gvar->var_size); break;
        case init_val:
          switch (gvar->var_size)
          {
            case 1: output_file("    .byte %lld", gvar->init_val); break;
            case 2: output_file("    .word %lld", gvar->init_val); break;
            case 4: output_file("    .long %lld", gvar->init_val); break;
            case 8: output_file("    .quad %lld", gvar->init_val); break;
            default: unreachable();
          }
          break;
        case init_pointer:
          output_file("    .quad %.*s", (int)gvar->assigned_var.var_name_len,
                      gvar->assigned_var.var_name);
          break;
        case init_string:
          output_file("    .quad %s", gvar->literal_name);
          break;
        default: unreachable(); break;
      }
    }
  }

  // strings
  if (vector_size(program->strings))
  {
    output_file("    .section .rodata");
    for (size_t i = 1; i <= vector_size(program->strings); i++)
    {
      Var *string = vector_peek_at(program->strings, i);
      output_file("%.*s:", (int)string->len, string->name);
      output_file("    .string \"%.*s\"", (int)string->token->len,
                  string->token->str);
    }
  }

  // Write functions
  for (size_t i = 0; i < vector_size(program->functions); i++)
  {
    IRFunc *func = vector_peek_at(program->functions, i + 1);
    switch (func->builtin_func)
    {
      case FUNC_USER_DEFINED:
      {
        output_file("\n    .text\n");
        if (!func->user_defined.is_static)
          output_file("    .global %.*s",
                      (int)func->user_defined.function_name_size,
                      func->user_defined.function_name);
        output_file("    .type   %.*s, @function",
                    (int)func->user_defined.function_name_size,
                    func->user_defined.function_name);
        output_file("%.*s:", (int)func->user_defined.function_name_size,
                    func->user_defined.function_name);

        // Function prologue
        output_file("    push rbp");
        output_file("    mov rbp, rsp");
        // Allocate space for all virtual registers and local variables on the
        // stack Align to 16 bytes
        size_t stack_size = (func->user_defined.num_virtual_regs * 8) +
                            func->user_defined.stack_size;
        stack_size = (stack_size + 15) & ~15;
        output_file("    sub rsp, %lu", stack_size);
        function_variables_stack_size = func->user_defined.stack_size;
        function_max_stack_size = stack_size;

        // Generate code for each IR instruction
        for (size_t j = 0; j < vector_size(func->IR_Blocks); j++)
        {
          IR *ir = vector_peek_at(func->IR_Blocks, j + 1);
          gen_ir_instruction(ir);
        }

        // Function epilogue (if not already handled by IR_RET)
        // If the last instruction is not IR_RET, add a default return.
        // This might be problematic if the function has multiple return points.
        // A better approach would be to ensure IR_RET is always the last
        // instruction of a basic block that ends a function.
        IR *last_ir =
            vector_peek_at(func->IR_Blocks, vector_size(func->IR_Blocks));
        if (last_ir->kind != IR_RET)
        {
          output_file("    leave");
          output_file("    ret");
        }
        break;
      }
      case FUNC_ASM:
      {
        gen_ir_instruction(vector_pop(func->IR_Blocks));
        break;
      }
    }
  }

  fclose(fout);
  pr_debug("Generation complete.");
}