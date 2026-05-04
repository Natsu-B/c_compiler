#include "include/a64_regalloc.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#include <stdlib.h>
#endif

#include "include/vector.h"
#include "include/error.h"

typedef struct Interval {
  size_t vindex;
  size_t start;
  size_t end;
  bool must_in_reg;
  bool exists;
} Interval;

static void interval_add_use(Interval* it, size_t at)
{
  if (!it->exists) { it->start = it->end = at; it->exists = true; }
  else { if (at < it->start) it->start = at; if (at > it->end) it->end = at; }
}

static void mark_operand_requirements(A64_Operand* op, Interval* table,
                                      size_t nregs)
{
  if (op->kind == A64_OP_REG && op->reg && op->reg->reg_type == a64_virtual_regs)
  {
    size_t idx = op->reg->reg_id + 1;
    if (idx >= 1 && idx <= nregs)
    {
      if (op->constraint.require_register || op->constraint.has_fixed_register)
        table[idx].must_in_reg = true;
    }
  }
  if (op->kind == A64_OP_MEM)
  {
    if (op->mem.base && op->mem.base->reg_type == a64_virtual_regs)
    {
      size_t idx = op->mem.base->reg_id + 1;
      if (idx >= 1 && idx <= nregs) table[idx].must_in_reg = true;
    }
  }
}

static void account_use(A64_Operand* op, Interval* table, size_t nregs,
                        size_t at)
{
  if (op->kind == A64_OP_REG && op->reg && op->reg->reg_type == a64_virtual_regs)
  {
    size_t idx = op->reg->reg_id + 1;
    if (idx >= 1 && idx <= nregs) interval_add_use(&table[idx], at);
  }
  else if (op->kind == A64_OP_MEM)
  {
    if (op->mem.base && op->mem.base->reg_type == a64_virtual_regs)
    {
      size_t idx = op->mem.base->reg_id + 1;
      if (idx >= 1 && idx <= nregs) interval_add_use(&table[idx], at);
    }
  }
}

static void account_def(A64_Operand* op, Interval* table, size_t nregs,
                        size_t at)
{
  if (op->kind == A64_OP_REG && op->reg && op->reg->reg_type == a64_virtual_regs)
  {
    size_t idx = op->reg->reg_id + 1;
    if (idx >= 1 && idx <= nregs) interval_add_use(&table[idx], at);
  }
}

