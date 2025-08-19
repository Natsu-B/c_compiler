#include "include/ir_optimizer.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#endif

#include "include/common.h"
#include "include/debug.h"
#include "include/error.h"

void add_cfg(Vector* blocks, Vector* labels)
{
  for (size_t i = 1; i < vector_size(blocks); i++)
  {
    pr_debug2("%d", i);
    IR_Blocks* block = vector_peek_at(blocks, i);
    IR* bottom = vector_peek(block->IRs);
    switch (bottom->kind)
    {
      case IR_JMP:
      case IR_JNE:
      case IR_JE:
      {
        for (size_t j = 1; j <= vector_size(labels); j++)
        {
          IR_Blocks* block_ir_label = vector_peek_at(labels, j);
          IR* label = vector_peek_at(block_ir_label->IRs, 1);
          if (label->kind != IR_LABEL)
          {
            pr_debug("label->kind: %d, vector_size(block_ir_label->IRs): %zu",
                     label->kind, vector_size(block_ir_label->IRs));
            unreachable();
          }
          if (!strcmp(bottom->jmp.label, label->label.name))
            block->lhs = block_ir_label;
          vector_push(block_ir_label->parent, block);
        }
        if (bottom->kind != IR_JMP)
        {
          IR_Blocks* next = vector_peek_at(blocks, i + 1);
          block->rhs = next;
          vector_push(next->parent, block);
        }
      }
      break;
      case IR_RET: break;
      default:
      {
        IR_Blocks* next = vector_peek_at(blocks, i + 1);
        block->lhs = next;
        vector_push(next->parent, block);
      }
      break;
    }
  }
}

void make_cfg(IRProgram* progarm)
{
  for (size_t i = 1; i <= vector_size(progarm->functions); i++)
  {
    IRFunc* function = vector_peek_at(progarm->functions, i);
    if (function->builtin_func == FUNC_USER_DEFINED)
      add_cfg(function->IR_Blocks, function->labels);
  }
}

IRProgram* optimize_ir(IRProgram* program)
{
  pr_debug("start optimizer");
  make_cfg(program);
#if DEBUG
  dump_cfg(program, stdout);
#endif
  return program;
}
