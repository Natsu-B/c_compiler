#include "include/x64_regalloc.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
// Provide qsort prototype for self-host header
void qsort(void* base, size_t nmemb, size_t size,
           int (*compar)(const void*, const void*));
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
                            Interval* table, size_t nregs,
                            size_t* coalesce_with, size_t* coalesce_at)
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

  // Initialize coalescing hints
  for (size_t i = 1; i <= nregs; i++)
  {
    coalesce_with[i] = 0;
    coalesce_at[i] = 0;
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
          account_use(&as->operands[1], table, nregs, as->insn_id);
          account_def(&as->operands[0], table, nregs, as->insn_id);
          // Capture simple mov vreg<-vreg as a coalescing opportunity
          if (as->kind == X64_MOV && as->operands[0].kind == OP_REG &&
              as->operands[1].kind == OP_REG && as->operands[0].reg &&
              as->operands[1].reg &&
              as->operands[0].reg->reg_type == virtual_regs &&
              as->operands[1].reg->reg_type == virtual_regs)
          {
            size_t d = as->operands[0].reg->reg_id + 1;
            size_t s = as->operands[1].reg->reg_id + 1;
            if (d >= 1 && d <= nregs && s >= 1 && s <= nregs && coalesce_with[d] == 0)
            {
              coalesce_with[d] = s;
              coalesce_at[d] = as->insn_id;
            }
          }
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
    if (!blk_start[bi]) continue;  // empty block
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

  free(blk_start);
  free(blk_end);
}

static int cmp_start(const void* a, const void* b)
{
  const Interval* ia = (const Interval*)a;
  const Interval* ib = (const Interval*)b;
  if (ia->start < ib->start) return -1;
  if (ia->start > ib->start) return 1;
  // Prefer must_in_reg earlier when starts equal
  if (ia->must_in_reg && !ib->must_in_reg) return -1;
  if (!ia->must_in_reg && ib->must_in_reg) return 1;
  return 0;
}

// Active entry used during linear scan
typedef struct { size_t i; enum register_name real; bool must; size_t end; } Active;

static void regalloc_expire_old(Active* active, size_t* active_n,
                                bool reg_taken[], size_t cur_start)
{
  size_t k = 0;
  for (size_t j = 0; j < *active_n; j++)
  {
    if (active[j].end < cur_start)
    {
      if (active[j].real != register_reserved)
        reg_taken[active[j].real] = false;
    }
    else
    {
      active[k++] = active[j];
    }
  }
  *active_n = k;
}

