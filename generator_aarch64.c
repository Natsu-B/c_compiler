#include "include/generator_aarch64.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#include <string.h>
#endif

#include "include/generator.h"
#include "include/builtin.h"
#include "include/vector.h"
#include "include/a64_emit.h"
#include "include/a64_liveness.h"
#include "include/a64_regalloc.h"
#include "include/a64_opt.h"
#include "include/a64_validate.h"

static const a64_register_name a64_abi_arg_regs[] = {
    a64_x0, a64_x1, a64_x2, a64_x3, a64_x4, a64_x5, a64_x6, a64_x7};

static A64_Blocks* a64_new_block()
{
  A64_Blocks* b = calloc(1, sizeof(A64_Blocks));
  b->asm_list = vector_new();
  b->parent = vector_new();
  b->in = vector_new();
  b->out = vector_new();
  return b;
}

static A64_ASM* a64_new_asm()
{
  return calloc(1, sizeof(A64_ASM));
}

static A64_REG* a64_set_reserved_real(A64_REG* r, a64_register_name rr,
                                      OperandSize sz)
{
  r->reg_type = a64_real_regs;
  r->size = sz;
  r->real_reg = rr;
  r->is_reserved = true;
  return r;
}

static void a64_set_reg(A64_Operand* op, A64_REG* r)
{
  op->kind = A64_OP_REG;
  op->reg = r;
}

static A64_REG* a64_search_reg(A64_FUNC* func, IR_REG* r)
{
  return vector_peek_at(func->virtual_regs, r->reg_num + 1);
}

static void a64_push_reg(A64_FUNC* func, size_t index, A64_REG* r)
{
  vector_replace_at(func->virtual_regs, index + 1, r);
}

static A64_REG* a64_search_or_create(A64_FUNC* func, IR_REG* r)
{
  A64_REG* p = a64_search_reg(func, r);
  if (!p)
  {
    p = calloc(1, sizeof(A64_REG));
    a64_push_reg(func, r->reg_num, p);
  }
  return p;
}

static void a64_assign_vreg(A64_FUNC* func, A64_Operand* op, IR_REG* r)
{
  op->kind = A64_OP_REG;
  A64_REG* v = a64_search_or_create(func, r);
  if (v->reg_type == a64_regs_reserved)
  {
    v->reg_type = a64_virtual_regs;
    v->size = r->reg_size;
    v->reg_id = r->reg_num;
  }
  op->reg = v;
}

