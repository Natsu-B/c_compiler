#include "include/x64_regalloc.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#include <stdlib.h>
#endif

#include "include/error.h"
#include "include/vector.h"

// Simple interval structure for linear scan (Phase 2: add splitting points)
typedef struct Interval
{
  size_t vindex;       // virtual register index (IR reg + 1)
  size_t start;        // first program point where live
  size_t end;          // last program point where live
  bool must_in_reg;    // prefer/require allocation to a register
  bool exists;         // has any use/def/liveness
} Interval;

static void interval_add_use(Interval* it, size_t at)
{
  if (!it->exists)
  {
    it->start = it->end = at;
    it->exists = true;
  }
  else
  {
    if (at < it->start) it->start = at;
    if (at > it->end) it->end = at;
  }
}

static void mark_operand_requirements(X64_Operand* op, Interval* table,
                                      size_t nregs)
{
  if (op->kind == OP_REG && op->reg && op->reg->reg_type == virtual_regs)
  {
    size_t idx = op->reg->reg_id + 1;
    if (idx >= 1 && idx <= nregs)
    {
      if (op->constraint.require_register || op->constraint.has_fixed_register)
        table[idx].must_in_reg = true;
    }
  }
  if (op->kind == OP_MEM)
  {
    if (op->mem.base && op->mem.base->reg_type == virtual_regs)
    {
      size_t idx = op->mem.base->reg_id + 1;
      if (idx >= 1 && idx <= nregs) table[idx].must_in_reg = true;
    }
    if (op->mem.index && op->mem.index->reg_type == virtual_regs)
    {
      size_t idx = op->mem.index->reg_id + 1;
      if (idx >= 1 && idx <= nregs) table[idx].must_in_reg = true;
    }
  }
}

static void account_use(X64_Operand* op, Interval* table, size_t nregs,
                        size_t at)
{
  if (op->kind == OP_REG && op->reg && op->reg->reg_type == virtual_regs)
  {
    size_t idx = op->reg->reg_id + 1;
    if (idx >= 1 && idx <= nregs) interval_add_use(&table[idx], at);
  }
  else if (op->kind == OP_MEM)
  {
    if (op->mem.base && op->mem.base->reg_type == virtual_regs)
    {
      size_t idx = op->mem.base->reg_id + 1;
      if (idx >= 1 && idx <= nregs) interval_add_use(&table[idx], at);
    }
    if (op->mem.index && op->mem.index->reg_type == virtual_regs)
    {
      size_t idx = op->mem.index->reg_id + 1;
      if (idx >= 1 && idx <= nregs) interval_add_use(&table[idx], at);
    }
  }
}

static void account_def(X64_Operand* op, Interval* table, size_t nregs,
                        size_t at)
{
  if (op->kind == OP_REG && op->reg && op->reg->reg_type == virtual_regs)
  {
    size_t idx = op->reg->reg_id + 1;
    if (idx >= 1 && idx <= nregs) interval_add_use(&table[idx], at);
  }
}