static enum register_name regalloc_get_free_reg(const enum register_name* pool,
                                                size_t pool_sz,
                                                bool reg_taken[])
{
  for (size_t p = 0; p < pool_sz; p++)
  {
    enum register_name r = pool[p];
    if (!reg_taken[r]) return r;
  }
  return register_reserved;
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
  size_t* coalesce_with = calloc(nregs + 1, sizeof(size_t));
  size_t* coalesce_at = calloc(nregs + 1, sizeof(size_t));
  build_intervals(func_ir, func_x64, table, nregs, coalesce_with, coalesce_at);

  // Collect active set and free-reg tracking
  bool reg_taken[16] = {0};
  // Spill slot free-list and live-spill tracking for reuse
  size_t* free_slots = calloc(nregs ? nregs : 1, sizeof(size_t));
  size_t free_slots_n = 0;
  typedef struct { size_t end; size_t slot; } LiveSpill;
  LiveSpill* live_spills = calloc(nregs ? nregs : 1, sizeof(LiveSpill));
  size_t live_spills_n = 0;

  // Build a compact list of intervals that exist
  size_t m = 0;
  for (size_t i = 1; i <= nregs; i++)
    if (table[i].exists) m++;
  Interval* list = calloc(m ? m : 1, sizeof(Interval));
  size_t li = 0;
  for (size_t i = 1; i <= nregs; i++)
    if (table[i].exists) list[li++] = table[i];
  qsort(list, m, sizeof(Interval), cmp_start);

  // Active intervals: store indices into list and assigned real register
  Active* active = calloc(m ? m : 1, sizeof(Active));
  size_t active_n = 0;

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

  // Helper to expire live spills and return their slots to free list
  auto void expire_spills(size_t cur_start)
  {
    size_t k = 0;
    for (size_t j = 0; j < live_spills_n; j++)
    {
      if (live_spills[j].end < cur_start)
      {
        // free this slot
        free_slots[free_slots_n++] = live_spills[j].slot;
        pr_debug("x64-regalloc: free spill slot rbp-%zu at %zu", live_spills[j].slot, cur_start);
      }
      else
      {
        live_spills[k++] = live_spills[j];
      }
    }
    live_spills_n = k;
  };

  auto size_t allocate_spill_slot(size_t end_point)
  {
    size_t slot;
    if (free_slots_n > 0)
    {
      slot = free_slots[--free_slots_n];
    }
    else
    {
      func_x64->spill_stack_size += 8;
      slot = func_ir->user_defined.stack_size + func_x64->spill_stack_size;
    }
    // Track as live until its interval end
    live_spills[live_spills_n++] = (LiveSpill){.end = end_point, .slot = slot};
    func_x64->stack_used = true;
    pr_debug("x64-regalloc: allocate spill slot rbp-%zu until %zu", slot, end_point);
    return slot;
  };

  // Gather call program points for caller-boundary splits
  Vector* call_points = vector_new();
  for (size_t bi = 1; bi <= vector_size(func_x64->asm_blocks); bi++)
  {
    X64_Blocks* blk = vector_peek_at(func_x64->asm_blocks, bi);
    for (size_t ji = 1; ji <= vector_size(blk->asm_list); ji++)
    {
      X64_ASM* as = vector_peek_at(blk->asm_list, ji);
      if (as->kind == X64_CALL)
      {
        size_t* idp = malloc(sizeof(size_t));
        *idp = as->insn_id;
        vector_push(call_points, idp);
      }
    }
  }
  // Sort call points just in case (they should already be monotonic)
  // Simple insertion sort for tiny lists
  for (size_t i = 2; i <= vector_size(call_points); i++)
  {
    size_t* key = vector_peek_at(call_points, i);
    size_t j = i;
    while (j > 1)
    {
      size_t* prev = vector_peek_at(call_points, j - 1);
      if (*prev <= *key) break;
      vector_replace_at(call_points, j, prev);
      j--;
    }
    vector_replace_at(call_points, j, key);
  }
  size_t call_idx = 1;

  auto void split_active_at(size_t at_point)
  {
    size_t j = 0;
    while (j < active_n)
    {
      // Only split non-must intervals that live across this point
      if (!active[j].must && active[j].end > at_point)
      {
        size_t vindex = list[active[j].i].vindex;
        // Mark the real mapping valid only up to at_point
        size_t* rend = vector_peek_at(func_x64->vreg_real_end, vindex);
        if (rend) *rend = at_point;
        // Allocate spill for the remainder of its live range
        size_t* slot = vector_peek_at(func_x64->vreg_stack_slot, vindex);
        if (slot && *slot == 0)
          *slot = allocate_spill_slot(active[j].end);
        // Free the register and remove from active
        if (active[j].real != register_reserved)
          reg_taken[active[j].real] = false;
        for (size_t k = j + 1; k < active_n; k++) active[k - 1] = active[k];
        active_n--;
        continue;  // stay at same j to re-check shifted element
      }
      j++;
    }
  };

  // Linear scan over intervals
  for (size_t i = 0; i < m; i++)
  {
    Interval cur = list[i];
    regalloc_expire_old(active, &active_n, reg_taken, cur.start);
    expire_spills(cur.start);

    // Apply caller-boundary splits for any CALL before this point
    while (call_idx <= vector_size(call_points))
    {
      size_t* cp = vector_peek_at(call_points, call_idx);
      if (!cp || *cp >= cur.start) break;
      pr_debug("x64-regalloc: split at call point #%zu", *cp);
      split_active_at(*cp);
      call_idx++;
    }

    // Prefer coalescing with source of an initial move into this vreg
    enum register_name r = register_reserved;
    size_t src_idx = coalesce_with[cur.vindex];
    if (src_idx)
    {
      size_t* src_real = vector_peek_at(func_x64->vreg_real, src_idx);
      if (src_real && *src_real != register_reserved)
      {
        enum register_name try_r = (enum register_name)*src_real;
        bool free_r = !reg_taken[try_r];
        bool non_overlap = table[src_idx].end <= cur.start;
        if (free_r && non_overlap)
        {
          r = try_r;
        }
        else if (!free_r)
        {
          // If the desired register is currently occupied, attempt
          // coalescing by evicting a non-must active interval using it.
          for (size_t j = 0; j < active_n; j++)
          {
            if (active[j].real == try_r)
            {
              // Do not evict the source interval itself
              size_t victim_vindex = list[active[j].i].vindex;
              if (victim_vindex == src_idx) break;
              if (!active[j].must)
              {
                // Split victim interval at current start: keep earlier mapping
                size_t* rend = vector_peek_at(func_x64->vreg_real_end, victim_vindex);
                if (rend) *rend = (cur.start > 0 ? cur.start - 1 : 0);
                size_t* slot_v = vector_peek_at(func_x64->vreg_stack_slot, victim_vindex);
                if (slot_v && *slot_v == 0) *slot_v = allocate_spill_slot(active[j].end);
                // Remove victim from active and free the register
                for (size_t k = j + 1; k < active_n; k++) active[k - 1] = active[k];
                active_n--;
                reg_taken[try_r] = false;
                r = try_r;
              }
              break;
            }
          }
        }
      }
    }
    if (r == register_reserved)
      r = regalloc_get_free_reg(alloc_pool, pool_size, reg_taken);
    if (r == register_reserved)
    {
      // No free reg: if current requires register, try spilling a non-required active
      if (cur.must_in_reg)
      {
        // Find a victim with latest end that is not must_in_reg
        size_t victim = (size_t)-1; size_t victim_end = 0;
        for (size_t j = 0; j < active_n; j++)
        {
          if (!active[j].must && active[j].end >= victim_end)
          {
            victim = j; victim_end = active[j].end;
          }
        }
        if (victim != (size_t)-1)
        {
          // Split victim at current start
          enum register_name freed = active[victim].real;
          size_t vindex = list[active[victim].i].vindex;
          size_t* slot = vector_peek_at(func_x64->vreg_stack_slot, vindex);
          size_t* rend = vector_peek_at(func_x64->vreg_real_end, vindex);
          if (rend) *rend = (cur.start > 0 ? cur.start - 1 : 0);
          if (slot && *slot == 0) *slot = allocate_spill_slot(active[victim].end);

          // Assign freed register to current
          r = freed;
          reg_taken[r] = true;
          // Replace victim with current in active
          active[victim].i = i;
          active[victim].real = r;
          active[victim].must = cur.must_in_reg;
          active[victim].end = cur.end;
          // Record mapping
          size_t* real_cur = vector_peek_at(func_x64->vreg_real, cur.vindex);
          *real_cur = r;
          func_x64->used_registers |= (1u << r);
          continue;
        }
      }

      // Spill current
      size_t* slot = vector_peek_at(func_x64->vreg_stack_slot, cur.vindex);
      *slot = allocate_spill_slot(cur.end);
      // Keep it out of active since no register assigned
      continue;
    }

    // Assign register to current
    reg_taken[r] = true;
    size_t* real = vector_peek_at(func_x64->vreg_real, cur.vindex);
    *real = r;
    func_x64->used_registers |= (1u << r);
    // Add to active
    active[active_n++] = (Active){.i = i, .real = r, .must = cur.must_in_reg, .end = cur.end};
  }

  // Debug dump of allocation results
  pr_debug("x64-regalloc: results for %.*s", (int)func_ir->user_defined.function_name_size,
           func_ir->user_defined.function_name);
  for (size_t i = 1; i <= nregs; i++)
  {
    if (!table[i].exists) continue;
    size_t* real = vector_peek_at(func_x64->vreg_real, i);
    size_t* slot = vector_peek_at(func_x64->vreg_stack_slot, i);
    if (real && *real != register_reserved)
      pr_debug("  v%zu [%zu,%zu] -> reg %zu", i - 1, table[i].start, table[i].end, *real);
    else if (slot && *slot)
      pr_debug("  v%zu [%zu,%zu] -> spill rbp-%zu", i - 1, table[i].start, table[i].end, *slot);
    else
      pr_debug("  v%zu [%zu,%zu] -> unassigned", i - 1, table[i].start, table[i].end);
  }

  free(active);
  free(list);
  free(table);
  free(live_spills);
  free(free_slots);
  free(coalesce_with);
  free(coalesce_at);
  // free call_points vector entries
  for (size_t i = 1; i <= vector_size(call_points); i++)
  {
    size_t* p = vector_peek_at(call_points, i);
    if (p) free(p);
  }
  // Note: call_points vector itself is on the stack; fine to leak or ignore free
}
