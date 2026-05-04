#include "include/x64_liveness.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#endif

#include "include/error.h"
#include "include/vector.h"
#include "include/generator_x64.h"

static void vec_push_unique(Vector* v, void* p)
{
  if (!vector_search(v, p))
    vector_push(v, p);
}

static void use_op(X64_Operand* op, Vector* uses)
{
  switch (op->kind)
  {
    case OP_REG:
      if (op->reg && op->reg->reg_type == virtual_regs)
        vec_push_unique(uses, op->reg);
      break;
    case OP_MEM:
      if (op->mem.base && op->mem.base->reg_type == virtual_regs)
        vec_push_unique(uses, op->mem.base);
      if (op->mem.index && op->mem.index->reg_type == virtual_regs)
        vec_push_unique(uses, op->mem.index);
      break;
    case OP_MEM_RELATIVE:
    case OP_IMM:
    case OP_RESERVED:
    default: break;
  }
}

static void def_reg(X64_Operand* op, Vector* defs)
{
  if (op->kind == OP_REG && op->reg && op->reg->reg_type == virtual_regs)
    vec_push_unique(defs, op->reg);
}

static void compute_use_def_for_insn(X64_ASM* as, Vector* uses, Vector* defs)
{
  switch (as->kind)
  {
    case X64_MOV:
    case X64_MOVSX:
    case X64_MOVSXD:
    case X64_MOVZX:
      if (as->operands[0].kind == OP_MEM)
        use_op(&as->operands[0], uses);
      use_op(&as->operands[1], uses);
      def_reg(&as->operands[0], defs);
      break;
    case X64_LEA:
      use_op(&as->operands[1], uses);
      def_reg(&as->operands[0], defs);
      break;
    case X64_ADD:
    case X64_SUB:
    case X64_AND:
    case X64_OR:
    case X64_XOR:
    case X64_SHL:
    case X64_SHR:
    case X64_SAL:
    case X64_SAR:
    case X64_NEG:
    case X64_NOT:
      // two-operand form: dst is read+written, src is read
      use_op(&as->operands[0], uses);
      use_op(&as->operands[1], uses);
      def_reg(&as->operands[0], defs);
      break;
    case X64_IMUL:
      // We use 2-operand form in MIR currently
      use_op(&as->operands[0], uses);
      use_op(&as->operands[1], uses);
      def_reg(&as->operands[0], defs);
      break;
    case X64_IDIV:
    case X64_DIV:
    case X64_CQO:
    case X64_CDQ:
      // implicit uses/defs in RAX/RDX are modeled via implicit_used_registers.
      // For MIR liveness we only track virtual regs; operand 0 (divisor) is use.
      use_op(&as->operands[0], uses);
      break;
    case X64_CMP:
      // Uses both operands; flags are ignored
      use_op(&as->operands[0], uses);
      use_op(&as->operands[1], uses);
      break;
    case X64_SETE:
    case X64_SETNE:
    case X64_SETL:
    case X64_SETB:
    case X64_SETLE:
    case X64_SETBE:
      def_reg(&as->operands[0], defs);
      break;
    case X64_JMP:
    case X64_JE:
    case X64_JNE:
    case X64_JZ:
    case X64_JNG:
    case X64_JNGE:
    case X64_CALL:
    case X64_RET:
    case X64_LEAVE:
    case X64_RETURN:
    case X64_LABEL:
    case X64_PUSH:
    case X64_POP:
    case X64_MUL:
    case X64_BUILTIN_ASM:
    case X64_UNUSED:
    default:
      break;
  }
}

void x64_number_program_points(X64_FUNC* func_x64)
{
  size_t id = 1;
  for (size_t i = 1; i <= vector_size(func_x64->asm_blocks); i++)
  {
    X64_Blocks* blk = vector_peek_at(func_x64->asm_blocks, i);
    for (size_t j = 1; j <= vector_size(blk->asm_list); j++)
    {
      X64_ASM* as = vector_peek_at(blk->asm_list, j);
      as->insn_id = id++;
    }
  }
}

static void clear_block_sets(X64_Blocks* blk)
{
  blk->in = vector_new();
  blk->out = vector_new();
}

static void build_block_use_def(IRFunc* func_ir, X64_FUNC* func_x64,
                                size_t bindex, Vector* use, Vector* def)
{
  (void)func_ir;
  (void)func_x64;
  X64_Blocks* blk = vector_peek_at(func_x64->asm_blocks, bindex);
  for (size_t j = 1; j <= vector_size(blk->asm_list); j++)
  {
    X64_ASM* as = vector_peek_at(blk->asm_list, j);
    compute_use_def_for_insn(as, use, def);
  }
}

