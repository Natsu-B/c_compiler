#include "include/generator_x64.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#endif

#include "include/builtin.h"
#include "include/common.h"
#include "include/error.h"
#include "include/generator.h"
#include "include/vector.h"

// // Registers used for general purpose, arguments, and return values
// static char *regs_64[] = {"rdi", "rsi", "rdx", "rcx", "r8",  "r9",
//                           "r10", "r11", "rax", "rbx", "rsp", "rbp",
//                           "r12", "r13", "r14", "r15"};
// static char *regs_32[] = {"edi",  "esi",  "edx",  "ecx", "r8d", "r9d",
//                           "r10d", "r11d", "eax",  "ebx", "esp", "ebp",
//                           "r12d", "r13d", "r14d", "r15d"};
// static char *regs_16[] = {"di",   "si",   "dx",   "cx",  "r8w", "r9w",
//                           "r10w", "r11w", "ax",   "bx",  "sp",  "bp",
//                           "r12w", "r13w", "r14w", "r15w"};
// static char *regs_8[] = {"dil",  "sil",  "dl",   "cl",  "r8b", "r9b",
//                          "r10b", "r11b", "al",   "bl",  "spl", "bpl",
//                          "r12b", "r13b", "r14b", "r15b"};

X64_Blocks *new_block()
{
  X64_Blocks *new = calloc(1, sizeof(X64_Blocks));
  new->asm_list = vector_new();
  new->parent = vector_new();
  new->in = vector_new();
  new->out = vector_new();
  return new;
}

X64_ASM *new_asm()
{
  return calloc(1, sizeof(X64_ASM));
}

X64_REG *set_reserved_real_regs(X64_REG *reg, enum register_name name,
                                OperandSize size)
{
  reg->reg_type = real_regs;
  reg->size = size;
  reg->real_reg = name;
  reg->is_reserved = true;
  return reg;
}

void set_regs(X64_Operand *op, X64_REG *reg)
{
  op->kind = OP_REG;
  op->reg = reg;
}

X64_REG *search_regs(X64_FUNC *func, IR_REG *reg)
{
  return vector_peek_at(func->virtual_regs, reg->reg_num + 1);
}

void push_regs(X64_FUNC *func, size_t register_num, X64_REG *push_reg)
{
  vector_replace_at(func->virtual_regs, register_num + 1, push_reg);
}

// search Vector and create X64_REG if not found
X64_REG *search_and_create_regs(X64_FUNC *func, IR_REG *reg)
{
  X64_REG *ptr = search_regs(func, reg);
  if (!ptr)
  {
    ptr = calloc(1, sizeof(X64_REG));
    push_regs(func, reg->reg_num, ptr);
  }
  return ptr;
}

void assign_virtual_regs(X64_FUNC *func, X64_Operand *op, IR_REG *reg)
{
  op->kind = OP_REG;
  X64_REG *virtual_reg = search_and_create_regs(func, reg);
  if (virtual_reg->reg_type == regs_reserved)
  {  // uninitialized
    virtual_reg->reg_type = virtual_regs;
    virtual_reg->size = reg->reg_size;
    virtual_reg->reg_id = reg->reg_num;
  }
  else
    unreachable();
  op->reg = virtual_reg;
}