static void build_intervals(IRFunc* func_ir, A64_FUNC* func_a64,
                            Interval* table, size_t nregs)
{
  (void)func_ir;
  for (size_t i = 1; i <= nregs; i++)
  {
    table[i].vindex = i; table[i].start = (size_t)-1; table[i].end = 0;
    table[i].must_in_reg = false; table[i].exists = false;
  }

  size_t* blk_start = calloc(vector_size(func_a64->asm_blocks) + 1, sizeof(size_t));
  size_t* blk_end   = calloc(vector_size(func_a64->asm_blocks) + 1, sizeof(size_t));

  for (size_t bi = 1; bi <= vector_size(func_a64->asm_blocks); bi++)
  {
    A64_Blocks* blk = vector_peek_at(func_a64->asm_blocks, bi);
    size_t first = 0, last = 0;
    for (size_t ji = 1; ji <= vector_size(blk->asm_list); ji++)
    {
      A64_ASM* as = vector_peek_at(blk->asm_list, ji);
      if (!first)
        first = as->insn_id;
      last = as->insn_id;
      for (int oi = 0; oi < A64_MAX_OPERANDS; oi++)
        mark_operand_requirements(&as->operands[oi], table, nregs);
      switch (as->kind)
      {
        case A64_MOV:
          account_use(&as->operands[1], table, nregs, as->insn_id);
          account_def(&as->operands[0], table, nregs, as->insn_id);
          break;
        case A64_LDR:
          account_use(&as->operands[1], table, nregs, as->insn_id);
          account_def(&as->operands[0], table, nregs, as->insn_id);
          break;
        case A64_STR:
          account_use(&as->operands[0], table, nregs, as->insn_id);
          account_use(&as->operands[1], table, nregs, as->insn_id);
          break;
        case A64_ADRP:
          account_def(&as->operands[0], table, nregs, as->insn_id);
          break;
        case A64_ADD_ADDR:
          account_use(&as->operands[0], table, nregs, as->insn_id);
          account_def(&as->operands[0], table, nregs, as->insn_id);
          break;
        case A64_ADD: case A64_SUB: case A64_MUL: case A64_SDIV: case A64_UDIV:
        case A64_AND: case A64_ORR: case A64_EOR: case A64_LSLV: case A64_LSRV:
        case A64_ASRV: case A64_MSUB: case A64_NEG: case A64_MVN:
          account_use(&as->operands[1], table, nregs, as->insn_id);
          account_use(&as->operands[2], table, nregs, as->insn_id);
          account_def(&as->operands[0], table, nregs, as->insn_id);
          break;
        case A64_SXTB: case A64_SXTH: case A64_SXTW:
        case A64_UXTB: case A64_UXTH: case A64_UXTW:
          account_use(&as->operands[1], table, nregs, as->insn_id);
          account_def(&as->operands[0], table, nregs, as->insn_id);
          break;
        case A64_CMP:
          account_use(&as->operands[0], table, nregs, as->insn_id);
          account_use(&as->operands[1], table, nregs, as->insn_id);
          break;
        case A64_CSET_EQ: case A64_CSET_NE: case A64_CSET_LT:
        case A64_CSET_LO: case A64_CSET_LE: case A64_CSET_LS:
          account_def(&as->operands[0], table, nregs, as->insn_id);
          break;
        default: break;
      }
    }
    blk_start[bi] = first; blk_end[bi] = last ? last : first;
  }

  for (size_t bi = 1; bi <= vector_size(func_a64->asm_blocks); bi++)
  {
    A64_Blocks* blk = vector_peek_at(func_a64->asm_blocks, bi);
    if (!blk_start[bi]) continue;
    for (size_t i = 1; i <= vector_size(blk->in); i++)
    {
      A64_REG* r = vector_peek_at(blk->in, i);
      if (r && r->reg_type == a64_virtual_regs)
        interval_add_use(&table[r->reg_id + 1], blk_start[bi]);
    }
    for (size_t i = 1; i <= vector_size(blk->out); i++)
    {
      A64_REG* r = vector_peek_at(blk->out, i);
      if (r && r->reg_type == a64_virtual_regs)
        interval_add_use(&table[r->reg_id + 1], blk_end[bi]);
    }
  }

  free(blk_start); free(blk_end);
}

static int interval_starts_before(Interval* left, Interval* right)
{
  if (left->start < right->start) return -1;
  if (left->start > right->start) return 1;
  if (left->must_in_reg && !right->must_in_reg) return -1;
  if (!left->must_in_reg && right->must_in_reg) return 1;
  return 0;
}

static void regalloc_expire_old(size_t* active_vindex, size_t* active_real,
                                size_t* active_must, size_t* active_end,
                                size_t* active_n, size_t* reg_taken,
                                size_t cur_start)
{
  size_t k = 0;
  for (size_t j = 0; j < *active_n; j++)
  {
    if (active_end[j] < cur_start)
    {
      if (active_real[j] != a64_reg_reserved)
        reg_taken[active_real[j]] = false;
    }
    else
    {
      active_vindex[k] = active_vindex[j];
      active_real[k] = active_real[j];
      active_must[k] = active_must[j];
      active_end[k] = active_end[j];
      k++;
    }
  }
  *active_n = k;
}

static void regalloc_expire_spills(size_t* live_spill_end,
                                   size_t* live_spill_slot,
                                   size_t* live_spills_n,
                                   size_t* free_slots, size_t* free_slots_n,
                                   size_t cur_start)
{
  size_t k = 0;
  for (size_t j = 0; j < *live_spills_n; j++)
  {
    if (live_spill_end[j] < cur_start)
    {
      free_slots[(*free_slots_n)++] = live_spill_slot[j];
    }
    else
    {
      live_spill_end[k] = live_spill_end[j];
      live_spill_slot[k] = live_spill_slot[j];
      k++;
    }
  }
  *live_spills_n = k;
}

