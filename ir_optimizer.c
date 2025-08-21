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
            unreachable();
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

void analyze_cfg(IRProgram* program)
{
  for (size_t i = 1; i <= vector_size(program->functions); i++)
  {
    IRFunc* function = vector_peek_at(program->functions, i);
    if (function->builtin_func == FUNC_USER_DEFINED)
    {
      for (size_t j = 2; j <= vector_size(function->IR_Blocks); j++)
      {
        IR_Blocks* block = vector_peek_at(function->IR_Blocks, j);
        if (!vector_size(block->parent))
          vector_pop_at(function->IR_Blocks, j--);
      }
    }
  }
}

bool analyze_live_variable_add_out(IR_Blocks* blocks, IR_Blocks* child)
{
  bool is_changed = false;
  for (size_t i = 1; i <= vector_size(child->reg_in); i++)
  {
    size_t* reg = vector_peek_at(child->reg_in, i);
    for (size_t j = 1; j <= vector_size(blocks->reg_out); j++)
      if (*reg == *(size_t*)vector_peek_at(blocks->reg_out, j))
        goto same;
    vector_push(blocks->reg_out, reg);
    is_changed = true;
  same:
  }
  return is_changed;
}

void add_reg_use(IR_Blocks* blocks, size_t reg, Vector* defined)
{
  if (reg > 1000)
    unreachable();
  for (size_t i = 1; i <= vector_size(defined); i++)
    if (reg == *(size_t*)vector_peek_at(defined, i))
      return;
  size_t* ptr = malloc(sizeof(size_t));
  *ptr = reg;
  vector_push(blocks->reg_use, ptr);
}

void add_reg_def(IR_Blocks* blocks, size_t reg, Vector* defined)
{
  size_t* ptr = malloc(sizeof(size_t));
  *ptr = reg;
  vector_push(blocks->reg_def, ptr);
  vector_push(defined, ptr);
}

void analyze_live_variable_internal(IR_Blocks* blocks)
{
  // reg_use, reg_def
  Vector* defined = vector_new();
  for (size_t i = 1; i <= vector_size(blocks->IRs); i++)
  {
    IR* ir = vector_peek_at(blocks->IRs, i);
    switch (ir->kind)
    {
      case IR_CALL:
        for (size_t j = 1; j <= vector_size(ir->call.args); j++)
          add_reg_use(blocks, *(size_t*)vector_peek_at(ir->call.args, j),
                      defined);
        add_reg_def(blocks, ir->call.dst_reg, defined);
        break;
      case IR_FUNC_PROLOGUE:
      case IR_FUNC_EPILOGUE:
      case IR_BUILTIN_ASM:
      case IR_JMP:
      case IR_LABEL:
      case IR_STRING: break;
      case IR_RET: add_reg_use(blocks, ir->ret.src_reg, defined); break;
      case IR_MOV:
        if (!ir->mov.is_imm)
          add_reg_use(blocks, ir->mov.src_reg, defined);
        add_reg_def(blocks, ir->mov.dst_reg, defined);
        break;
      case IR_ADD:
      case IR_SUB:
      case IR_MUL:
      case IR_OP_DIV:
      case IR_OP_IDIV:
      case IR_EQ:
      case IR_NEQ:
      case IR_LT:
      case IR_LTE:
      case IR_OR:
      case IR_XOR:
      case IR_AND:
      case IR_SAL:
      case IR_SHL:
      case IR_SAR:
      case IR_SHR:
        add_reg_use(blocks, ir->bin_op.lhs_reg, defined);
        add_reg_use(blocks, ir->bin_op.rhs_reg, defined);
        add_reg_def(blocks, ir->bin_op.dst_reg, defined);
        break;
      case IR_JNE:
      case IR_JE: add_reg_use(blocks, ir->jmp.cond_reg, defined); break;
      case IR_LOAD:
        add_reg_use(blocks, ir->mem.mem_reg, defined);
        add_reg_def(blocks, ir->mem.reg, defined);
        break;
      case IR_STORE:
        add_reg_use(blocks, ir->mem.mem_reg, defined);
        add_reg_use(blocks, ir->mem.reg, defined);
        break;
      case IR_STORE_ARG:
        add_reg_def(blocks, ir->store_arg.dst_reg, defined);
        break;
      case IR_LEA: add_reg_def(blocks, ir->lea.dst_reg, defined); break;
      case IR_NOT:
      case IR_BIT_NOT:
      case IR_NEG:
        add_reg_use(blocks, ir->un_op.src_reg, defined);
        add_reg_def(blocks, ir->un_op.dst_reg, defined);
        break;
      default: unreachable(); break;
    }
  }
  // copy reg_use to reg_in
  for (size_t i = 1; i <= vector_size(blocks->reg_use); i++)
    vector_push(blocks->reg_in, vector_peek_at(blocks->reg_use, i));
}

void analyze_live_variable(IRProgram* program)
{
  for (size_t i = 1; i <= vector_size(program->functions); i++)
  {
    IRFunc* function = vector_peek_at(program->functions, i);
    if (function->builtin_func == FUNC_USER_DEFINED)
    {
      for (size_t j = 1; j <= vector_size(function->IR_Blocks); j++)
        analyze_live_variable_internal(vector_peek_at(function->IR_Blocks, j));
      // calculate reg_in, reg_out
      bool is_changed = false;
      do
      {
        is_changed = false;
        for (size_t k = 1; k <= vector_size(function->IR_Blocks); k++)
        {
          IR_Blocks* blocks = vector_peek_at(function->IR_Blocks, k);
          for (size_t i = 1; i <= vector_size(blocks->reg_out); i++)
          {
            size_t* reg = vector_peek_at(blocks->reg_out, i);
            for (size_t j = 1; j <= vector_size(blocks->reg_def); j++)
              if (*reg == *(size_t*)vector_peek_at(blocks->reg_def, j))
                goto same;
            for (size_t j = 1; j <= vector_size(blocks->reg_in); j++)
              if (*reg == *(size_t*)vector_peek_at(blocks->reg_in, j))
                goto same;
            vector_push(blocks->reg_in, reg);
            is_changed = true;
          same:
          }
          if (blocks->lhs && analyze_live_variable_add_out(blocks, blocks->lhs))
            is_changed = true;
          if (blocks->rhs && analyze_live_variable_add_out(blocks, blocks->rhs))
            is_changed = true;
        }
      } while (is_changed);
    }
  }
}

IRProgram* optimize_ir(IRProgram* program)
{
  pr_debug("start optimizer");
  make_cfg(program);
  analyze_cfg(program);
  analyze_live_variable(program);

#if DEBUG
  dump_cfg(program, stdout);
#endif
  return program;
}
