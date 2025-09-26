#include "include/a64_liveness.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#endif

#include "include/error.h"
#include "include/vector.h"

static void vec_push_unique(Vector* v, void* p)
{
  if (!vector_search(v, p))
    vector_push(v, p);
}

static void use_op(A64_Operand* op, Vector* uses)
{
  switch (op->kind)
  {
    case A64_OP_REG:
      if (op->reg && op->reg->reg_type == a64_virtual_regs)
        vec_push_unique(uses, op->reg);
      break;
    case A64_OP_MEM:
      if (op->mem.base && op->mem.base->reg_type == a64_virtual_regs)
        vec_push_unique(uses, op->mem.base);
      break;
    case A64_OP_MEM_RELATIVE:
    case A64_OP_IMM:
    case A64_OP_RESERVED:
    default: break;
  }
}

static void def_reg(A64_Operand* op, Vector* defs)
{
  if (op->kind == A64_OP_REG && op->reg && op->reg->reg_type == a64_virtual_regs)
    vec_push_unique(defs, op->reg);
}

static void compute_use_def_for_insn(A64_ASM* as, Vector* uses, Vector* defs)
{
  switch (as->kind)
  {
    case A64_MOV:
      use_op(&as->operands[1], uses);
      def_reg(&as->operands[0], defs);
      break;
    case A64_LDR:
      use_op(&as->operands[1], uses);
      def_reg(&as->operands[0], defs);
      break;
    case A64_STR:
      use_op(&as->operands[0], uses);
      use_op(&as->operands[1], uses); // address
      break;
    case A64_ADRP:
      def_reg(&as->operands[0], defs);
      break;
    case A64_ADD_ADDR:
      use_op(&as->operands[0], uses);
      def_reg(&as->operands[0], defs);
      break;
    case A64_ADD:
    case A64_SUB:
    case A64_MUL:
    case A64_SDIV:
    case A64_UDIV:
    case A64_AND:
    case A64_ORR:
    case A64_EOR:
    case A64_LSLV:
    case A64_LSRV:
    case A64_ASRV:
    case A64_MSUB:
    case A64_NEG:
    case A64_MVN:
      // 3-operand form: dst def, lhs/rhs used
      use_op(&as->operands[1], uses);
      use_op(&as->operands[2], uses);
      def_reg(&as->operands[0], defs);
      break;
    case A64_SXTB:
    case A64_SXTH:
    case A64_SXTW:
    case A64_UXTB:
    case A64_UXTH:
    case A64_UXTW:
      // unary extend: use src, def dst
      use_op(&as->operands[1], uses);
      def_reg(&as->operands[0], defs);
      break;
    case A64_CMP:
      use_op(&as->operands[0], uses);
      use_op(&as->operands[1], uses);
      break;
    case A64_CSET_EQ:
    case A64_CSET_NE:
    case A64_CSET_LT:
    case A64_CSET_LO:
    case A64_CSET_LE:
    case A64_CSET_LS:
      def_reg(&as->operands[0], defs);
      break;
    case A64_B:
    case A64_BEQ:
    case A64_BNE:
    case A64_CALL:
    case A64_LABEL:
    case A64_RETURN:
    case A64_BUILTIN_ASM:
    case A64_UNUSED:
    default:
      break;
  }
}

void a64_number_program_points(A64_FUNC* func)
{
  size_t id = 1;
  for (size_t i = 1; i <= vector_size(func->asm_blocks); i++)
  {
    A64_Blocks* blk = vector_peek_at(func->asm_blocks, i);
    for (size_t j = 1; j <= vector_size(blk->asm_list); j++)
    {
      A64_ASM* as = vector_peek_at(blk->asm_list, j);
      as->insn_id = id++;
    }
  }
}

static void clear_block_sets(A64_Blocks* blk)
{
  blk->in = vector_new();
  blk->out = vector_new();
}

static void build_block_use_def(IRFunc* func_ir, A64_FUNC* func,
                                size_t bindex, Vector* use, Vector* def)
{
  (void)func_ir;
  (void)func;
  A64_Blocks* blk = vector_peek_at(func->asm_blocks, bindex);
  for (size_t j = 1; j <= vector_size(blk->asm_list); j++)
  {
    A64_ASM* as = vector_peek_at(blk->asm_list, j);
    compute_use_def_for_insn(as, use, def);
  }
}

