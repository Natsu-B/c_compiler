#include "include/x64_opt.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#endif

#include "include/vector.h"

static int get_vreg_real(X64_FUNC* fx, size_t vindex)
{
  size_t* real = vector_peek_at(fx->vreg_real, vindex);
  return real ? (int)(*real) : (int)register_reserved;
}

static bool is_virtual_reg(const X64_Operand* op)
{
  return op->kind == OP_REG && op->reg && op->reg->reg_type == virtual_regs;
}

static size_t vindex_of(const X64_Operand* op)
{
  return op->reg->reg_id + 1;  // virtual reg index matches IR reg + 1
}

void x64_peephole_optimize(X64_FUNC* func_x64)
{
  for (size_t bi = 1; bi <= vector_size(func_x64->asm_blocks); bi++)
  {
    X64_Blocks* blk = vector_peek_at(func_x64->asm_blocks, bi);
    for (size_t ji = 1; ji <= vector_size(blk->asm_list); ji++)
    {
      X64_ASM* as = vector_peek_at(blk->asm_list, ji);
      if (as->kind != X64_MOV) continue;

      X64_Operand* dst = &as->operands[0];
      X64_Operand* src = &as->operands[1];

      // Case 1: dst is a fixed real reg and src is a vreg already in that reg
      if (is_virtual_reg(src))
      {
        int assigned = get_vreg_real(func_x64, vindex_of(src));
        if (assigned != (int)register_reserved)
        {
          // If dst is the same real reg, remove the move
          if (dst->kind == OP_REG && dst->reg && dst->reg->reg_type == real_regs &&
              (int)dst->reg->real_reg == assigned)
          {
            as->kind = X64_UNUSED;
            continue;
          }
          // If dst is also a vreg mapped to the same real reg, remove
          if (is_virtual_reg(dst))
          {
            int assigned_dst = get_vreg_real(func_x64, vindex_of(dst));
            if (assigned_dst == assigned)
            {
              as->kind = X64_UNUSED;
              continue;
            }
          }
        }
      }

      // Case 2: src is a fixed real reg and dst is a vreg mapped to same reg
      if (is_virtual_reg(dst) && src->kind == OP_REG && src->reg &&
          src->reg->reg_type == real_regs)
      {
        int assigned_dst = get_vreg_real(func_x64, vindex_of(dst));
        if (assigned_dst == (int)src->reg->real_reg)
        {
          as->kind = X64_UNUSED;
          continue;
        }
      }

      // Case 3: both are real registers and equal -> redundant
      if (dst->kind == OP_REG && src->kind == OP_REG && dst->reg && src->reg &&
          dst->reg->reg_type == real_regs && src->reg->reg_type == real_regs &&
          dst->reg->real_reg == src->reg->real_reg)
      {
        as->kind = X64_UNUSED;
        continue;
      }

      // Case 4: dst/src are the same virtual register object -> redundant
      if (dst->kind == OP_REG && src->kind == OP_REG && dst->reg && src->reg &&
          dst->reg == src->reg && dst->reg->reg_type == virtual_regs)
      {
        as->kind = X64_UNUSED;
        continue;
      }
    }
  }
}

void x64_compact_unused(X64_FUNC* func_x64)
{
  for (size_t bi = 1; bi <= vector_size(func_x64->asm_blocks); bi++)
  {
    X64_Blocks* blk = vector_peek_at(func_x64->asm_blocks, bi);
    for (size_t ji = vector_size(blk->asm_list); ji >= 1; ji--)
    {
      X64_ASM* as = vector_peek_at(blk->asm_list, ji);
      if (as && as->kind == X64_UNUSED)
        vector_pop_at(blk->asm_list, ji);
      if (ji == 1) break; // size_t underflow guard
    }
  }
}