static void build_intervals(IRFunc* func_ir, X64_FUNC* func_x64,
                            Interval* table, size_t nregs)
{
  (void)func_ir;
  // Initialize
  for (size_t i = 1; i <= nregs; i++)
  {
    table[i].vindex = i;
    table[i].start = (size_t)-1;
    table[i].end = 0;
    table[i].must_in_reg = false;
    table[i].exists = false;
  }

  // Track block start/end instruction IDs
  size_t* blk_start = calloc(vector_size(func_x64->asm_blocks) + 1, sizeof(size_t));
  size_t* blk_end = calloc(vector_size(func_x64->asm_blocks) + 1, sizeof(size_t));

  for (size_t bi = 1; bi <= vector_size(func_x64->asm_blocks); bi++)
  {
    X64_Blocks* blk = vector_peek_at(func_x64->asm_blocks, bi);
    size_t first = 0, last = 0;
    for (size_t ji = 1; ji <= vector_size(blk->asm_list); ji++)
    {
      X64_ASM* as = vector_peek_at(blk->asm_list, ji);
      if (!first) first = as->insn_id;
      last = as->insn_id;

      // Record operand requirements
      for (int oi = 0; oi < MAX_OPERANDS; oi++)
        mark_operand_requirements(&as->operands[oi], table, nregs);

      // Use/def modeling (match x64_liveness semantics)
      switch (as->kind)
      {
        case X64_MOV:
          case X64_MOVSX:
          case X64_MOVSXD:
          case X64_MOVZX:
          case X64_LEA:
          if (as->operands[0].kind == OP_MEM)
            account_use(&as->operands[0], table, nregs, as->insn_id);
          account_use(&as->operands[1], table, nregs, as->insn_id);
          account_def(&as->operands[0], table, nregs, as->insn_id);
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
        case X64_IMUL:
          account_use(&as->operands[0], table, nregs, as->insn_id);
          account_use(&as->operands[1], table, nregs, as->insn_id);
          account_def(&as->operands[0], table, nregs, as->insn_id);
          break;
        case X64_CMP:
          account_use(&as->operands[0], table, nregs, as->insn_id);
          account_use(&as->operands[1], table, nregs, as->insn_id);
          break;
        case X64_SETE:
        case X64_SETNE:
        case X64_SETL:
        case X64_SETB:
        case X64_SETLE:
        case X64_SETBE:
          account_def(&as->operands[0], table, nregs, as->insn_id);
          break;
        case X64_IDIV:
        case X64_DIV:
        case X64_CQO:
          account_use(&as->operands[0], table, nregs, as->insn_id);
          break;
        default: break;
      }
    }
    blk_start[bi] = first;
    blk_end[bi] = last ? last : first;
  }

  // Extend intervals using MIR block in/out sets
  for (size_t bi = 1; bi <= vector_size(func_x64->asm_blocks); bi++)
  {
    X64_Blocks* blk = vector_peek_at(func_x64->asm_blocks, bi);
    if (blk_start[bi])  // empty block
    {
      for (size_t i = 1; i <= vector_size(blk->in); i++)
      {
        X64_REG* r = vector_peek_at(blk->in, i);
        if (r && r->reg_type == virtual_regs)
          interval_add_use(&table[r->reg_id + 1], blk_start[bi]);
      }
      for (size_t i = 1; i <= vector_size(blk->out); i++)
      {
        X64_REG* r = vector_peek_at(blk->out, i);
        if (r && r->reg_type == virtual_regs)
          interval_add_use(&table[r->reg_id + 1], blk_end[bi]);
      }
    }
  }

  free(blk_start);
  free(blk_end);
}

static int interval_starts_before(Interval* left, Interval* right)
{
  if (left->start < right->start) return -1;
  if (left->start > right->start) return 1;
  // Prefer must_in_reg earlier when starts equal
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
      if (active_real[j] != register_reserved)
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
      pr_debug("x64-regalloc: free spill slot rbp-%zu at %zu",
               live_spill_slot[j], cur_start);
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

static size_t regalloc_allocate_spill_slot(IRFunc* func_ir, X64_FUNC* func_x64,
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
    func_x64->spill_stack_size += 8;
    slot = func_ir->user_defined.stack_size + func_x64->spill_stack_size;
  }
  live_spill_end[*live_spills_n] = end_point;
  live_spill_slot[*live_spills_n] = slot;
  (*live_spills_n)++;
  func_x64->stack_used = true;
  pr_debug("x64-regalloc: allocate spill slot rbp-%zu until %zu", slot, end_point);
  return slot;
}

