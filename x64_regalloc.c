#include "include/x64_regalloc.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#endif

#include "include/error.h"
#include "include/vector.h"

// Minimal placeholder: future work will allocate real registers per MIR with
// constraints. For now, keep virtual registers and rely on later emission to
// materialize moves/spills.
void x64_regalloc(IRFunc* func_ir, X64_FUNC* func_x64)
{
  (void)func_ir;
  // Reserve clobbered/fixed registers up-front to avoid allocating them to
  // persistent virtuals: rsp, rbp always; rax/rdx for idiv/cqo; caller-saved
  // (rax, rcx, rdx, rsi, rdi, r8–r11) for calls. Use only callee-saved pool.
  const enum register_name alloc_pool[] = {rbx, r12, r13, r14, r15};
  const size_t pool_size = sizeof(alloc_pool) / sizeof(alloc_pool[0]);

  size_t nregs = vector_size(func_x64->virtual_regs);
  func_x64->vreg_real = vector_allocate(nregs);
  func_x64->vreg_stack_slot = vector_allocate(nregs);
  func_x64->spill_stack_size = 0;

  // Track pool availability
  bool taken[16] = {0};

  // Pre-pass: find vregs that must be in registers (addressing bases/indexes)
  bool must_in_reg[nregs + 1];
  for (size_t i = 0; i <= nregs; i++) must_in_reg[i] = false;
  for (size_t bi = 1; bi <= vector_size(func_x64->asm_blocks); bi++)
  {
    X64_Blocks* blk = vector_peek_at(func_x64->asm_blocks, bi);
    for (size_t ji = 1; ji <= vector_size(blk->asm_list); ji++)
    {
      X64_ASM* as = vector_peek_at(blk->asm_list, ji);
      for (int oi = 0; oi < MAX_OPERANDS; oi++)
      {
        X64_Operand* op = &as->operands[oi];
        if (op->kind == OP_MEM)
        {
          if (op->mem.base && op->mem.base->reg_type == virtual_regs)
          {
            size_t idx = op->mem.base->reg_id + 1;
            if (idx >= 1 && idx <= nregs) must_in_reg[idx] = true;
          }
          if (op->mem.index && op->mem.index->reg_type == virtual_regs)
          {
            size_t idx = op->mem.index->reg_id + 1;
            if (idx >= 1 && idx <= nregs) must_in_reg[idx] = true;
          }
        }
      }
    }
  }

  for (size_t i = 1; i <= nregs; i++)
  {
    X64_REG* vr = vector_peek_at(func_x64->virtual_regs, i);
    size_t* real = malloc(sizeof(size_t));
    size_t* slot = malloc(sizeof(size_t));
    *real = register_reserved;  // none assigned
    *slot = 0;                  // 0 means no stack slot yet

    if (vr && vr->reg_type == virtual_regs)
    {
      // Try to grab a callee-saved real register if available
      // First, if this vreg is required in a register, allocate preferentially
      for (size_t p = 0; p < pool_size; p++)
      {
        enum register_name r = alloc_pool[p];
        if (!taken[r])
        {
          if (must_in_reg[i])
          {
            taken[r] = true;
            *real = r;
            func_x64->used_registers |= (1u << r);
            break;
          }
        }
      }
      // If not assigned yet, try again for non-required vregs
      if (*real == register_reserved)
      {
        for (size_t p = 0; p < pool_size; p++)
        {
          enum register_name r = alloc_pool[p];
          if (!taken[r])
          {
            taken[r] = true;
            *real = r;
            func_x64->used_registers |= (1u << r);
            break;
          }
        }
      }
      // If no real reg available, allocate a spill slot
      if (*real == register_reserved)
      {
        // Place spills above local variables area to avoid overlap.
        func_x64->spill_stack_size += 8;  // 8-byte aligned slots (integers)
        *slot = func_ir->user_defined.stack_size + func_x64->spill_stack_size;
        func_x64->stack_used = true;
      }
    }

    vector_replace_at(func_x64->vreg_real, i, real);
    vector_replace_at(func_x64->vreg_stack_slot, i, slot);
  }
}