static size_t regalloc_allocate_spill_slot(IRFunc* func_ir, A64_FUNC* fa,
                                           size_t* live_spill_end,
                                           size_t* live_spill_slot,
                                           size_t* live_spills_n,
                                           size_t* free_slots,
                                           size_t* free_slots_n,
                                           size_t end_point)
{
  size_t slot;
  if (*free_slots_n > 0)
  {
    slot = free_slots[--(*free_slots_n)];
  }
  else
  {
    fa->spill_stack_size += 8;
    slot = func_ir->user_defined.stack_size + fa->spill_stack_size;
  }
  live_spill_end[*live_spills_n] = end_point;
  live_spill_slot[*live_spills_n] = slot;
  (*live_spills_n)++;
  fa->stack_used = true;
  return slot;
}

void a64_regalloc(IRFunc* func_ir, A64_FUNC* fa)
{
  const a64_register_name alloc_pool[] = {
      a64_x19,a64_x20,a64_x21,a64_x22,a64_x23,a64_x24,a64_x25,a64_x26,a64_x27,a64_x28};
  const size_t pool_n = sizeof(alloc_pool)/sizeof(alloc_pool[0]);

  size_t nregs = vector_size(fa->virtual_regs);
  fa->vreg_real = vector_allocate(nregs);
  fa->vreg_stack_slot = vector_allocate(nregs);
  fa->vreg_real_end = vector_allocate(nregs);
  fa->vreg_is_remat_imm = vector_allocate(nregs);
  fa->vreg_remat_imm_value = vector_allocate(nregs);
  fa->spill_stack_size = 0;

  Interval* table = calloc(nregs + 1, sizeof(Interval));
  build_intervals(func_ir, fa, table, nregs);

  size_t* reg_taken = calloc(64, sizeof(size_t));
  size_t* free_slots = calloc(nregs ? nregs : 1, sizeof(size_t));
  size_t free_n = 0;
  size_t* live_spill_end = calloc(nregs ? nregs : 1, sizeof(size_t));
  size_t* live_spill_slot = calloc(nregs ? nregs : 1, sizeof(size_t));
  size_t live_n = 0;

  size_t m = 0;
  for (size_t i = 1; i <= nregs; i++)
    if (table[i].exists)
      m++;

  size_t* active_vindex = calloc(m ? m : 1, sizeof(size_t));
  size_t* active_real = calloc(m ? m : 1, sizeof(size_t));
  size_t* active_must = calloc(m ? m : 1, sizeof(size_t));
  size_t* active_end = calloc(m ? m : 1, sizeof(size_t));
  size_t active_n = 0;
  size_t* interval_done = calloc(nregs + 1, sizeof(size_t));

  for (size_t i = 1; i <= nregs; i++)
  {
    size_t* real = malloc(sizeof(size_t)); *real = a64_reg_reserved;
    size_t* slot = malloc(sizeof(size_t)); *slot = 0;
    size_t* rend = malloc(sizeof(size_t)); *rend = 0;
    size_t* isimm = malloc(sizeof(size_t)); *isimm = 0;
    long long* immv = malloc(sizeof(long long)); *immv = 0;
    vector_replace_at(fa->vreg_real, i, real);
    vector_replace_at(fa->vreg_stack_slot, i, slot);
    vector_replace_at(fa->vreg_real_end, i, rend);
    vector_replace_at(fa->vreg_is_remat_imm, i, isimm);
    vector_replace_at(fa->vreg_remat_imm_value, i, immv);
  }

  // Discover rematerializable MOV vreg, imm
  for (size_t bi = 1; bi <= vector_size(fa->asm_blocks); bi++)
  {
    A64_Blocks* blk = vector_peek_at(fa->asm_blocks, bi);
    for (size_t ji = 1; ji <= vector_size(blk->asm_list); ji++)
    {
      A64_ASM* as = vector_peek_at(blk->asm_list, ji);
      if (as->kind == A64_MOV && as->operands[0].kind == A64_OP_REG &&
          as->operands[0].reg && as->operands[0].reg->reg_type == a64_virtual_regs &&
          as->operands[1].kind == A64_OP_IMM)
      {
        size_t idx = as->operands[0].reg->reg_id + 1;
        size_t* isimm = vector_peek_at(fa->vreg_is_remat_imm, idx);
        long long* immv = vector_peek_at(fa->vreg_remat_imm_value, idx);
        if (isimm && immv) { *isimm = 1; *immv = as->operands[1].imm; }
      }
    }
  }

  for (size_t handled = 0; handled < m; handled++)
  {
    size_t cur_vindex = 0;
    for (size_t vindex = 1; vindex <= nregs; vindex++)
    {
      bool pending = table[vindex].exists && !interval_done[vindex];
      if (pending &&
          (!cur_vindex ||
           interval_starts_before(&table[vindex], &table[cur_vindex]) < 0))
      {
        cur_vindex = vindex;
      }
    }
    if (!cur_vindex)
      break;
    interval_done[cur_vindex] = 1;

    size_t cur_start = table[cur_vindex].start;
    size_t cur_end = table[cur_vindex].end;
    bool cur_must_in_reg = table[cur_vindex].must_in_reg;

    regalloc_expire_old(active_vindex, active_real, active_must, active_end,
                        &active_n, reg_taken, cur_start);
    regalloc_expire_spills(live_spill_end, live_spill_slot, &live_n,
                           free_slots, &free_n, cur_start);

    a64_register_name r = a64_reg_reserved;
    for (size_t p = 0; p < pool_n; p++)
    {
      a64_register_name candidate = alloc_pool[p];
      if (!reg_taken[candidate])
      {
        r = candidate;
        break;
      }
    }

    bool assigned_current = false;
    bool active_entry_recorded = false;
    if (r == a64_reg_reserved)
    {
      if (cur_must_in_reg)
      {
        size_t victim = (size_t)-1; size_t vend = 0;
        for (size_t j = 0; j < active_n; j++)
        {
          if (!active_must[j] && active_end[j] >= vend)
          {
            victim = j;
            vend = active_end[j];
          }
        }
        if (victim != (size_t)-1)
        {
          a64_register_name freed = (a64_register_name)active_real[victim];
          size_t vindex = active_vindex[victim];
          size_t* slot = vector_peek_at(fa->vreg_stack_slot, vindex);
          size_t* rend = vector_peek_at(fa->vreg_real_end, vindex);
          if (rend) *rend = (cur_start > 0 ? cur_start - 1 : 0);
          if (slot && *slot == 0)
            *slot = regalloc_allocate_spill_slot(
                func_ir, fa, live_spill_end, live_spill_slot, &live_n,
                free_slots, &free_n, active_end[victim]);
          r = freed; reg_taken[r] = true;
          active_vindex[victim] = cur_vindex;
          active_real[victim] = r;
          active_must[victim] = cur_must_in_reg ? 1 : 0;
          active_end[victim] = cur_end;
          size_t* real_cur = vector_peek_at(fa->vreg_real, cur_vindex);
          *real_cur = r;
          fa->used_registers |= (1 << r);
          assigned_current = true;
          active_entry_recorded = true;
        }
      }

      if (!assigned_current)
      {
        size_t* slot = vector_peek_at(fa->vreg_stack_slot, cur_vindex);
        *slot = regalloc_allocate_spill_slot(
            func_ir, fa, live_spill_end, live_spill_slot, &live_n,
            free_slots, &free_n, cur_end);
      }
    }
    else
    {
      assigned_current = true;
    }

    if (assigned_current && r != a64_reg_reserved && !active_entry_recorded)
    {
      reg_taken[r] = true;
      size_t* real = vector_peek_at(fa->vreg_real, cur_vindex);
      *real = r;
      fa->used_registers |= (1 << r);
      active_vindex[active_n] = cur_vindex;
      active_real[active_n] = r;
      active_must[active_n] = cur_must_in_reg ? 1 : 0;
      active_end[active_n] = cur_end;
      active_n++;
    }
  }

  // Cleanup
  free(active_vindex);
  free(active_real);
  free(active_must);
  free(active_end);
  free(interval_done);
  free(table);
  free(live_spill_end);
  free(live_spill_slot);
  free(free_slots);
  free(reg_taken);
}