static void union_in_from_succs(IRFunc* func_ir, A64_FUNC* func,
                                size_t bindex, Vector* out)
{
  IR_Blocks* ir_blk = vector_peek_at(func_ir->IR_Blocks, bindex);
  if (ir_blk->lhs)
  {
    size_t idx = vector_search(func_ir->IR_Blocks, ir_blk->lhs);
    if (idx)
    {
      A64_Blocks* succ = vector_peek_at(func->asm_blocks, idx);
      for (size_t i = 1; i <= vector_size(succ->in); i++)
        vec_push_unique(out, vector_peek_at(succ->in, i));
    }
  }
  if (ir_blk->rhs)
  {
    size_t idx = vector_search(func_ir->IR_Blocks, ir_blk->rhs);
    if (idx)
    {
      A64_Blocks* succ = vector_peek_at(func->asm_blocks, idx);
      for (size_t i = 1; i <= vector_size(succ->in); i++)
        vec_push_unique(out, vector_peek_at(succ->in, i));
    }
  }
}

static bool vec_contains(Vector* v, void* p)
{
  return vector_search(v, p) != 0;
}

void a64_build_mir_liveness(IRFunc* func_ir, A64_FUNC* func)
{
  for (size_t i = 1; i <= vector_size(func->asm_blocks); i++)
    clear_block_sets(vector_peek_at(func->asm_blocks, i));

  Vector* uses = vector_allocate(vector_size(func->asm_blocks));
  Vector* defs = vector_allocate(vector_size(func->asm_blocks));
  for (size_t i = 1; i <= vector_size(func->asm_blocks); i++)
  {
    Vector* u = vector_new();
    Vector* d = vector_new();
    build_block_use_def(func_ir, func, i, u, d);
    vector_push(uses, u);
    vector_push(defs, d);
  }

  for (size_t i = 1; i <= vector_size(func->asm_blocks); i++)
  {
    A64_Blocks* blk = vector_peek_at(func->asm_blocks, i);
    Vector* u = vector_peek_at(uses, i);
    for (size_t j = 1; j <= vector_size(u); j++)
      vec_push_unique(blk->in, vector_peek_at(u, j));
  }

  bool changed = true;
  while (changed)
  {
    changed = false;
    for (size_t i = 1; i <= vector_size(func->asm_blocks); i++)
    {
      A64_Blocks* blk = vector_peek_at(func->asm_blocks, i);
      Vector* in_old = vector_new();
      for (size_t k = 1; k <= vector_size(blk->in); k++)
        vector_push(in_old, vector_peek_at(blk->in, k));

      blk->out = vector_new();
      union_in_from_succs(func_ir, func, i, blk->out);

      Vector* new_in = vector_new();
      Vector* u = vector_peek_at(uses, i);
      Vector* d = vector_peek_at(defs, i);
      for (size_t j = 1; j <= vector_size(u); j++)
        vec_push_unique(new_in, vector_peek_at(u, j));
      for (size_t j = 1; j <= vector_size(blk->out); j++)
      {
        void* r = vector_peek_at(blk->out, j);
        if (!vec_contains(d, r))
          vec_push_unique(new_in, r);
      }
      blk->in = new_in;

      if (!vector_compare(in_old, blk->in))
        changed = true;
    }
  }
}

static void dump_reg_set(const char* tag, Vector* regs)
{
  pr_debug("  %s:", (char*)tag);
  for (size_t i = 1; i <= vector_size(regs); i++)
  {
    A64_REG* r = vector_peek_at(regs, i);
    if (r && r->reg_type == a64_virtual_regs)
      pr_debug("    v%zu", r->reg_id);
  }
}

void a64_dump_mir_cfg(IRFunc* func_ir, A64_FUNC* func)
{
  pr_debug("a64-mir: function %.*s blocks=%zu",
           (int)func_ir->user_defined.function_name_size,
           func_ir->user_defined.function_name,
           vector_size(func->asm_blocks));
  for (size_t bi = 1; bi <= vector_size(func->asm_blocks); bi++)
  {
    IR_Blocks* irb = vector_peek_at(func_ir->IR_Blocks, bi);
    A64_Blocks* ab = vector_peek_at(func->asm_blocks, bi);
    pr_debug(" block %zu: insns=%zu", bi, vector_size(ab->asm_list));
    size_t lhs = irb->lhs ? vector_search(func_ir->IR_Blocks, irb->lhs) : 0;
    size_t rhs = irb->rhs ? vector_search(func_ir->IR_Blocks, irb->rhs) : 0;
    if (lhs) pr_debug("  succ lhs=%zu", lhs);
    if (rhs) pr_debug("  succ rhs=%zu", rhs);
    dump_reg_set("in", ab->in);
    dump_reg_set("out", ab->out);
    for (size_t j = 1; j <= vector_size(ab->asm_list); j++)
    {
      A64_ASM* as = vector_peek_at(ab->asm_list, j);
      pr_debug("    #%zu kind=%d", as->insn_id, (int)as->kind);
    }
  }
}