void x64_regalloc(IRFunc* func_ir, X64_FUNC* func_x64)
{
  // Callee-saved allocation pool only: callers clobber caller-saved regs
  const enum register_name alloc_pool[] = {rbx, r12, r13, r14, r15};
  const size_t pool_size = sizeof(alloc_pool) / sizeof(alloc_pool[0]);

  size_t nregs = vector_size(func_x64->virtual_regs);
  func_x64->vreg_real = vector_allocate(nregs);
  func_x64->vreg_stack_slot = vector_allocate(nregs);
  func_x64->vreg_real_end = vector_allocate(nregs);
  // Rematerialization hints (immediates)
  func_x64->vreg_is_remat_imm = vector_allocate(nregs);
  func_x64->vreg_remat_imm_value = vector_allocate(nregs);
  // Rematerialization hints (LEA)
  func_x64->vreg_is_remat_lea = vector_allocate(nregs);
  func_x64->vreg_lea_is_rbp = vector_allocate(nregs);
  func_x64->vreg_lea_is_rip = vector_allocate(nregs);
  func_x64->vreg_lea_disp = vector_allocate(nregs);
  func_x64->vreg_lea_var_name = vector_allocate(nregs);
  func_x64->vreg_lea_var_name_len = vector_allocate(nregs);
  func_x64->spill_stack_size = 0;

  // Build intervals
  Interval* table = calloc(nregs + 1, sizeof(Interval));
  build_intervals(func_ir, func_x64, table, nregs);

  // Collect active set and free-reg tracking
  size_t* reg_taken = calloc(16, sizeof(size_t));
  // Spill slot free-list and live-spill tracking for reuse
  size_t* free_slots = calloc(nregs ? nregs : 1, sizeof(size_t));
  size_t free_slots_n = 0;
  size_t* live_spill_end = calloc(nregs ? nregs : 1, sizeof(size_t));
  size_t* live_spill_slot = calloc(nregs ? nregs : 1, sizeof(size_t));
  size_t live_spills_n = 0;

  // Count intervals that exist
  size_t m = 0;
  for (size_t i = 1; i <= nregs; i++)
    if (table[i].exists) m++;

  // Active intervals, stored as parallel arrays to keep self-hosted codegen simple.
  size_t* active_vindex = calloc(m ? m : 1, sizeof(size_t));
  size_t* active_real = calloc(m ? m : 1, sizeof(size_t));
  size_t* active_must = calloc(m ? m : 1, sizeof(size_t));
  size_t* active_end = calloc(m ? m : 1, sizeof(size_t));
  size_t active_n = 0;
  size_t* interval_done = calloc(nregs + 1, sizeof(size_t));

  // Allocate result arrays with defaults
  for (size_t i = 1; i <= nregs; i++)
  {
    size_t* real = malloc(sizeof(size_t));
    size_t* slot = malloc(sizeof(size_t));
    size_t* rend = malloc(sizeof(size_t));
    size_t* isimm = malloc(sizeof(size_t));
    long long* immval = malloc(sizeof(long long));
    size_t* islea = malloc(sizeof(size_t));
    size_t* leas_rbp = malloc(sizeof(size_t));
    size_t* leas_rip = malloc(sizeof(size_t));
    int* lea_disp = malloc(sizeof(int));
    char** lea_var = malloc(sizeof(char*));
    size_t* lea_var_len = malloc(sizeof(size_t));
    *real = register_reserved;
    *slot = 0;
    *rend = 0;   // no split by default
    *isimm = 0;  // not rematerializable
    *immval = 0;
    *islea = 0;
    *leas_rbp = 0;
    *leas_rip = 0;
    *lea_disp = 0;
    *lea_var = NULL;
    *lea_var_len = 0;
    vector_replace_at(func_x64->vreg_real, i, real);
    vector_replace_at(func_x64->vreg_stack_slot, i, slot);
    vector_replace_at(func_x64->vreg_real_end, i, rend);
    vector_replace_at(func_x64->vreg_is_remat_imm, i, isimm);
    vector_replace_at(func_x64->vreg_remat_imm_value, i, immval);
    vector_replace_at(func_x64->vreg_is_remat_lea, i, islea);
    vector_replace_at(func_x64->vreg_lea_is_rbp, i, leas_rbp);
    vector_replace_at(func_x64->vreg_lea_is_rip, i, leas_rip);
    vector_replace_at(func_x64->vreg_lea_disp, i, lea_disp);
    vector_replace_at(func_x64->vreg_lea_var_name, i, lea_var);
    vector_replace_at(func_x64->vreg_lea_var_name_len, i, lea_var_len);
  }

  // Analyze rematerialization: MOV vreg, imm can be rematerialized
  for (size_t bi = 1; bi <= vector_size(func_x64->asm_blocks); bi++)
  {
    X64_Blocks* blk = vector_peek_at(func_x64->asm_blocks, bi);
    for (size_t ji = 1; ji <= vector_size(blk->asm_list); ji++)
    {
      X64_ASM* as = vector_peek_at(blk->asm_list, ji);
      if (as->kind == X64_MOV && as->operands[0].kind == OP_REG &&
          as->operands[0].reg && as->operands[0].reg->reg_type == virtual_regs &&
          as->operands[1].kind == OP_IMM)
      {
        size_t idx = as->operands[0].reg->reg_id + 1;
        size_t* isimm = vector_peek_at(func_x64->vreg_is_remat_imm, idx);
        long long* immv = vector_peek_at(func_x64->vreg_remat_imm_value, idx);
        if (isimm && immv)
        {
          *isimm = 1;
          *immv = as->operands[1].imm;
        }
      }
      if (as->kind == X64_LEA && as->operands[0].kind == OP_REG &&
          as->operands[0].reg && as->operands[0].reg->reg_type == virtual_regs)
      {
        size_t idx = as->operands[0].reg->reg_id + 1;
        size_t* islea = vector_peek_at(func_x64->vreg_is_remat_lea, idx);
        size_t* isrbp = vector_peek_at(func_x64->vreg_lea_is_rbp, idx);
        size_t* isrip = vector_peek_at(func_x64->vreg_lea_is_rip, idx);
        int* disp = vector_peek_at(func_x64->vreg_lea_disp, idx);
        char** vname = vector_peek_at(func_x64->vreg_lea_var_name, idx);
        size_t* vlen = vector_peek_at(func_x64->vreg_lea_var_name_len, idx);
        if (islea && isrbp && isrip && disp && vname && vlen)
        {
          if (as->operands[1].kind == OP_MEM && as->operands[1].mem.base &&
              as->operands[1].mem.base->reg_type == real_regs &&
              as->operands[1].mem.base->real_reg == rbp)
          {
            *islea = 1; *isrbp = 1; *isrip = 0; *disp = as->operands[1].mem.displacement;
            *vname = NULL; *vlen = 0;
          }
          else if (as->operands[1].kind == OP_MEM_RELATIVE)
          {
            *islea = 1; *isrbp = 0; *isrip = 1; *disp = as->operands[1].mem.displacement;
            *vname = as->operands[1].mem.var_name; *vlen = as->operands[1].mem.var_name_len;
          }
        }
      }
    }
  }

  // We allocate virtual registers only from callee-saved registers, so their
  // values remain live across calls.

  // Linear scan over intervals
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
    if (!cur_vindex) break;
    interval_done[cur_vindex] = 1;

    size_t cur_start = table[cur_vindex].start;
    size_t cur_end = table[cur_vindex].end;
    bool cur_must_in_reg = table[cur_vindex].must_in_reg;

    regalloc_expire_old(active_vindex, active_real, active_must, active_end,
                        &active_n, reg_taken, cur_start);
    regalloc_expire_spills(live_spill_end, live_spill_slot, &live_spills_n,
                           free_slots, &free_slots_n, cur_start);

    enum register_name r = register_reserved;
    if (r == register_reserved)
    {
      for (size_t p = 0; p < pool_size; p++)
      {
        enum register_name candidate = alloc_pool[p];
        if (!reg_taken[candidate])
        {
          r = candidate;
          break;
        }
      }
    }
    bool assigned_current = false;
    bool active_entry_recorded = false;
    if (r == register_reserved)
    {
      // No free reg: if current requires register, try spilling a non-required active
      if (cur_must_in_reg)
      {
        // Find a victim with latest end that is not must_in_reg
        size_t victim = (size_t)-1; size_t victim_end = 0;
        for (size_t j = 0; j < active_n; j++)
        {
          if (!active_must[j] && active_end[j] >= victim_end)
          {
            victim = j; victim_end = active_end[j];
          }
        }
        if (victim != (size_t)-1)
        {
          // Split victim at current start
          enum register_name freed = (enum register_name)active_real[victim];
          size_t vindex = active_vindex[victim];
          size_t* slot = vector_peek_at(func_x64->vreg_stack_slot, vindex);
          size_t* rend = vector_peek_at(func_x64->vreg_real_end, vindex);
          if (rend) *rend = (cur_start > 0 ? cur_start - 1 : 0);
          if (slot && *slot == 0)
            *slot = regalloc_allocate_spill_slot(
                func_ir, func_x64, live_spill_end, live_spill_slot,
                &live_spills_n, free_slots, &free_slots_n, active_end[victim]);

          // Assign freed register to current
          r = freed;
          reg_taken[r] = true;
          // Replace victim with current in active
          active_vindex[victim] = cur_vindex;
          active_real[victim] = r;
          active_must[victim] = cur_must_in_reg ? 1 : 0;
          active_end[victim] = cur_end;
          // Record mapping
          size_t* real_cur = vector_peek_at(func_x64->vreg_real, cur_vindex);
          *real_cur = r;
          func_x64->used_registers |= (1 << r);
          assigned_current = true;
          active_entry_recorded = true;
        }
      }

      if (!assigned_current)
      {
        // Spill current
        size_t* slot = vector_peek_at(func_x64->vreg_stack_slot, cur_vindex);
        *slot = regalloc_allocate_spill_slot(
            func_ir, func_x64, live_spill_end, live_spill_slot,
            &live_spills_n, free_slots, &free_slots_n, cur_end);
        // Keep it out of active since no register assigned
      }
    }
    else
    {
      assigned_current = true;
    }

    if (assigned_current && r != register_reserved && !active_entry_recorded)
    {
      // Assign register to current
      reg_taken[r] = true;
      size_t* real = vector_peek_at(func_x64->vreg_real, cur_vindex);
      *real = r;
      func_x64->used_registers |= (1 << r);
      // Add to active
      active_vindex[active_n] = cur_vindex;
      active_real[active_n] = r;
      active_must[active_n] = cur_must_in_reg ? 1 : 0;
      active_end[active_n] = cur_end;
      active_n++;
    }
  }

  // Debug dump of allocation results
  pr_debug("x64-regalloc: results for %.*s", (int)func_ir->user_defined.function_name_size,
           func_ir->user_defined.function_name);
  for (size_t i = 1; i <= nregs; i++)
  {
    if (table[i].exists)
    {
      size_t* real = vector_peek_at(func_x64->vreg_real, i);
      size_t* slot = vector_peek_at(func_x64->vreg_stack_slot, i);
      if (real && *real != register_reserved)
        pr_debug("  v%zu [%zu,%zu] -> reg %zu", i - 1, table[i].start, table[i].end, *real);
      else if (slot && *slot)
        pr_debug("  v%zu [%zu,%zu] -> spill rbp-%zu", i - 1, table[i].start, table[i].end, *slot);
      else
        pr_debug("  v%zu [%zu,%zu] -> unassigned", i - 1, table[i].start, table[i].end);
    }
  }

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
