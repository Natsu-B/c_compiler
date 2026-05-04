#include "include/a64_validate.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#endif

#include "include/error.h"
#include "include/vector.h"

// Track SP % 16 across function body and ensure alignment at CALLs
static void validate_stack_alignment(IRFunc* func_ir, A64_FUNC* fa)
{
  int sp_mod = 0; // relative to post-prologue state (we assume aligned)
  char* op = NULL;
  char* target = NULL;

  for (size_t bi = 1; bi <= vector_size(fa->asm_blocks); bi++)
  {
    A64_Blocks* blk = vector_peek_at(fa->asm_blocks, bi);
    for (size_t ji = 1; ji <= vector_size(blk->asm_list); ji++)
    {
      A64_ASM* as = vector_peek_at(blk->asm_list, ji);
      switch (as->kind)
      {
        case A64_SUB:
        case A64_ADD:
        {
          // sub sp, sp, #imm / add sp, sp, #imm
          A64_Operand* dst = &as->operands[0];
          A64_Operand* src = &as->operands[1];
          if (dst->kind == A64_OP_REG && dst->reg && dst->reg->reg_type == a64_real_regs &&
              dst->reg->real_reg == a64_sp && src->kind == A64_OP_IMM)
          {
            long long imm = src->imm; int k = (int)(imm & 15);
            if (as->kind == A64_SUB) sp_mod = (sp_mod - k) & 15;
            else sp_mod = (sp_mod + k) & 15;
            op = "+";
            if (as->kind == A64_SUB)
              op = "-";
            (void)op;
            pr_debug("a64-validate: sp %s= %lld -> mod16=%d",
                     op, imm, sp_mod);
          }
          break;
        }
        case A64_CALL:
          if (sp_mod != 0)
          {
            target = as->jump_target_label;
            if (!target)
              target = "<unknown>";
            error_exit("AArch64 stack alignment error before CALL to %s in %.*s: sp%%16=%d (expected 0)",
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

void a64_validate(IRFunc* func_ir, A64_FUNC* func)
{
  validate_stack_alignment(func_ir, func);
}