static void union_in_from_succs(IRFunc* func_ir, X64_FUNC* func_x64,
                                size_t bindex, Vector* out)
{
  IR_Blocks* ir_blk = vector_peek_at(func_ir->IR_Blocks, bindex);
  X64_Blocks* xblk = vector_peek_at(func_x64->asm_blocks, bindex);
  (void)xblk;  // Not used yet

  if (ir_blk->lhs)
  {
    size_t idx = vector_search(func_ir->IR_Blocks, ir_blk->lhs);
    if (idx)
    {
      X64_Blocks* succ = vector_peek_at(func_x64->asm_blocks, idx);
      for (size_t i = 1; i <= vector_size(succ->in); i++)
        vec_push_unique(out, vector_peek_at(succ->in, i));
    }
  }
  if (ir_blk->rhs)
  {
    size_t idx = vector_search(func_ir->IR_Blocks, ir_blk->rhs);
    if (idx)
    {
      X64_Blocks* succ = vector_peek_at(func_x64->asm_blocks, idx);
      for (size_t i = 1; i <= vector_size(succ->in); i++)
        vec_push_unique(out, vector_peek_at(succ->in, i));
    }
  }
}

static bool vec_contains(Vector* v, void* p)
{
  return vector_search(v, p) != 0;
}

void x64_build_mir_liveness(IRFunc* func_ir, X64_FUNC* func_x64)
{
  // Prepare in/out vectors
  for (size_t i = 1; i <= vector_size(func_x64->asm_blocks); i++)
    clear_block_sets(vector_peek_at(func_x64->asm_blocks, i));

  // Precompute use/def per block
  Vector* uses = vector_allocate(vector_size(func_x64->asm_blocks));
  Vector* defs = vector_allocate(vector_size(func_x64->asm_blocks));
  for (size_t i = 1; i <= vector_size(func_x64->asm_blocks); i++)
  {
    Vector* u = vector_new();
    Vector* d = vector_new();
    build_block_use_def(func_ir, func_x64, i, u, d);
    vector_push(uses, u);
    vector_push(defs, d);
  }

  // Initialize in with uses (like IR pass) and out empty
  for (size_t i = 1; i <= vector_size(func_x64->asm_blocks); i++)
  {
    X64_Blocks* blk = vector_peek_at(func_x64->asm_blocks, i);
    Vector* u = vector_peek_at(uses, i);
    for (size_t j = 1; j <= vector_size(u); j++)
      vec_push_unique(blk->in, vector_peek_at(u, j));
  }

  bool changed = true;
  while (changed)
  {
    changed = false;
    for (size_t i = 1; i <= vector_size(func_x64->asm_blocks); i++)
    {
      X64_Blocks* blk = vector_peek_at(func_x64->asm_blocks, i);
      Vector* in_old = vector_new();
      // snapshot old in
      for (size_t k = 1; k <= vector_size(blk->in); k++)
        vector_push(in_old, vector_peek_at(blk->in, k));

      // out[b] = U in[succ]
      blk->out = vector_new();
      union_in_from_succs(func_ir, func_x64, i, blk->out);

      // in[b] = use[b] U (out[b] - def[b])
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

      // detect changes
      if (!vector_compare(in_old, blk->in))
        changed = true;
    }
  }
}

static void dump_reg_set(const char* tag, Vector* regs)
{
  (void)tag;
  pr_debug("  %s:", (char*)tag);
  for (size_t i = 1; i <= vector_size(regs); i++)
  {
    X64_REG* r = vector_peek_at(regs, i);
    if (r && r->reg_type == virtual_regs)
      pr_debug("    v%zu", r->reg_id);
  }
}

void x64_dump_mir_cfg(IRFunc* func_ir, X64_FUNC* func_x64)
{
  pr_debug("x64-mir: function %.*s blocks=%zu", (int)func_ir->user_defined.function_name_size,
           func_ir->user_defined.function_name, vector_size(func_x64->asm_blocks));
  for (size_t bi = 1; bi <= vector_size(func_x64->asm_blocks); bi++)
  {
    IR_Blocks* irb = vector_peek_at(func_ir->IR_Blocks, bi);
    X64_Blocks* xb = vector_peek_at(func_x64->asm_blocks, bi);
    pr_debug(" block %zu: insns=%zu", bi, vector_size(xb->asm_list));
    // successors
    size_t lhs = irb->lhs ? vector_search(func_ir->IR_Blocks, irb->lhs) : 0;
    size_t rhs = irb->rhs ? vector_search(func_ir->IR_Blocks, irb->rhs) : 0;
    if (lhs) pr_debug("  succ lhs=%zu", lhs);
    if (rhs) pr_debug("  succ rhs=%zu", rhs);
    dump_reg_set("in", xb->in);
    dump_reg_set("out", xb->out);
    // instruction IDs
    for (size_t j = 1; j <= vector_size(xb->asm_list); j++)
    {
      X64_ASM* as = vector_peek_at(xb->asm_list, j);
      (void)as;
      pr_debug("    #%zu kind=%d", as->insn_id, (int)as->kind);
    }
  }
}
