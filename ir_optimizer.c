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

void remove_parent(IRFunc* func, IR_Blocks* blocks, IR_Blocks* removed)
{
  for (size_t i = 1; i <= vector_size(blocks->parent); i++)
  {
    IR_Blocks* tmp = vector_peek_at(blocks->parent, i);
    if (tmp == removed)
    {
      vector_pop_at(blocks->parent, i);
      if (!vector_size(blocks->parent))
      {
        vector_pop_at(func->IR_Blocks, vector_search(func->IR_Blocks, blocks));
        if (blocks->lhs)
          remove_parent(func, blocks->lhs, blocks);
        if (blocks->rhs)
          remove_parent(func, blocks->rhs, blocks);
      }
      goto exit;
    }
  }
  unreachable();
exit:
  ;
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
        {  // remove unused blocks
          IR_Blocks* removed = vector_pop_at(function->IR_Blocks, j--);
          if (removed->lhs)
            remove_parent(function, removed->lhs, removed);
          if (removed->rhs)
            remove_parent(function, removed->rhs, removed);
        }
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
  same:;
  }
  return is_changed;
}

void add_reg_use(IR_Blocks* blocks, size_t reg, Vector* defined)
{
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
        // Use all arg regs; define dst only if present (non-void calls)
        for (size_t j = 1; j <= vector_size(ir->call.args); j++)
          add_reg_use(blocks, *(size_t*)vector_peek_at(ir->call.args, j),
                      defined);
        if (ir->call.dst_reg)
          add_reg_def(blocks, ir->call.dst_reg->reg_num, defined);
        break;
      case IR_FUNC_PROLOGUE:
      case IR_FUNC_EPILOGUE:
      case IR_BUILTIN_ASM:
      case IR_JMP:
      case IR_LABEL: break;
      case IR_RET:
        // void return has no src_reg; guard before use
        if (!ir->ret.return_void && ir->ret.src_reg)
          add_reg_use(blocks, ir->ret.src_reg->reg_num, defined);
        break;
      case IR_MOV:
        if (!ir->mov.is_imm)
          add_reg_use(blocks, ir->mov.src_reg->reg_num, defined);
        add_reg_def(blocks, ir->mov.dst_reg->reg_num, defined);
        break;
      case IR_ADD:
      case IR_SUB:
      case IR_MUL:
      case IR_MULU:
      case IR_DIV:
      case IR_DIVU:
      case IR_REM:
      case IR_REMU:
      case IR_EQ:
      case IR_NEQ:
      case IR_LT:
      case IR_LTU:
      case IR_LTE:
      case IR_LTEU:
      case IR_OR:
      case IR_XOR:
      case IR_AND:
      case IR_SAL:
      case IR_SHL:
      case IR_SAR:
      case IR_SHR:
        add_reg_use(blocks, ir->bin_op.lhs_reg->reg_num, defined);
        add_reg_use(blocks, ir->bin_op.rhs_reg->reg_num, defined);
        add_reg_def(blocks, ir->bin_op.dst_reg->reg_num, defined);
        break;
      case IR_PHI:
        add_reg_use(blocks, ir->phi.lhs_reg->reg_num, defined);
        add_reg_use(blocks, ir->phi.rhs_reg->reg_num, defined);
        add_reg_def(blocks, ir->phi.dst_reg->reg_num, defined);
        break;
      case IR_JNE:
      case IR_JE:
        add_reg_use(blocks, ir->jmp.cond_reg->reg_num, defined);
        break;
      case IR_LOAD:
        add_reg_use(blocks, ir->mem.mem_reg->reg_num, defined);
        add_reg_def(blocks, ir->mem.reg->reg_num, defined);
        break;
      case IR_STORE:
        add_reg_use(blocks, ir->mem.mem_reg->reg_num, defined);
        add_reg_use(blocks, ir->mem.reg->reg_num, defined);
        break;
      case IR_STORE_ARG:
        add_reg_def(blocks, ir->store_arg.dst_reg->reg_num, defined);
        break;
      case IR_LEA:
        add_reg_def(blocks, ir->lea.dst_reg->reg_num, defined);
        break;
      case IR_NOT:
      case IR_BIT_NOT:
      case IR_NEG:
        add_reg_use(blocks, ir->un_op.src_reg->reg_num, defined);
        add_reg_def(blocks, ir->un_op.dst_reg->reg_num, defined);
        break;
      case IR_SIGN_EXTEND:
      case IR_ZERO_EXTEND:
      case IR_TRUNCATE:
        add_reg_use(blocks, ir->memsize.src_reg->reg_num, defined);
        add_reg_def(blocks, ir->memsize.dst_reg->reg_num, defined);
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
          same:;
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

// Forward decl for local PHI lowering pass
static void lower_phi(IRProgram* program);

IRProgram* optimize_ir(IRProgram* program)
{
  pr_debug("start optimizer");
  make_cfg(program);
  analyze_cfg(program);
  // Lower PHI nodes to explicit copies on predecessor edges at IR level.
  // This makes the transformation target-agnostic so backends can assume
  // SSA has been deconstructed. Place before live-variable analysis so
  // liveness accounts for the inserted MOVs.
  lower_phi(program);
  analyze_live_variable(program);

#if DEBUG
  dump_cfg(program, stdout);
#endif
  return program;
}

// ------------------------------
// IR-level PHI lowering
// ------------------------------

static bool ir_block_defines_reg(IR_Blocks* block, size_t reg_num)
{
  // Scan IRs directly to detect whether this block defines the given vreg.
  // This avoids relying on reg_def being populated before liveness.
  for (size_t i = 1; i <= vector_size(block->IRs); i++)
  {
    IR* ir = vector_peek_at(block->IRs, i);
    switch (ir->kind)
    {
      case IR_CALL:
        if (ir->call.dst_reg && ir->call.dst_reg->reg_num == reg_num) return true;
        break;
      case IR_MOV:
        if (ir->mov.dst_reg && ir->mov.dst_reg->reg_num == reg_num) return true;
        break;
      case IR_ADD: case IR_SUB: case IR_MUL: case IR_MULU:
      case IR_DIV: case IR_DIVU: case IR_REM: case IR_REMU:
      case IR_EQ: case IR_NEQ: case IR_LT: case IR_LTU:
      case IR_LTE: case IR_LTEU: case IR_OR: case IR_XOR:
      case IR_AND: case IR_SAL: case IR_SHL: case IR_SAR: case IR_SHR:
        if (ir->bin_op.dst_reg && ir->bin_op.dst_reg->reg_num == reg_num) return true;
        break;
      case IR_PHI:
        if (ir->phi.dst_reg && ir->phi.dst_reg->reg_num == reg_num) return true;
        break;
      case IR_LOAD:
        if (ir->mem.reg && ir->mem.reg->reg_num == reg_num) return true;
        break;
      case IR_STORE:
        break;
      case IR_STORE_ARG:
        if (ir->store_arg.dst_reg && ir->store_arg.dst_reg->reg_num == reg_num) return true;
        break;
      case IR_LEA:
        if (ir->lea.dst_reg && ir->lea.dst_reg->reg_num == reg_num) return true;
        break;
      case IR_NOT: case IR_BIT_NOT: case IR_NEG:
        if (ir->un_op.dst_reg && ir->un_op.dst_reg->reg_num == reg_num) return true;
        break;
      case IR_SIGN_EXTEND: case IR_ZERO_EXTEND: case IR_TRUNCATE:
        if (ir->memsize.dst_reg && ir->memsize.dst_reg->reg_num == reg_num) return true;
        break;
      default: break;
    }
  }
  return false;
}

static void ir_insert_before_terminator(IR_Blocks* blk, IR* insn)
{
  size_t n = vector_size(blk->IRs);
  if (n == 0)
  {
    vector_push(blk->IRs, insn);
    return;
  }
  IR* last = vector_peek(blk->IRs);
  switch (last->kind)
  {
    case IR_JMP:
    case IR_JNE:
    case IR_JE:
    case IR_RET:
    case IR_FUNC_EPILOGUE:
      // Insert before the terminator
      vector_insert(blk->IRs, n, insn);
      break;
    default:
      vector_push(blk->IRs, insn);
      break;
  }
}

static void lower_phi_in_func(IRFunc* func)
{
  size_t block_count = vector_size(func->IR_Blocks);
  for (size_t block_index = 1; block_index <= block_count; block_index++)
  {
    IR_Blocks* block = vector_peek_at(func->IR_Blocks, block_index);
    size_t original_ir_count = vector_size(block->IRs);
    Vector* phis = vector_new();
    for (size_t ir_index = 1; ir_index <= original_ir_count; ir_index++)
    {
      IR* ir = vector_peek_at(block->IRs, ir_index);
      if (ir->kind == IR_PHI)
        vector_push(phis, ir);
    }
    if (vector_size(phis) != 0)
    {
      size_t phi_count = vector_size(phis);
      size_t parent_count = vector_size(block->parent);
      for (size_t phi_index = 1; phi_index <= phi_count; phi_index++)
      {
        IR* phi = vector_peek_at(phis, phi_index);
        for (size_t parent_index = 1; parent_index <= parent_count; parent_index++)
        {
          IR_Blocks* pred = vector_peek_at(block->parent, parent_index);
          IR_REG* src = NULL;
          if (ir_block_defines_reg(pred, phi->phi.lhs_reg->reg_num))
            src = phi->phi.lhs_reg;
          else if (ir_block_defines_reg(pred, phi->phi.rhs_reg->reg_num))
            src = phi->phi.rhs_reg;
          else if (phi->phi.lhs_reg->reg_size == phi->phi.dst_reg->reg_size)
            src = phi->phi.lhs_reg;
          else
            src = phi->phi.rhs_reg;

          IR* mov = calloc(1, sizeof(IR));
          mov->kind = IR_MOV;
          mov->mov.is_imm = false;
          mov->mov.src_reg = src;
          mov->mov.dst_reg = phi->phi.dst_reg;
          vector_push(src->used_list, mov);
          vector_push(phi->phi.dst_reg->used_list, mov);
          ir_insert_before_terminator(pred, mov);
        }
      }

      Vector* new_list = vector_new();
      size_t current_ir_count = vector_size(block->IRs);
      for (size_t ir_index = 1; ir_index <= current_ir_count; ir_index++)
      {
        IR* ir = vector_peek_at(block->IRs, ir_index);
        if (ir->kind != IR_PHI)
          vector_push(new_list, ir);
      }
      block->IRs = new_list;
    }
  }
}

__attribute__((unused)) static void lower_phi(IRProgram* program)
{
  for (size_t fi = 1; fi <= vector_size(program->functions); fi++)
  {
    IRFunc* f = vector_peek_at(program->functions, fi);
    if (f->builtin_func == FUNC_USER_DEFINED)
      lower_phi_in_func(f);
  }
}
