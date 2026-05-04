#include "include/x64_validate.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#endif

#include "include/error.h"
#include "include/vector.h"

// Model stack pointer delta modulo 16 and verify alignment at CALLs.
// Assumptions:
// - Prologue ensures rsp % 16 == 0 at start of body (before any CALLs).
// - We only adjust rsp with SUB/ADD of immediates around stack-args, and
//   do not use arbitrary PUSH/POP in the body currently (but we handle them).
static void validate_stack_alignment(IRFunc* func_ir, X64_FUNC* fx)
{
  int sp_mod = 0;  // rsp % 16, relative to post-prologue state
  char* op = NULL;
  char* target = NULL;

  for (size_t bi = 1; bi <= vector_size(fx->asm_blocks); bi++)
  {
    X64_Blocks* blk = vector_peek_at(fx->asm_blocks, bi);
    for (size_t ji = 1; ji <= vector_size(blk->asm_list); ji++)
    {
      X64_ASM* as = vector_peek_at(blk->asm_list, ji);
      switch (as->kind)
      {
        case X64_SUB:
        case X64_ADD:
        {
          // Track SUB/ADD rsp, imm
          X64_Operand* dst = &as->operands[0];
          X64_Operand* src = &as->operands[1];
          if (dst->kind == OP_REG && dst->reg && dst->reg->reg_type == real_regs &&
              dst->reg->real_reg == rsp && src->kind == OP_IMM)
          {
            long long imm = src->imm;
            int k = (int)(imm & 15);
            if (as->kind == X64_SUB)
              sp_mod = (sp_mod - k) & 15;
            else
              sp_mod = (sp_mod + k) & 15;
            op = "+";
            if (as->kind == X64_SUB)
              op = "-";
            (void)op;
            pr_debug("x64-validate: rsp %s= %lld -> mod16=%d", op, imm,
                     sp_mod);
          }
          break;
        }
        case X64_PUSH:
          sp_mod = (sp_mod - 8) & 15;
          pr_debug("x64-validate: push -> mod16=%d", sp_mod);
          break;
        case X64_POP:
          sp_mod = (sp_mod + 8) & 15;
          pr_debug("x64-validate: pop -> mod16=%d", sp_mod);
          break;
        case X64_CALL:
          if (sp_mod != 0)
          {
            target = as->jump_target_label;
            if (!target)
              target = "<unknown>";
            error_exit("stack alignment error before CALL to %s in %.*s: rsp%%16=%d (expected 0)",
                      target,
                      (int)func_ir->user_defined.function_name_size,
                      func_ir->user_defined.function_name,
                      sp_mod);
          }
          break;
        default: break;
      }
    }
  }
}

void x64_validate(IRFunc* func_ir, X64_FUNC* func_x64)
{
  validate_stack_alignment(func_ir, func_x64);
}