static void a64_gen_no_phi(A64_FUNC* f, A64_Blocks* blk, IR* ir)
{
  switch (ir->kind)
  {
    case IR_CALL:
    {
      size_t nargs = vector_size(ir->call.args);
      // Move argument regs
      for (size_t i = 0; i < nargs && i < 8; i++)
      {
        IR_REG* arg = vector_peek_at(ir->call.args, i + 1);
        A64_ASM* mov = a64_new_asm();
        mov->kind = A64_MOV;
        // dst real arg reg
        a64_set_reg(&mov->operands[0],
                    a64_set_reserved_real(calloc(1, sizeof(A64_REG)),
                                          a64_abi_arg_regs[i], arg->reg_size));
        // src vreg
        a64_assign_vreg(f, &mov->operands[1], arg);
        vector_push(blk->asm_list, mov);
      }

      // Stack args beyond x7
      if (nargs > 8)
      {
        size_t stack_count = nargs - 8;
        size_t stack_bytes = stack_count * 8;
        size_t pad = (stack_bytes % 16) ? 8 : 0; // keep sp % 16 == 0
        size_t total = stack_bytes + pad;
        if (total)
        {
          A64_ASM* sub = a64_new_asm();
          sub->kind = A64_SUB;
          a64_set_reg(&sub->operands[0],
                      a64_set_reserved_real(calloc(1, sizeof(A64_REG)),
                                            a64_sp /* SP */,
                                            SIZE_QWORD));
          // Model SP as a real reg operand (we special-case it in emission)
          sub->operands[0].reg->real_reg = a64_sp;
          // sub sp, sp, #total
          a64_set_reg(&sub->operands[1], sub->operands[0].reg);
          sub->operands[2].kind = A64_OP_IMM;
          sub->operands[2].imm = (long long)total;
          vector_push(blk->asm_list, sub);
        }
        for (size_t i = 8; i < nargs; i++)
        {
          IR_REG* arg = vector_peek_at(ir->call.args, i + 1);
          A64_ASM* str = a64_new_asm();
          str->kind = A64_STR;
          // src = vreg
          a64_assign_vreg(f, &str->operands[0], arg);
          // dst mem [sp + pad + (i-8)*8]
          str->operands[1].kind = A64_OP_MEM;
          str->operands[1].mem.base =
              a64_set_reserved_real(calloc(1, sizeof(A64_REG)), a64_sp,
                                     SIZE_QWORD);
          str->operands[1].mem.displacement = (int)((i - 8) * 8);
          str->operands[1].mem.mov_size = arg->reg_size;
          vector_push(blk->asm_list, str);
        }
      }

      // CALL
      A64_ASM* call = a64_new_asm();
      call->kind = A64_CALL;
      size_t fn_len = ir->call.func_name_size;
      char* fn = calloc(1, fn_len + 1);
      memcpy(fn, ir->call.func_name, fn_len);
      fn[fn_len] = '\0';
      call->jump_target_label = fn;
      // Mark caller-saved clobbers (x0-x17)
      unsigned int cl = 0;
      for (int r = (int)a64_x0; r <= (int)a64_x17; r++) cl |= 1 << r;
      call->implicit_used_registers = cl;
      vector_push(blk->asm_list, call);

      // Move return (x0) into dst vreg
      if (ir->call.dst_reg)
      {
        A64_ASM* mr = a64_new_asm();
        mr->kind = A64_MOV;
        a64_assign_vreg(f, &mr->operands[0], ir->call.dst_reg);
        a64_set_reg(&mr->operands[1],
                    a64_set_reserved_real(calloc(1, sizeof(A64_REG)), a64_x0,
                                          ir->call.dst_reg->reg_size));
        vector_push(blk->asm_list, mr);
      }

      // Reclaim stack arg area
      if (nargs > 8)
      {
        size_t stack_count = nargs - 8;
        size_t stack_bytes = stack_count * 8;
        size_t pad = (stack_bytes % 16) ? 8 : 0;
        size_t total = stack_bytes + pad;
        if (total)
        {
          A64_ASM* add = a64_new_asm();
          add->kind = A64_ADD;
          a64_set_reg(&add->operands[0],
                      a64_set_reserved_real(calloc(1, sizeof(A64_REG)),
                                            a64_sp, SIZE_QWORD));
          // add sp, sp, #total
          a64_set_reg(&add->operands[1], add->operands[0].reg);
          add->operands[2].kind = A64_OP_IMM;
          add->operands[2].imm = (long long)total;
          vector_push(blk->asm_list, add);
        }
      }
    }
    break;
    case IR_FUNC_PROLOGUE:
    case IR_FUNC_EPILOGUE:
      break;
    case IR_RET:
    {
      A64_ASM* r = a64_new_asm();
      r->kind = A64_RETURN;
      // Carry the return value vreg (if any) in operands[0] so the emitter
      // can reliably move it into x0 just before returning.
      if (!ir->ret.return_void)
        a64_assign_vreg(f, &r->operands[0], ir->ret.src_reg);
      vector_push(blk->asm_list, r);
    }
    break;
    case IR_BUILTIN_ASM:
    {
      A64_ASM* as = a64_new_asm();
      as->kind = A64_BUILTIN_ASM;
      as->builtin_asm.asm_len = ir->builtin_asm.asm_len;
      as->builtin_asm.asm_str = parse_string_literal(ir->builtin_asm.asm_str,
                                                     &as->builtin_asm.asm_len);
      vector_push(blk->asm_list, as);
    }
    break;
    case IR_MOV:
    {
      A64_ASM* mv = a64_new_asm();
      mv->kind = A64_MOV;
      a64_assign_vreg(f, &mv->operands[0], ir->mov.dst_reg);
      if (ir->mov.is_imm)
      {
        mv->operands[1].kind = A64_OP_IMM;
        mv->operands[1].imm = ir->mov.imm_val;
      }
      else
        a64_assign_vreg(f, &mv->operands[1], ir->mov.src_reg);
      vector_push(blk->asm_list, mv);
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
      A64_ASM* op = a64_new_asm();
      switch (ir->kind)
      {
        case IR_ADD: op->kind = A64_ADD; break;
        case IR_SUB: op->kind = A64_SUB; break;
        case IR_MUL:
        case IR_MULU: op->kind = A64_MUL; break;
        case IR_AND: op->kind = A64_AND; break;
        case IR_OR: op->kind = A64_ORR; break;
        case IR_XOR: op->kind = A64_EOR; break;
        case IR_SHL:
        case IR_SAL: op->kind = A64_LSLV; break;
        case IR_SHR: op->kind = A64_LSRV; break;
        case IR_SAR: op->kind = A64_ASRV; break;
        default: break;
      }
      a64_assign_vreg(f, &op->operands[0], ir->bin_op.dst_reg);
      a64_assign_vreg(f, &op->operands[1], ir->bin_op.lhs_reg);
      a64_assign_vreg(f, &op->operands[2], ir->bin_op.rhs_reg);
      vector_push(blk->asm_list, op);
    }
    break;
    case IR_DIV:
    case IR_DIVU:
    case IR_REM:
    case IR_REMU:
    {
      // q = sdiv/udiv lhs, rhs
      A64_ASM* div = a64_new_asm();
      div->kind = (ir->kind == IR_DIVU || ir->kind == IR_REMU) ? A64_UDIV
                                                               : A64_SDIV;
      a64_assign_vreg(f, &div->operands[0], ir->bin_op.dst_reg);
      a64_assign_vreg(f, &div->operands[1], ir->bin_op.lhs_reg);
      a64_assign_vreg(f, &div->operands[2], ir->bin_op.rhs_reg);
      vector_push(blk->asm_list, div);
      if (ir->kind == IR_REM || ir->kind == IR_REMU)
      {
        // dst = lhs - q*rhs
        // tmp = mul q, rhs
        A64_ASM* mul = a64_new_asm();
        mul->kind = A64_MUL;
        a64_assign_vreg(f, &mul->operands[0], ir->bin_op.dst_reg);
        // quotient is in dst vreg of div
        a64_assign_vreg(f, &mul->operands[1], ir->bin_op.dst_reg);
        a64_assign_vreg(f, &mul->operands[2], ir->bin_op.rhs_reg);
        vector_push(blk->asm_list, mul);
        // dst = lhs - dst
        A64_ASM* sub = a64_new_asm();
        sub->kind = A64_SUB;
        a64_assign_vreg(f, &sub->operands[0], ir->bin_op.dst_reg);
        a64_assign_vreg(f, &sub->operands[1], ir->bin_op.lhs_reg);
        a64_assign_vreg(f, &sub->operands[2], ir->bin_op.dst_reg);
        vector_push(blk->asm_list, sub);
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
      A64_ASM* cmp = a64_new_asm();
      cmp->kind = A64_CMP;
      a64_assign_vreg(f, &cmp->operands[0], ir->bin_op.lhs_reg);
      a64_assign_vreg(f, &cmp->operands[1], ir->bin_op.rhs_reg);
      vector_push(blk->asm_list, cmp);
      A64_ASM* cset = a64_new_asm();
      switch (ir->kind)
      {
        case IR_EQ: cset->kind = A64_CSET_EQ; break;
        case IR_NEQ: cset->kind = A64_CSET_NE; break;
        case IR_LT: cset->kind = A64_CSET_LT; break;
        case IR_LTU: cset->kind = A64_CSET_LO; break;
        case IR_LTE: cset->kind = A64_CSET_LE; break;
        case IR_LTEU: cset->kind = A64_CSET_LS; break;
        default: break;
      }
      a64_assign_vreg(f, &cset->operands[0], ir->bin_op.dst_reg);
      cset->operands[0].constraint.require_register = true;
      vector_push(blk->asm_list, cset);
    }
    break;
    case IR_JMP:
    case IR_JNE:
    case IR_JE:
    {
      if (ir->kind != IR_JMP)
      {
        A64_ASM* cmp = a64_new_asm();
        cmp->kind = A64_CMP;
        a64_assign_vreg(f, &cmp->operands[0], ir->jmp.cond_reg);
        cmp->operands[1].kind = A64_OP_IMM;
        cmp->operands[1].imm = 0;
        vector_push(blk->asm_list, cmp);
      }
      A64_ASM* b = a64_new_asm();
      switch (ir->kind)
      {
        case IR_JMP: b->kind = A64_B; break;
        case IR_JNE: b->kind = A64_BNE; break;
        case IR_JE: b->kind = A64_BEQ; break;
        default: break;
      }
      b->jump_target_label = ir->jmp.label;
      vector_push(blk->asm_list, b);
    }
    break;
    case IR_LOAD:
    case IR_STORE:
    {
      A64_ASM* m = a64_new_asm();
      m->kind = (ir->kind == IR_LOAD) ? A64_LDR : A64_STR;
      if (ir->kind == IR_LOAD)
      {
        a64_assign_vreg(f, &m->operands[0], ir->mem.reg);
        m->operands[1].kind = A64_OP_MEM;
        m->operands[1].mem.base = a64_search_reg(f, ir->mem.mem_reg);
        m->operands[1].mem.displacement = ir->mem.offset;
        m->operands[1].mem.mov_size = ir->mem.size;
      }
      else
      {
        a64_assign_vreg(f, &m->operands[0], ir->mem.reg);
        m->operands[1].kind = A64_OP_MEM;
        m->operands[1].mem.base = a64_search_reg(f, ir->mem.mem_reg);
        m->operands[1].mem.displacement = ir->mem.offset;
        m->operands[1].mem.mov_size = ir->mem.size;
      }
      vector_push(blk->asm_list, m);
    }
    break;
    case IR_STORE_ARG:
    {
      A64_ASM* str = a64_new_asm();
      str->kind = A64_STR;
      // dst mem at address in dst_reg
      str->operands[1].kind = A64_OP_MEM;
      str->operands[1].mem.base = a64_search_reg(f, ir->store_arg.dst_reg);
      str->operands[1].mem.displacement = 0;
      OperandSize asz = ir->store_arg.size ? ir->store_arg.size : SIZE_DWORD;
      str->operands[1].mem.mov_size = asz;
      // src from ABI
      if (ir->store_arg.arg_index < 8)
      {
        a64_set_reg(&str->operands[0],
                    a64_set_reserved_real(calloc(1, sizeof(A64_REG)),
                                          a64_abi_arg_regs[ir->store_arg.arg_index], asz));
      }
      else
      {
        str->operands[0].kind = A64_OP_MEM;
        str->operands[0].mem.base =
            a64_set_reserved_real(calloc(1, sizeof(A64_REG)), a64_x29,
                                  SIZE_QWORD);
        // After prologue, x29 points to saved x29/x30 area; first stack arg at fp+16
        int disp = 16 + (int)((ir->store_arg.arg_index - 8) * 8);
        str->operands[0].mem.displacement = disp;
        str->operands[0].mem.mov_size = asz;
      }
      vector_push(blk->asm_list, str);
    }
    break;
    case IR_LEA:
    {
      if (ir->lea.is_local)
      {
        A64_ASM* add = a64_new_asm();
        add->kind = A64_ADD;
        a64_assign_vreg(f, &add->operands[0], ir->lea.dst_reg);
        a64_set_reg(&add->operands[1],
                    a64_set_reserved_real(calloc(1, sizeof(A64_REG)), a64_x29,
                                          SIZE_QWORD));
        add->operands[2].kind = A64_OP_IMM;
        add->operands[2].imm = ir->lea.var_offset;
        vector_push(blk->asm_list, add);
      }
      else
      {
        // Global: adrp + add
        A64_ASM* adrp = a64_new_asm();
        adrp->kind = A64_ADRP;
        a64_assign_vreg(f, &adrp->operands[0], ir->lea.dst_reg);
        adrp->operands[1].kind = A64_OP_MEM_RELATIVE;
        adrp->operands[1].mem.var_name = ir->lea.var_name;
        adrp->operands[1].mem.var_name_len = ir->lea.var_name_len;
        adrp->operands[1].mem.displacement = ir->lea.var_offset;
        vector_push(blk->asm_list, adrp);
        A64_ASM* add = a64_new_asm();
        add->kind = A64_ADD_ADDR;
        a64_assign_vreg(f, &add->operands[0], ir->lea.dst_reg);
        add->operands[1] = adrp->operands[1];
        vector_push(blk->asm_list, add);
      }
    }
    break;
    case IR_SIGN_EXTEND:
    case IR_ZERO_EXTEND:
    {
      OperandSize ssz = ir->memsize.src_reg->reg_size;
      OperandSize dsz = ir->memsize.dst_reg->reg_size;
      A64_ASM* ext = a64_new_asm();
      if (ir->kind == IR_SIGN_EXTEND)
      {
        switch (ssz)
        {
          case SIZE_BYTE:  ext->kind = A64_SXTB; break;
          case SIZE_WORD:  ext->kind = A64_SXTH; break;
          case SIZE_DWORD: ext->kind = (dsz == SIZE_QWORD) ? A64_SXTW : A64_MOV; break;
          default:         ext->kind = A64_MOV; break; // no-op or unsupported
        }
      }
      else
      {
        switch (ssz)
        {
          case SIZE_BYTE:  ext->kind = A64_UXTB; break;
          case SIZE_WORD:  ext->kind = A64_UXTH; break;
          case SIZE_DWORD: ext->kind = (dsz == SIZE_QWORD) ? A64_UXTW : A64_MOV; break;
          default:         ext->kind = A64_MOV; break;
        }
      }
      a64_assign_vreg(f, &ext->operands[0], ir->memsize.dst_reg);
      a64_assign_vreg(f, &ext->operands[1], ir->memsize.src_reg);
      vector_push(blk->asm_list, ext);
    }
    break;
    case IR_TRUNCATE:
      break;
    case IR_BIT_NOT:
    case IR_NEG:
    {
      A64_ASM* u = a64_new_asm();
      u->kind = (ir->kind == IR_NEG) ? A64_NEG : A64_MVN;
      a64_assign_vreg(f, &u->operands[0], ir->un_op.dst_reg);
      a64_assign_vreg(f, &u->operands[1], ir->un_op.src_reg);
      vector_push(blk->asm_list, u);
    }
    break;
    case IR_NOT:
    {
      // logical not: cmp src, #0; cset dst, eq
      A64_ASM* cmp = a64_new_asm();
      cmp->kind = A64_CMP;
      a64_assign_vreg(f, &cmp->operands[0], ir->un_op.src_reg);
      cmp->operands[1].kind = A64_OP_IMM;
      cmp->operands[1].imm = 0;
      vector_push(blk->asm_list, cmp);
      A64_ASM* cset = a64_new_asm();
      cset->kind = A64_CSET_EQ;
      a64_assign_vreg(f, &cset->operands[0], ir->un_op.dst_reg);
      cset->operands[0].constraint.require_register = true;
      vector_push(blk->asm_list, cset);
    }
    break;
    case IR_PHI:
      // handled later in phi lowering
      break;
    case IR_LABEL:
    {
      A64_ASM* lab = a64_new_asm();
      lab->kind = A64_LABEL;
      lab->jump_target_label = ir->label.name;
      vector_push(blk->asm_list, lab);
    }
    break;
    default:
      break;
  }
}

static bool a64_block_defines_reg(IR_Blocks* b, size_t regnum)
{
  for (size_t i = 1; i <= vector_size(b->IRs); i++)
  {
    IR* ir = vector_peek_at(b->IRs, i);
    switch (ir->kind)
    {
      case IR_MOV:
        if (ir->mov.dst_reg->reg_num == regnum)
          return true;
        break;
      case IR_ADD: case IR_SUB: case IR_MUL: case IR_MULU: case IR_AND:
      case IR_OR: case IR_XOR: case IR_SHL: case IR_SHR: case IR_SAL:
      case IR_SAR: case IR_DIV: case IR_DIVU: case IR_REM: case IR_REMU:
      case IR_EQ: case IR_NEQ: case IR_LT: case IR_LTU: case IR_LTE: case IR_LTEU:
        if (ir->bin_op.dst_reg->reg_num == regnum)
          return true;
        break;
      case IR_LOAD:
        if (ir->mem.reg->reg_num == regnum)
          return true;
        break;
      case IR_LEA:
        if (ir->lea.dst_reg->reg_num == regnum)
          return true;
        break;
      case IR_SIGN_EXTEND: case IR_ZERO_EXTEND:
        if (ir->memsize.dst_reg->reg_num == regnum)
          return true;
        break;
      case IR_NEG: case IR_BIT_NOT:
        if (ir->un_op.dst_reg->reg_num == regnum)
          return true;
        break;
      default: break;
    }
  }
  return false;
}

static void a64_insert_before_terminator(Vector* list, A64_ASM* insn)
{
  size_t n = vector_size(list);
  if (!n) { vector_push(list, insn); return; }
  A64_ASM* last = vector_peek_at(list, n);
  if (last && (last->kind == A64_B || last->kind == A64_BEQ ||
               last->kind == A64_BNE || last->kind == A64_RETURN))
    vector_insert(list, n, insn);
  else
    vector_push(list, insn);
}

static void a64_lower_block_phis(A64_FUNC* func_a64, IR_Blocks* irb,
                                 A64_Blocks* a64b, Vector* ir_blocks,
                                 Vector* a64_blocks)
{
  (void)a64b;
  for (size_t i = 1; i <= vector_size(irb->IRs); i++)
  {
    IR* ir = vector_peek_at(irb->IRs, i);
    if (ir->kind != IR_PHI) continue;
    for (size_t pi = 1; pi <= vector_size(irb->parent); pi++)
    {
      IR_Blocks* pred_ir = vector_peek_at(irb->parent, pi);
      size_t pred_index = vector_search(ir_blocks, pred_ir);
      if (!pred_index) continue;
      A64_Blocks* pred_a64 = vector_peek_at(a64_blocks, pred_index);
      IR_REG* src_reg = NULL;
      if (a64_block_defines_reg(pred_ir, ir->phi.lhs_reg->reg_num))
        src_reg = ir->phi.lhs_reg;
      else if (a64_block_defines_reg(pred_ir, ir->phi.rhs_reg->reg_num))
        src_reg = ir->phi.rhs_reg;
      else
        src_reg = ir->phi.lhs_reg;
      A64_ASM* mov = a64_new_asm();
      mov->kind = A64_MOV;
      a64_assign_vreg(func_a64, &mov->operands[0], ir->phi.dst_reg);
      a64_assign_vreg(func_a64, &mov->operands[1], src_reg);
      a64_insert_before_terminator(pred_a64->asm_list, mov);
    }
  }
}

static void a64_generate_blocks(A64_FUNC* fa, IRFunc* func)
{
  Vector* a64_blocks = vector_allocate(vector_size(func->IR_Blocks));
  for (size_t i = 1; i <= vector_size(func->IR_Blocks); i++)
  {
    A64_Blocks* blk = a64_new_block();
    vector_push(fa->asm_blocks, blk);
    vector_replace_at(a64_blocks, i, blk);
  }
  // non-PHI
  for (size_t i = 1; i <= vector_size(func->IR_Blocks); i++)
  {
    IR_Blocks* irb = vector_peek_at(func->IR_Blocks, i);
    A64_Blocks* a64b = vector_peek_at(a64_blocks, i);
    for (size_t j = 1; j <= vector_size(irb->IRs); j++)
      a64_gen_no_phi(fa, a64b, vector_peek_at(irb->IRs, j));
  }
  // PHI
  for (size_t i = 1; i <= vector_size(func->IR_Blocks); i++)
  {
    IR_Blocks* irb = vector_peek_at(func->IR_Blocks, i);
    A64_Blocks* a64b = vector_peek_at(a64_blocks, i);
    a64_lower_block_phis(fa, irb, a64b, func->IR_Blocks, a64_blocks);
  }
}

static void emit_top_level_builtin_asm(IRFunc* func)
{
  for (size_t bi = 1; bi <= vector_size(func->IR_Blocks); bi++)
  {
    IR_Blocks* blk = vector_peek_at(func->IR_Blocks, bi);
    for (size_t ii = 1; ii <= vector_size(blk->IRs); ii++)
    {
      IR* ir = vector_peek_at(blk->IRs, ii);
      if (ir->kind == IR_BUILTIN_ASM)
      {
        size_t len = ir->builtin_asm.asm_len;
        char* str = parse_string_literal(ir->builtin_asm.asm_str, &len);
        output_file("    %.*s", (int)len, str);
      }
    }
  }
}

static void generate_func_a64(IRFunc* func)
{
  A64_FUNC fa;
  memset(&fa, 0, sizeof(fa));
  fa.asm_blocks = vector_new();
  fa.function_name = func->user_defined.function_name;
  fa.function_name_size = func->user_defined.function_name_size;
  fa.is_static = func->user_defined.is_static;
  fa.stack_used = false;
  fa.stack_size = func->user_defined.stack_size;
  fa.num_virtual_regs = 0;
  fa.virtual_regs = vector_allocate(vector_size(func->user_defined.num_virtual_regs));

  a64_generate_blocks(&fa, func);

  a64_number_program_points(&fa);
  a64_build_mir_liveness(func, &fa);
  a64_regalloc(func, &fa);
  a64_peephole_optimize(&fa);
  a64_compact_unused(&fa);
  a64_validate(func, &fa);
  a64_emit_function(func, &fa);
}

void generate_aarch64(IRFunc* func)
{
  switch (func->builtin_func)
  {
    case FUNC_USER_DEFINED:
      generate_func_a64(func);
      break;
    case FUNC_ASM:
      emit_top_level_builtin_asm(func);
      break;
  }
}
