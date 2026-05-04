#include "include/a64_opt.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#endif

#include "include/vector.h"

static bool same_reg_operand(const A64_Operand* a, const A64_Operand* b)
{
  if (!a || !b) return false;
  if (a->kind != A64_OP_REG || b->kind != A64_OP_REG) return false;
  if (!a->reg || !b->reg) return false;
  if (a->reg == b->reg) return true;
  if (a->reg->reg_type != b->reg->reg_type) return false;
  if (a->reg->reg_type == a64_real_regs)
    return a->reg->real_reg == b->reg->real_reg;
  if (a->reg->reg_type == a64_virtual_regs)
    return a->reg->reg_id == b->reg->reg_id;
  return false;
}

static OperandSize op_reg_size(const A64_Operand* a)
{
  if (a && a->kind == A64_OP_REG && a->reg)
    return a->reg->size ? a->reg->size : SIZE_DWORD;
  return SIZE_DWORD;
}

static int get_vreg_real(A64_FUNC* fa, size_t vindex)
{
  size_t* real = vector_peek_at(fa->vreg_real, vindex);
  return real ? (int)(*real) : (int)a64_reg_reserved;
}

static bool is_vreg(const A64_Operand* op)
{
  return op->kind == A64_OP_REG && op->reg && op->reg->reg_type == a64_virtual_regs;
}

static size_t vindex_of(const A64_Operand* op)
{
  return op->reg->reg_id + 1;
}

void a64_peephole_optimize(A64_FUNC* func)
{
  for (size_t bi = 1; bi <= vector_size(func->asm_blocks); bi++)
  {
    A64_Blocks* blk = vector_peek_at(func->asm_blocks, bi);
    for (size_t ji = 1; ji <= vector_size(blk->asm_list); ji++)
    {
      A64_ASM* as = vector_peek_at(blk->asm_list, ji);
      if (!as) continue;

      // Fold double MVN/NEG: mvn t, s; mvn d, t  ==> mov d, s
      //                       neg t, s; neg d, t  ==> mov d, s
      if ((as->kind == A64_MVN || as->kind == A64_NEG) && ji + 1 <= vector_size(blk->asm_list))
      {
        A64_ASM* nx = vector_peek_at(blk->asm_list, ji + 1);
        if (nx && nx->kind == as->kind)
        {
          if (same_reg_operand(&nx->operands[1], &as->operands[0]))
          {
            OperandSize ssz = op_reg_size(&as->operands[1]);
            OperandSize mid = op_reg_size(&as->operands[0]);
            OperandSize dsz = op_reg_size(&nx->operands[0]);
            if (ssz == mid && mid == dsz)
            {
              nx->kind = A64_MOV;
              nx->operands[1] = as->operands[1];
              as->kind = A64_UNUSED;
              // continue normal pass; MOV cleanup below may also remove nx if redundant
            }
          }
        }
      }
      // Simplify redundant 32->32 extends into MOV (no semantic change)
      if (as->kind == A64_UXTW || as->kind == A64_SXTW)
      {
        A64_Operand* d = &as->operands[0];
        A64_Operand* s = &as->operands[1];
        if (d->kind == A64_OP_REG && d->reg && d->reg->size == SIZE_DWORD && s->kind == A64_OP_REG && s->reg)
        {
          as->kind = A64_MOV;
        }
      }

      if (as->kind != A64_MOV) continue;
      A64_Operand* dst = &as->operands[0];
      A64_Operand* src = &as->operands[1];

      // Do not eliminate moves that explicitly set the ABI return register (x0/w0).
      // Even if they look redundant from a local perspective, they carry semantic
      // meaning at function returns and should be preserved.
      if (dst->kind == A64_OP_REG && dst->reg && dst->reg->reg_type == a64_real_regs &&
          dst->reg->real_reg == a64_x0)
      {
        continue;
      }

      if (is_vreg(src))
      {
        int assigned = get_vreg_real(func, vindex_of(src));
        if (assigned != (int)a64_reg_reserved)
        {
          if (dst->kind == A64_OP_REG && dst->reg && dst->reg->reg_type == a64_real_regs &&
              (int)dst->reg->real_reg == assigned)
          {
            as->kind = A64_UNUSED; continue;
          }
          if (is_vreg(dst))
          {
            int ad = get_vreg_real(func, vindex_of(dst));
            if (ad == assigned) { as->kind = A64_UNUSED; continue; }
          }
        }
      }

      if (is_vreg(dst) && src->kind == A64_OP_REG && src->reg && src->reg->reg_type == a64_real_regs)
      {
        int ad = get_vreg_real(func, vindex_of(dst));
        if (ad == (int)src->reg->real_reg) { as->kind = A64_UNUSED; continue; }
      }

      if (dst->kind == A64_OP_REG && src->kind == A64_OP_REG && dst->reg && src->reg &&
          dst->reg->reg_type == a64_real_regs && src->reg->reg_type == a64_real_regs &&
          dst->reg->real_reg == src->reg->real_reg)
      { as->kind = A64_UNUSED; continue; }

      if (dst->kind == A64_OP_REG && src->kind == A64_OP_REG && dst->reg && src->reg && dst->reg == src->reg &&
          dst->reg->reg_type == a64_virtual_regs)
      { as->kind = A64_UNUSED; continue; }
    }
  }
}

void a64_compact_unused(A64_FUNC* func)
{
  for (size_t bi = 1; bi <= vector_size(func->asm_blocks); bi++)
  {
    A64_Blocks* blk = vector_peek_at(func->asm_blocks, bi);
    for (size_t ji = vector_size(blk->asm_list); ji >= 1; ji--)
    {
      A64_ASM* as = vector_peek_at(blk->asm_list, ji);
      if (as && as->kind == A64_UNUSED) vector_pop_at(blk->asm_list, ji);
      if (ji == 1) break;
    }
  }
}
