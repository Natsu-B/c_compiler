#include "include/generator_x64.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#include <string.h>
#endif

#include "include/builtin.h"
#include "include/common.h"
#include "include/error.h"
#include "include/generator.h"
#include "include/vector.h"
#include "include/x64_liveness.h"
#include "include/x64_regalloc.h"
#include "include/x64_emit.h"
#include "include/x64_opt.h"
#include "include/x64_validate.h"

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
  {  // initialize once
    virtual_reg->reg_type = virtual_regs;
    virtual_reg->size = reg->reg_size;
    virtual_reg->reg_id = reg->reg_num;
  }
  op->reg = virtual_reg;
}

static void generate_x64_asm_no_phi(X64_FUNC *func, X64_Blocks *blocks, IR *ir)
{
  pr_debug("x64-select: IR kind=%d", ir->kind);
  switch (ir->kind)
  {
    case IR_CALL:
    {
      // Move args into ABI registers/stack then emit CALL
      size_t nargs = vector_size(ir->call.args);
      static const enum register_name abi_regs[] = {rdi, rsi, rdx, rcx, r8, r9};
      for (size_t i = 0; i < nargs && i < sizeof(abi_regs) / sizeof(abi_regs[0]); i++)
      {
        IR_REG *arg = vector_peek_at(ir->call.args, i + 1);
        X64_ASM *mov = new_asm();
        mov->kind = X64_MOV;
        // dst: fixed arg reg (use real reg to avoid null vreg metadata)
        set_regs(&mov->operands[0], calloc(1, sizeof(X64_REG)));
        mov->operands[0].reg->reg_type = real_regs;
        mov->operands[0].reg->size = arg->reg_size;
        mov->operands[0].reg->real_reg = abi_regs[i];
        // src: virtual arg reg
        assign_virtual_regs(func, &mov->operands[1], arg);
        vector_push(blocks->asm_list, mov);
      }

      // Handle stack arguments when nargs > 6 (System V AMD64 ABI)
      if (nargs > 6)
      {
        size_t stack_count = nargs - 6;
        size_t stack_bytes = stack_count * 8;  // 8-byte slots per arg
        // Maintain 16-byte alignment at call: if we add an odd number of
        // 8-byte slots, insert 8 bytes of padding so that RSP%16 remains 8
        // before the call (assuming the prologue arranged that baseline).
        size_t pad = (stack_count % 2 == 1) ? 8 : 0;

        if (stack_bytes + pad)
        {
          X64_ASM *sub = new_asm();
          sub->kind = X64_SUB;
          // dst: rsp (real reg)
          set_regs(&sub->operands[0], calloc(1, sizeof(X64_REG)));
          sub->operands[0].reg->reg_type = real_regs;
          sub->operands[0].reg->size = SIZE_QWORD;
          sub->operands[0].reg->real_reg = rsp;
          // src: immediate total stack space
          sub->operands[1].kind = OP_IMM;
          sub->operands[1].imm = (long long)(stack_bytes + pad);
          vector_push(blocks->asm_list, sub);
        }

        // Store arguments 7..N into [rsp + offset] (pad reserved above them)
        // Callee expects them at ascending addresses, so lay them contiguously.
        for (size_t i = 6; i < nargs; i++)
        {
          IR_REG *arg = vector_peek_at(ir->call.args, i + 1);
          X64_ASM *mov = new_asm();
          mov->kind = X64_MOV;
          // dst: memory [rsp + pad + (i-6)*8]
          mov->operands[0].kind = OP_MEM;
          mov->operands[0].mem.base = set_reserved_real_regs(calloc(1, sizeof(X64_REG)), rsp, SIZE_QWORD);
          mov->operands[0].mem.displacement = (int)((i - 6) * 8);
          mov->operands[0].mem.index = NULL;
          mov->operands[0].mem.scale = SIZE_QWORD;
          // Use the natural size of the argument; emitter will choose size
          mov->operands[0].mem.mov_size = arg->reg_size;
          // src: virtual arg reg
          assign_virtual_regs(func, &mov->operands[1], arg);
          vector_push(blocks->asm_list, mov);
        }
      }

      // CALL instruction
      X64_ASM *call = new_asm();
      call->kind = X64_CALL;
      // Copy function name into a NUL-terminated buffer for emission
      size_t fn_len = ir->call.func_name_size;
      char *fn = calloc(1, fn_len + 1);
      memcpy(fn, ir->call.func_name, fn_len);
      fn[fn_len] = '\0';
      call->jump_target_label = fn;
      // Clobber caller-saved regs (System V AMD64)
      unsigned int clob = 0;
      clob |= 1 << rax; clob |= 1 << rcx; clob |= 1 << rdx; clob |= 1 << rsi;
      clob |= 1 << rdi; clob |= 1 << r8;  clob |= 1 << r9;  clob |= 1 << r10;
      clob |= 1 << r11;
      call->implicit_used_registers = clob;
      vector_push(blocks->asm_list, call);

      // Move return value from RAX into dst virtual reg if needed
      if (ir->call.dst_reg)
      {
        X64_ASM *mov = new_asm();
        mov->kind = X64_MOV;
        assign_virtual_regs(func, &mov->operands[0], ir->call.dst_reg);
        set_regs(&mov->operands[1], calloc(1, sizeof(X64_REG)));
        mov->operands[1].reg->reg_type = real_regs;
        mov->operands[1].reg->size = ir->call.dst_reg->reg_size;
        mov->operands[1].reg->real_reg = rax;
        mov->implicit_used_registers = 1 << rax;
        vector_push(blocks->asm_list, mov);
      }

      // If we reserved space for stack args, reclaim it now
      if (nargs > 6)
      {
        size_t stack_count = nargs - 6;
        size_t stack_bytes = stack_count * 8;
        size_t pad = (stack_count % 2 == 1) ? 8 : 0;
        if (stack_bytes + pad)
        {
          X64_ASM *add = new_asm();
          add->kind = X64_ADD;
          // dst: rsp (real reg)
          set_regs(&add->operands[0], calloc(1, sizeof(X64_REG)));
          add->operands[0].reg->reg_type = real_regs;
          add->operands[0].reg->size = SIZE_QWORD;
          add->operands[0].reg->real_reg = rsp;
          // src: immediate total stack space to free
          add->operands[1].kind = OP_IMM;
          add->operands[1].imm = (long long)(stack_bytes + pad);
          vector_push(blocks->asm_list, add);
        }
      }
    }
    break;
    case IR_FUNC_PROLOGUE: break;
    case IR_FUNC_EPILOGUE:
    {
      // No-op: epilogue label is emitted by the backend; fallthrough reaches it
    }
    break;
    case IR_RET:
    {
      if (!ir->ret.return_void)
      {
        // Move the return value into RAX via constraint metadata
        X64_ASM *mov = new_asm();
        mov->kind = X64_MOV;
        // dst: fixed RAX of appropriate size (use real reg)
        set_regs(&mov->operands[0], calloc(1, sizeof(X64_REG)));
        mov->operands[0].reg->reg_type = real_regs;
        mov->operands[0].reg->size = ir->ret.src_reg->reg_size;
        mov->operands[0].reg->real_reg = rax;
        mov->implicit_used_registers = 1 << rax;
        // src: original virtual register
        assign_virtual_regs(func, &mov->operands[1], ir->ret.src_reg);
        vector_push(blocks->asm_list, mov);
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
    case IR_XOR:
    case IR_SHL:
    case IR_SHR:
    case IR_SAL:
    case IR_SAR:
    {
      // Ensure 2-operand form correctness: dst = lhs op rhs
      // Emit: mov dst, lhs; op dst, rhs
      X64_ASM *mv = new_asm();
      mv->kind = X64_MOV;
      assign_virtual_regs(func, &mv->operands[0], ir->bin_op.dst_reg);
      assign_virtual_regs(func, &mv->operands[1], ir->bin_op.lhs_reg);
      vector_push(blocks->asm_list, mv);

      // For variable shifts, ensure count is in ECX/CL to avoid RA aliasing
      bool is_shift = (ir->kind == IR_SHL || ir->kind == IR_SHR ||
                       ir->kind == IR_SAL || ir->kind == IR_SAR);
      if (is_shift)
      {
        X64_ASM *mov_ecx = new_asm();
        mov_ecx->kind = X64_MOV;
        // dst: ecx
        set_regs(&mov_ecx->operands[0],
                 set_reserved_real_regs(calloc(1, sizeof(X64_REG)), rcx,
                                        SIZE_DWORD));
        // src: rhs vreg
        assign_virtual_regs(func, &mov_ecx->operands[1], ir->bin_op.rhs_reg);
        vector_push(blocks->asm_list, mov_ecx);
      }

      X64_ASM *op = new_asm();
      switch (ir->kind)
      {
        case IR_ADD: op->kind = X64_ADD; break;
        case IR_SUB: op->kind = X64_SUB; break;
        case IR_MUL:
        case IR_MULU: op->kind = X64_IMUL; break;
        case IR_AND: op->kind = X64_AND; break;
        case IR_OR: op->kind = X64_OR; break;
        case IR_XOR: op->kind = X64_XOR; break;
        case IR_SHL: op->kind = X64_SHL; break;
        case IR_SHR: op->kind = X64_SHR; break;
        case IR_SAL: op->kind = X64_SAL; break;
        case IR_SAR: op->kind = X64_SAR; break;
        default: unreachable(); return;
      }
      assign_virtual_regs(func, &op->operands[0], ir->bin_op.dst_reg);
      if (is_shift)
      {
        // Use fixed CL as the count operand
        set_regs(&op->operands[1],
                 set_reserved_real_regs(calloc(1, sizeof(X64_REG)), rcx,
                                        SIZE_DWORD));
      }
      else
      {
        assign_virtual_regs(func, &op->operands[1], ir->bin_op.rhs_reg);
      }
      vector_push(blocks->asm_list, op);
    }
    break;
    case IR_DIV:
    case IR_DIVU:
    case IR_REM:
    case IR_REMU:
    {  // implicit use rdx, and rax
      // Move LHS into RAX with a constraint, then CQO, then DIV/IDIV.
      X64_REG *lhs_reg = search_regs(func, ir->bin_op.lhs_reg);
      X64_REG *rhs_reg = search_regs(func, ir->bin_op.rhs_reg);
      if (!lhs_reg || !rhs_reg)
        unreachable();
      // mov rax, lhs
      X64_ASM *prep = new_asm();
      prep->kind = X64_MOV;
      set_regs(&prep->operands[0], calloc(1, sizeof(X64_REG)));
      prep->operands[0].reg->reg_type = real_regs;
      prep->operands[0].reg->size = lhs_reg->size;
      prep->operands[0].reg->real_reg = rax;
      assign_virtual_regs(func, &prep->operands[1], ir->bin_op.lhs_reg);
      prep->implicit_used_registers = 1 << rax;
      vector_push(blocks->asm_list, prep);

      // Sign/zero extend high half before division depending on width
      X64_ASM *ext = new_asm();
      if (ir->kind == IR_DIVU || ir->kind == IR_REMU)
      {
        // For unsigned, clear RDX
        ext->kind = X64_XOR;
        set_regs(&ext->operands[0],
                 set_reserved_real_regs(calloc(1, sizeof(X64_REG)), rdx,
                                        SIZE_QWORD));
        set_regs(&ext->operands[1], ext->operands[0].reg);
      }
      else
      {
        // For signed: use CDQ for 32-bit values, CQO for 64-bit
        if (lhs_reg->size == SIZE_DWORD)
          ext->kind = X64_CDQ;
        else
          ext->kind = X64_CQO;
      }
      ext->implicit_used_registers = (1 << rax) | (1 << rdx);
      vector_push(blocks->asm_list, ext);

      // div/idiv
      X64_ASM *div = new_asm();
      switch (ir->kind)
      {
        case IR_DIV: div->kind = X64_IDIV; break;
        case IR_DIVU: div->kind = X64_DIV; break;  // unsigned
        case IR_REM: div->kind = X64_IDIV; break;
        case IR_REMU: div->kind = X64_DIV; break;  // unsigned
        default: unreachable(); break;
      }
      div->implicit_used_registers = (1 << rax) | (1 << rdx);
      set_regs(&div->operands[0], rhs_reg);
      vector_push(blocks->asm_list, div);

      // Move result (RAX for quot, RDX for rem) into dst virtual
      X64_ASM *res = new_asm();
      res->kind = X64_MOV;
      assign_virtual_regs(func, &res->operands[0], ir->bin_op.dst_reg);
      set_regs(&res->operands[1], calloc(1, sizeof(X64_REG)));
      res->operands[1].reg->reg_type = real_regs;
      res->operands[1].reg->size = ir->bin_op.dst_reg->reg_size;
      res->operands[1].reg->real_reg =
          (ir->kind == IR_REM || ir->kind == IR_REMU) ? rdx : rax;
      vector_push(blocks->asm_list, res);
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
      // Use search_and_create_regs to ensure vregs exist for compare operands
      X64_REG *lhs_reg = search_and_create_regs(func, ir->bin_op.lhs_reg);
      X64_REG *rhs_reg = search_and_create_regs(func, ir->bin_op.rhs_reg);
      set_regs(&cmp->operands[0], lhs_reg);
      set_regs(&cmp->operands[1], rhs_reg);
      vector_push(blocks->asm_list, cmp);
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
      assign_virtual_regs(func, &set->operands[0], ir->bin_op.dst_reg);
      // Force register (byte-capable) for SETcc destination
      set->operands[0].constraint.require_register = true;
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
        X64_REG *cond = search_and_create_regs(func, ir->jmp.cond_reg);
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
      // Move argument from ABI location into local stack slot [dst_reg]
      X64_ASM *mov = new_asm();
      mov->kind = X64_MOV;
      // dst: memory at address in dst_reg
      mov->operands[0].kind = OP_MEM;
      mov->operands[0].mem.base = search_regs(func, ir->store_arg.dst_reg);
      mov->operands[0].mem.displacement = 0;
      // Use the width provided by IR for this argument
      OperandSize arg_sz = ir->store_arg.size ? ir->store_arg.size : SIZE_DWORD;
      mov->operands[0].mem.mov_size = arg_sz;
      // src: fixed ABI register for first 6, otherwise from caller stack at
      // [rbp + 16 + 8*(arg_index-6)] per SysV AMD64 ABI
      static const enum register_name abi_regs[] = {rdi, rsi, rdx, rcx, r8, r9};
      if (ir->store_arg.arg_index < sizeof(abi_regs) / sizeof(abi_regs[0]))
      {
        mov->operands[1].kind = OP_REG;
        mov->operands[1].reg = calloc(1, sizeof(X64_REG));
        mov->operands[1].reg->reg_type = real_regs;
        mov->operands[1].reg->size = arg_sz;
        mov->operands[1].reg->real_reg = abi_regs[ir->store_arg.arg_index];
      }
      else
      {
        mov->operands[1].kind = OP_MEM;
        mov->operands[1].mem.base =
            set_reserved_real_regs(calloc(1, sizeof(X64_REG)), rbp, SIZE_QWORD);
        // return address at [rbp+8], first stack arg at [rbp+16]
        int disp = 16 + (int)((ir->store_arg.arg_index - 6) * 8);
        mov->operands[1].mem.displacement = disp;
        mov->operands[1].mem.index = NULL;
        mov->operands[1].mem.scale = SIZE_QWORD;
        mov->operands[1].mem.mov_size = arg_sz;
      }
      vector_push(blocks->asm_list, mov);
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
      X64_ASM *ext = new_asm();
      if (ir->kind == IR_SIGN_EXTEND)
      {
        // Choose MOVSXD for 32->64 sign-extend, otherwise MOVSX
        OperandSize src_sz = ir->memsize.src_reg->reg_size;
        OperandSize dst_sz = ir->memsize.dst_reg->reg_size;
        if (src_sz == SIZE_DWORD && dst_sz == SIZE_QWORD)
          ext->kind = X64_MOVSXD;
        else
          ext->kind = X64_MOVSX;
      }
      else
      {
        ext->kind = X64_MOVZX;
      }
      assign_virtual_regs(func, &ext->operands[0], ir->memsize.dst_reg);
      assign_virtual_regs(func, &ext->operands[1], ir->memsize.src_reg);
      vector_push(blocks->asm_list, ext);
    }
    break;
    case IR_TRUNCATE: break;
    case IR_BIT_NOT:
    case IR_NEG:
    {
      X64_ASM *op = new_asm();
      switch (ir->kind)
      {
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
      X64_REG *cond = search_regs(func, ir->un_op.src_reg);
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
      xor->operands[0].constraint.require_register = true;
      vector_push(blocks->asm_list, xor);
      X64_ASM *set = new_asm();
      set->kind = X64_SETE;
      set_regs(&set->operands[0], xor->operands[1].reg);
      set->operands[0].constraint.require_register = true;
      vector_push(blocks->asm_list, set);
    }
    break;
    case IR_PHI:
      // Phi is lowered later into parallel copies on predecessor edges.
      // Do not emit anything here.
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

static void insert_before_terminator(Vector *asm_list, X64_ASM *insn)
{
  if (!vector_size(asm_list))
  {
    vector_push(asm_list, insn);
    return;
  }
  X64_ASM *last = vector_peek(asm_list);
  switch (last->kind)
  {
    case X64_JMP:
    case X64_JNE:
    case X64_JE:
    case X64_RET:
    case X64_RETURN:
    case X64_LEAVE:
      // Insert before the terminator
      vector_insert(asm_list, vector_size(asm_list), insn);
      break;
    default: vector_push(asm_list, insn); break;
  }
}

static bool block_defines_reg(IR_Blocks *ir_block, size_t reg_num)
{
  for (size_t i = 1; i <= vector_size(ir_block->reg_def); i++)
  {
    size_t *p = vector_peek_at(ir_block->reg_def, i);
    if (*p == reg_num)
      return true;
  }
  return false;
}

static void lower_block_phis(X64_FUNC *func_x64,
                             IR_Blocks *ir_block,
                             X64_Blocks *x64_block,
                             Vector *ir_blocks_vec,
                             Vector *x64_blocks_vec)
{
  (void)x64_block; // phi copies go to predecessors
  // Scan IRs for PHI and emit parallel copies on each predecessor
  for (size_t i = 1; i <= vector_size(ir_block->IRs); i++)
  {
    IR *ir = vector_peek_at(ir_block->IRs, i);
    if (ir->kind != IR_PHI)
      continue;

    // For each predecessor, choose corresponding source
    for (size_t pi = 1; pi <= vector_size(ir_block->parent); pi++)
    {
      IR_Blocks *pred_ir = vector_peek_at(ir_block->parent, pi);
      // Map predecessor IR block to the created X64 block
      size_t pred_index = vector_search(ir_blocks_vec, pred_ir);
      if (!pred_index)
        continue;  // safety: skip if predecessor not found
      X64_Blocks *pred_x64 = vector_peek_at(x64_blocks_vec, pred_index);

      // Decide source: if pred defines lhs, use lhs; else if defines rhs, use rhs; else default to rhs
      IR_REG *src_reg = NULL;
      if (block_defines_reg(pred_ir, ir->phi.lhs_reg->reg_num))
        src_reg = ir->phi.lhs_reg;
      else if (block_defines_reg(pred_ir, ir->phi.rhs_reg->reg_num))
        src_reg = ir->phi.rhs_reg;
      else
      {
        // Fallback: if predecessor has no explicit def, pick the one whose
        // size matches dst, preferring lhs.
        if (ir->phi.lhs_reg->reg_size == ir->phi.dst_reg->reg_size)
          src_reg = ir->phi.lhs_reg;
        else
          src_reg = ir->phi.rhs_reg;
      }

      // Emit move: dst <- src at end of predecessor
      X64_ASM *mov = new_asm();
      mov->kind = X64_MOV;
      assign_virtual_regs((X64_FUNC *)func_x64, &mov->operands[0],
                          ir->phi.dst_reg);
      assign_virtual_regs((X64_FUNC *)func_x64, &mov->operands[1], src_reg);
      insert_before_terminator(pred_x64->asm_list, mov);
    }
  }
}

static void generate_x64_blocks(X64_FUNC *func_x64, IRFunc *func)
{
  // Create 1:1 X64 blocks for each IR block to keep CFG alignment
  Vector *x64_blocks = vector_allocate(vector_size(func->IR_Blocks));
  for (size_t i = 1; i <= vector_size(func->IR_Blocks); i++)
  {
    X64_Blocks *blk = new_block();
    // Maintain 1:1 index mapping with IR blocks
    vector_push(func_x64->asm_blocks, blk);
    vector_replace_at(x64_blocks, i, blk);
  }

  // Emit all non-PHI instructions into corresponding blocks
  for (size_t i = 1; i <= vector_size(func->IR_Blocks); i++)
  {
    IR_Blocks *ir_block = vector_peek_at(func->IR_Blocks, i);
    X64_Blocks *x64_block = vector_peek_at(x64_blocks, i);
    for (size_t j = 1; j <= vector_size(ir_block->IRs); j++)
    {
      IR *ir = vector_peek_at(ir_block->IRs, j);
      generate_x64_asm_no_phi(func_x64, x64_block, ir);
    }
  }

  // Lower PHIs into parallel copies on predecessor edges
  for (size_t i = 1; i <= vector_size(func->IR_Blocks); i++)
  {
    IR_Blocks *ir_block = vector_peek_at(func->IR_Blocks, i);
    X64_Blocks *x64_block = vector_peek_at(x64_blocks, i);
    lower_block_phis(func_x64, ir_block, x64_block, func->IR_Blocks,
                     x64_blocks);
  }
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

  generate_x64_blocks(&func_x64, func);

  // MIR numbering and liveness (scaffold)
  x64_number_program_points(&func_x64);
  x64_build_mir_liveness(func, &func_x64);
  // Optional MIR dump (disabled to reduce overhead)
  // x64_dump_mir_cfg(func, &func_x64);
  x64_regalloc(func, &func_x64);
  // Simple peephole after allocation to remove redundant moves
  x64_peephole_optimize(&func_x64);
  // Drop instructions marked unused to keep the stream minimal
  x64_compact_unused(&func_x64);
  // Validate target invariants (e.g., stack alignment at calls)
  x64_validate(func, &func_x64);
  x64_emit_function(func, &func_x64);
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
      // Emit top-level builtin assembly chunks verbatim
      for (size_t bi = 1; bi <= vector_size(func->IR_Blocks); bi++)
      {
        IR_Blocks *blk = vector_peek_at(func->IR_Blocks, bi);
        for (size_t ii = 1; ii <= vector_size(blk->IRs); ii++)
        {
          IR *ir = vector_peek_at(blk->IRs, ii);
          if (ir->kind == IR_BUILTIN_ASM)
          {
            size_t len = ir->builtin_asm.asm_len;
            char *str = parse_string_literal(ir->builtin_asm.asm_str, &len);
            // Print as-is; rely on provided newlines
            output_file("    %.*s", (int)len, str);
          }
        }
      }
      break;
    }
  }
}