void generate_x64_asm(X64_FUNC *func, X64_Blocks *blocks, IR *ir)
{
  switch (ir->kind)
  {
    case IR_CALL:
    {
      unimplemented();
    }
    break;
    case IR_FUNC_PROLOGUE: break;
    case IR_FUNC_EPILOGUE:
    {
      if (func->function_name_size == 4 &&
          !strncmp(func->function_name, "main", 4))
      {  // implicit return 0 when function name is main
        X64_ASM *new = new_asm();
        new->kind = X64_MOV;
        // source reg
        new->operands[1].kind = OP_IMM;
        new->operands[1].imm = 0;
        set_regs(&new->operands[0],
                 set_reserved_real_regs(calloc(1, sizeof(X64_REG)), rax,
                                        SIZE_DWORD));  // int size
        new->implicit_used_registers = 1 << rax;
        vector_push(blocks->asm_list, new);
      }
      X64_ASM *leave = new_asm();
      leave->kind = X64_RETURN;
      vector_push(blocks->asm_list, leave);
    }
    break;
    case IR_RET:
    {
      if (!ir->ret.return_void)
      {
        X64_REG *ret_reg = search_and_create_regs(func, ir->ret.src_reg);
        if (ret_reg->reg_type == real_regs && ret_reg->is_reserved &&
            ret_reg->real_reg != rax)
        {                            // already reserved
          X64_ASM *mov = new_asm();  // separate virtual regs
          mov->kind = X64_MOV;
          set_regs(&mov->operands[0],
                   set_reserved_real_regs(calloc(1, sizeof(X64_REG)), rax,
                                          ir->ret.src_reg->reg_size));
          set_regs(&mov->operands[1],
                   set_reserved_real_regs(calloc(1, sizeof(X64_REG)),
                                          ret_reg->real_reg, ret_reg->size));
          mov->implicit_used_registers = 1 << rax;
          vector_push(blocks->asm_list, mov);
        }
        else
        {
          ret_reg->reg_type = real_regs;
          ret_reg->size = ir->ret.src_reg->reg_size;
          ret_reg->real_reg = rax;
          ret_reg->is_reserved = true;
        }
      }
      X64_ASM *leave = new_asm();
      leave->kind = X64_RETURN;
      vector_push(blocks->asm_list, leave);
    }
    break;
    case IR_BUILTIN_ASM:
    {
      X64_ASM *builtin_asm = new_asm();
      builtin_asm->builtin_asm.asm_len = ir->builtin_asm.asm_len;
      builtin_asm->builtin_asm.asm_str = parse_string_literal(
          ir->builtin_asm.asm_str, &builtin_asm->builtin_asm.asm_len);
      vector_push(blocks->asm_list, builtin_asm);
    }
    break;
    case IR_BUILTIN_VA_LIST:
    {
      unimplemented();
    }
    break;
    case IR_BUILTIN_VA_ARGS:
    {
      unimplemented();
    }
    break;
    case IR_MOV:
    {
      X64_ASM *new = new_asm();
      new->kind = X64_MOV;
      assign_virtual_regs(func, &new->operands[0], ir->mov.dst_reg);
      if (ir->mov.is_imm)
      {
        new->operands[1].kind = OP_IMM;
        new->operands[1].imm = ir->mov.imm_val;
      }
      else
        assign_virtual_regs(func, &new->operands[1], ir->mov.src_reg);
      vector_push(blocks->asm_list, new);
    }
    break;
    case IR_ADD:
    case IR_SUB:
    case IR_MUL:
    case IR_MULU:
    case IR_AND:
    case IR_OR:
    case IR_SHL:
    case IR_SHR:
    case IR_SAL:
    case IR_SAR:
    {
      X64_ASM *new = new_asm();
      switch (ir->kind)
      {
        case IR_ADD: new->kind = X64_ADD; break;
        case IR_SUB: new->kind = X64_SUB; break;
        case IR_MUL:
        case IR_MULU: new->kind = X64_IMUL; break;
        case IR_AND: new->kind = X64_AND; break;
        case IR_OR: new->kind = X64_OR; break;
        case IR_SHL: new->kind = X64_SHL; break;
        case IR_SHR: new->kind = X64_SHR; break;
        case IR_SAL: new->kind = X64_SAL; break;
        case IR_SAR: new->kind = X64_SAR; break;
        default: unreachable(); return;
      }
      X64_REG *dst_reg = search_regs(func, ir->bin_op.dst_reg);
      X64_REG *lhs_reg = search_regs(func, ir->bin_op.lhs_reg);
      X64_REG *rhs_reg = search_regs(func, ir->bin_op.rhs_reg);
      if (!lhs_reg || !rhs_reg)
        unreachable();
      if (!dst_reg)
      {
        if (lhs_reg->reg_type == virtual_regs)  // dst_reg = lhs_reg
        {
          push_regs(func, ir->bin_op.dst_reg->reg_num, lhs_reg);
          set_regs(&new->operands[1], rhs_reg);
        }
        else  // dst_reg = rhs_reg
        {
          push_regs(func, ir->bin_op.dst_reg->reg_num, rhs_reg);
          set_regs(&new->operands[1], lhs_reg);
        }
      }
      else
        unreachable();
      set_regs(&new->operands[0], dst_reg);
      vector_push(blocks->asm_list, new);
    }
    break;
    case IR_DIV:
    case IR_DIVU:
    case IR_REM:
    case IR_REMU:
    {  // implicit use rdx, and rax
      // convert QuadWord to OctoWord
      X64_ASM *cqo = new_asm();
      cqo->kind = X64_CQO;
      cqo->implicit_used_registers = 1 << rax | 1 << rdx;
      X64_REG *lhs_reg = search_regs(func, ir->bin_op.lhs_reg);
      X64_REG *rhs_reg = search_regs(func, ir->bin_op.rhs_reg);
      if (!lhs_reg || !rhs_reg)
        unreachable();
      if (lhs_reg->reg_type == virtual_regs)
      {
        lhs_reg->reg_type = real_regs;
        lhs_reg->real_reg = rax;
        lhs_reg->is_reserved = true;
      }
      else
        unimplemented();
      vector_push(blocks->asm_list, cqo);
      // div
      X64_ASM *div = new_asm();
      switch (ir->kind)
      {
        case IR_DIV:
        case IR_REM: div->kind = X64_DIV; break;
        case IR_DIVU:
        case IR_REMU: div->kind = X64_IDIV; break;
        default: unreachable(); break;
      }
      div->implicit_used_registers = 1 << rax | 1 << rdx;
      set_regs(&div->operands[0], rhs_reg);
      vector_push(blocks->asm_list, div);
      X64_REG *dst_reg = search_and_create_regs(func, ir->bin_op.dst_reg);
      if (dst_reg->reg_type != regs_reserved)
        unreachable();
      switch (ir->kind)
      {
        case IR_DIV:
        case IR_DIVU:
          set_reserved_real_regs(dst_reg, rax, ir->bin_op.dst_reg->reg_size);
          break;
        case IR_REM:
        case IR_REMU:
          set_reserved_real_regs(dst_reg, rdx, ir->bin_op.dst_reg->reg_size);
          break;
        default: unreachable(); break;
      }
    }
    break;
    case IR_EQ:
    case IR_NEQ:
    case IR_LT:
    case IR_LTU:
    case IR_LTE:
    case IR_LTEU:
    {
      X64_ASM *cmp = new_asm();
      cmp->kind = X64_CMP;
      X64_REG *lhs_reg = search_regs(func, ir->bin_op.lhs_reg);
      X64_REG *rhs_reg = search_regs(func, ir->bin_op.rhs_reg);
      if (!lhs_reg || !rhs_reg)
        unreachable();
      set_regs(&cmp->operands[0], lhs_reg);
      set_regs(&cmp->operands[1], rhs_reg);
      vector_push(blocks->asm_list, cmp);
      // zero clear
      X64_ASM *xor = new_asm();
      xor->kind = X64_XOR;
      assign_virtual_regs(func, &xor->operands[0], ir->bin_op.dst_reg);
      set_regs(&xor->operands[1], xor->operands[0].reg);
      vector_push(blocks->asm_list, xor);
      X64_ASM *set = new_asm();
      switch (ir->kind)
      {
        case IR_EQ: set->kind = X64_SETE; break;
        case IR_NEQ: set->kind = X64_SETNE; break;
        case IR_LT: set->kind = X64_SETL; break;
        case IR_LTU: set->kind = X64_SETB; break;
        case IR_LTE: set->kind = X64_SETLE; break;
        case IR_LTEU: set->kind = X64_SETBE; break;
        default: unreachable(); break;
      }
      set_regs(&set->operands[0], xor->operands[1].reg);
      vector_push(blocks->asm_list, set);
    }
    break;
    case IR_JMP:
    case IR_JNE:
    case IR_JE:
    {
      if (ir->kind != IR_JMP)
      {
        X64_ASM *cmp = new_asm();
        cmp->kind = X64_CMP;
        X64_REG *cond = search_regs(func, ir->jmp.cond_reg);
        if (!cond)
          unreachable();
        set_regs(&cmp->operands[0], cond);
        cmp->operands[1].kind = OP_IMM;
        cmp->operands[1].imm = 0;
        vector_push(blocks->asm_list, cmp);
      }
      X64_ASM *jmp = new_asm();
      switch (ir->kind)
      {
        case IR_JMP: jmp->kind = X64_JMP; break;
        case IR_JNE: jmp->kind = X64_JNE; break;
        case IR_JE: jmp->kind = X64_JE; break;
        default: unreachable(); break;
      }
      jmp->jump_target_label = ir->jmp.label;
      vector_push(blocks->asm_list, jmp);
    }
    break;
    case IR_LOAD:
    case IR_STORE:
    {
      X64_ASM *mov = new_asm();
      mov->kind = X64_MOV;
      X64_REG *mem = search_regs(func, ir->mem.mem_reg);
      if (!mem)
        unreachable();
      X64_Operand *op =
          ir->kind == IR_LOAD ? &mov->operands[1] : &mov->operands[0];
      op->kind = OP_MEM;
      op->mem.base = mem;
      op->mem.displacement = ir->mem.offset;
      op->mem.mov_size = ir->mem.size;

      assign_virtual_regs(
          func, ir->kind == IR_LOAD ? &mov->operands[0] : &mov->operands[1],
          ir->mem.reg);
      vector_push(blocks->asm_list, mov);
    }
    break;
    case IR_STORE_ARG:
    {
      unimplemented();
    }
    break;
    case IR_LEA:
    {
      X64_ASM *lea = new_asm();
      lea->kind = X64_LEA;
      assign_virtual_regs(func, &lea->operands[0], ir->lea.dst_reg);
      if (ir->lea.is_local)
      {
        lea->operands[1].kind = OP_MEM;
        lea->operands[1].mem.base =
            set_reserved_real_regs(calloc(1, sizeof(X64_REG)), rbp, SIZE_QWORD);
      }
      else
      {
        lea->operands[1].kind = OP_MEM_RELATIVE;
        lea->operands[1].mem.var_name = ir->lea.var_name;
        lea->operands[1].mem.var_name_len = ir->lea.var_name_len;
      }
      lea->operands[1].mem.displacement = ir->lea.var_offset;
      lea->operands[1].mem.mov_size = SIZE_DWORD;
      vector_push(blocks->asm_list, lea);
    }
    break;
    case IR_SIGN_EXTEND:
    case IR_ZERO_EXTEND:
    {
      unimplemented();
    }
    break;
    case IR_TRUNCATE: break;
    case IR_XOR:
    case IR_BIT_NOT:
    case IR_NEG:
    {
      X64_ASM *op = new_asm();
      switch (ir->kind)
      {
        case IR_XOR: op->kind = X64_XOR; break;
        case IR_BIT_NOT: op->kind = X64_NOT; break;
        case IR_NEG: op->kind = X64_NEG; break;
        default: unreachable(); break;
      }
      X64_REG *reg = search_regs(func, ir->un_op.src_reg);
      if (!reg)
        unreachable();
      push_regs(func, ir->un_op.dst_reg->reg_num, reg);
      set_regs(&op->operands[0], reg);
      vector_push(blocks->asm_list, op);
    }
    break;
    case IR_NOT:
    {
      X64_ASM *cmp = new_asm();
      cmp->kind = X64_CMP;
      X64_REG *cond = search_regs(func, ir->jmp.cond_reg);
      if (!cond)
        unreachable();
      set_regs(&cmp->operands[0], cond);
      cmp->operands[1].kind = OP_IMM;
      cmp->operands[1].imm = 0;
      vector_push(blocks->asm_list, cmp);
      // zero clear
      X64_ASM *xor = new_asm();
      xor->kind = X64_XOR;
      assign_virtual_regs(func, &xor->operands[0], ir->un_op.dst_reg);
      set_regs(&xor->operands[1], xor->operands[0].reg);
      vector_push(blocks->asm_list, xor);
      X64_ASM *set = new_asm();
      set->kind = X64_SETE;
      set_regs(&set->operands[0], xor->operands[1].reg);
      vector_push(blocks->asm_list, set);
    }
    break;
    case IR_PHI:
    {
      X64_REG *lhs_reg = search_regs(func, ir->phi.lhs_reg);
      X64_REG *rhs_reg = search_regs(func, ir->phi.rhs_reg);
      if (lhs_reg->reg_type == virtual_regs)
        memcpy(lhs_reg, rhs_reg, sizeof(X64_REG));
      else if (rhs_reg->reg_type == virtual_regs)
        memcpy(rhs_reg, lhs_reg, sizeof(X64_REG));
      else
        unimplemented();
      push_regs(func, ir->phi.dst_reg->reg_num, rhs_reg);
    }
    break;
    case IR_LABEL:
    {
      X64_ASM *label = new_asm();
      label->kind = X64_LABEL;
      label->jump_target_label = ir->label.name;
      vector_push(blocks->asm_list, label);
    }
    break;
  }
}

void generate_x64_block(X64_FUNC *func, IR_Blocks *ir_block)
{
  X64_Blocks *new = new_block();
  for (size_t i = 1; i <= vector_size(ir_block->IRs); i++)
    generate_x64_asm(func, new, vector_peek_at(ir_block->IRs, i));
}

void generate_func_x64(IRFunc *func)
{
  X64_FUNC func_x64;
  func_x64.used_registers = 0;
  func_x64.asm_blocks = vector_new();
  func_x64.function_name = func->user_defined.function_name;
  func_x64.function_name_size = func->user_defined.function_name_size;
  func_x64.is_static = func->user_defined.is_static;
  func_x64.stack_used = false;
  func_x64.stack_size = func->user_defined.stack_size;
  func_x64.num_virtual_regs = 0;
  func_x64.virtual_regs =
      vector_allocate(vector_size(func->user_defined.num_virtual_regs));

  for (size_t i = 1; i <= vector_size(func->IR_Blocks); i++)
    generate_x64_block(&func_x64, vector_peek_at(func->IR_Blocks, i));
}

void generate_x64(IRFunc *func)
{
  switch (func->builtin_func)
  {
    case FUNC_USER_DEFINED:
    {
      generate_func_x64(func);
      break;
    }
    case FUNC_ASM:
    {
      // IR_Blocks *block = vector_pop(func->IR_Blocks);
      unimplemented();
      break;
    }
  }
}