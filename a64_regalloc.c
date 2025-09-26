#include "include/a64_regalloc.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
void qsort(void* base, size_t nmemb, size_t size,
           int (*compar)(const void*, const void*));
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
                            Interval* table, size_t nregs,
                            size_t* coalesce_with, size_t* coalesce_at)
{
  (void)func_ir;
  for (size_t i = 1; i <= nregs; i++)
  {
    table[i].vindex = i; table[i].start = (size_t)-1; table[i].end = 0;
    table[i].must_in_reg = false; table[i].exists = false;
    coalesce_with[i] = 0; coalesce_at[i] = 0;
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
          // coalesce simple vreg<-vreg
          if (as->operands[0].kind == A64_OP_REG && as->operands[1].kind == A64_OP_REG &&
              as->operands[0].reg && as->operands[1].reg &&
              as->operands[0].reg->reg_type == a64_virtual_regs &&
              as->operands[1].reg->reg_type == a64_virtual_regs)
          {
            size_t d = as->operands[0].reg->reg_id + 1;
            size_t s = as->operands[1].reg->reg_id + 1;
            if (d>=1 && d<=nregs && s>=1 && s<=nregs && coalesce_with[d]==0)
            { coalesce_with[d]=s; coalesce_at[d]=as->insn_id; }
          }
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

static int cmp_start(const void* a, const void* b)
{
  const Interval* ia = (const Interval*)a;
  const Interval* ib = (const Interval*)b;
  if (ia->start < ib->start) return -1;
  if (ia->start > ib->start) return 1;
  if (ia->must_in_reg && !ib->must_in_reg) return -1;
  if (!ia->must_in_reg && ib->must_in_reg) return 1;
  return 0;
}

typedef struct { size_t i; a64_register_name real; bool must; size_t end; } Active;

static void regalloc_expire_old(Active* active, size_t* active_n,
                                bool reg_taken[], size_t cur_start)
{
  size_t k = 0;
  for (size_t j = 0; j < *active_n; j++)
  {
    if (active[j].end < cur_start)
    {
      if (active[j].real != a64_reg_reserved)
        reg_taken[active[j].real] = false;
    }
    else
    {
      active[k++] = active[j];
    }
  }
  *active_n = k;
}

static a64_register_name regalloc_get_free(const a64_register_name* pool,
                                           size_t n, bool reg_taken[])
{
  for (size_t i = 0; i < n; i++)
  {
    a64_register_name r = pool[i];
    if (!reg_taken[r]) return r;
  }
  return a64_reg_reserved;
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
  size_t* coalesce_with = calloc(nregs + 1, sizeof(size_t));
  size_t* coalesce_at   = calloc(nregs + 1, sizeof(size_t));
  build_intervals(func_ir, fa, table, nregs, coalesce_with, coalesce_at);

  bool reg_taken[64] = {0};
  size_t* free_slots = calloc(nregs ? nregs : 1, sizeof(size_t)); size_t free_n = 0;
  typedef struct { size_t end; size_t slot; } LiveSpill;
  LiveSpill* live = calloc(nregs ? nregs : 1, sizeof(LiveSpill)); size_t live_n = 0;

  size_t m = 0; for (size_t i = 1; i <= nregs; i++) if (table[i].exists) m++;
  Interval* list = calloc(m ? m : 1, sizeof(Interval));
  size_t li = 0; for (size_t i = 1; i <= nregs; i++) if (table[i].exists) list[li++] = table[i];
  qsort(list, m, sizeof(Interval), cmp_start);

  Active* active = calloc(m ? m : 1, sizeof(Active)); size_t active_n = 0;

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

  auto void expire_spills(size_t cur)
  {
    size_t k = 0; for (size_t j = 0; j < live_n; j++)
    {
      if (live[j].end < cur) free_slots[free_n++] = live[j].slot;
      else live[k++] = live[j];
    }
    live_n = k;
  };

  auto size_t alloc_spill(size_t endp)
  {
    size_t slot;
    if (free_n) slot = free_slots[--free_n];
    else { fa->spill_stack_size += 8; slot = func_ir->user_defined.stack_size + fa->spill_stack_size; }
    live[live_n++] = (LiveSpill){.end=endp, .slot=slot};
    fa->stack_used = true;
    return slot;
  };

  Vector* call_points = vector_new();
  for (size_t bi = 1; bi <= vector_size(fa->asm_blocks); bi++)
  {
    A64_Blocks* blk = vector_peek_at(fa->asm_blocks, bi);
    for (size_t ji = 1; ji <= vector_size(blk->asm_list); ji++)
    {
      A64_ASM* as = vector_peek_at(blk->asm_list, ji);
      if (as->kind == A64_CALL)
      {
        size_t* idp = malloc(sizeof(size_t)); *idp = as->insn_id; vector_push(call_points, idp);
      }
    }
  }
  for (size_t i = 2; i <= vector_size(call_points); i++)
  {
    size_t* key = vector_peek_at(call_points, i); size_t j = i;
    while (j > 1)
    {
      size_t* prev = vector_peek_at(call_points, j-1);
      if (*prev <= *key)
        break;
      vector_replace_at(call_points, j, prev);
      j--;
    }
    vector_replace_at(call_points, j, key);
  }
  size_t call_idx = 1;

  auto void split_active_at(size_t at)
  {
    size_t j = 0;
    while (j < active_n)
    {
      if (!active[j].must && active[j].end > at)
      {
        size_t vindex = list[active[j].i].vindex;
        size_t* rend = vector_peek_at(fa->vreg_real_end, vindex); if (rend) *rend = at;
        size_t* slot = vector_peek_at(fa->vreg_stack_slot, vindex);
        if (slot && *slot == 0) *slot = alloc_spill(active[j].end);
        if (active[j].real != a64_reg_reserved) reg_taken[active[j].real] = false;
        for (size_t k = j + 1; k < active_n; k++) active[k-1] = active[k];
        active_n--; continue;
      }
      j++;
    }
  };

  for (size_t i = 0; i < m; i++)
  {
    Interval cur = list[i];
    regalloc_expire_old(active, &active_n, reg_taken, cur.start);
    expire_spills(cur.start);
    while (call_idx <= vector_size(call_points))
    {
      size_t* cp = vector_peek_at(call_points, call_idx);
      if (!cp || *cp >= cur.start)
        break;
      split_active_at(*cp);
      call_idx++;
    }

    a64_register_name r = a64_reg_reserved;
    size_t src_idx = coalesce_with[cur.vindex];
    if (src_idx)
    {
      size_t* src_real = vector_peek_at(fa->vreg_real, src_idx);
      if (src_real && *src_real != a64_reg_reserved)
      {
        a64_register_name try_r = (a64_register_name)(*src_real);
        bool free_r = !reg_taken[try_r];
        bool non_overlap = table[src_idx].end <= cur.start;
        if (free_r && non_overlap) r = try_r;
        else if (!free_r)
        {
          for (size_t j = 0; j < active_n; j++)
          {
            if (active[j].real == try_r)
            {
              size_t victim = list[active[j].i].vindex; if (victim == src_idx) break;
              if (!active[j].must)
              {
                size_t* rend = vector_peek_at(fa->vreg_real_end, victim);
                if (rend) *rend = (cur.start > 0 ? cur.start - 1 : 0);
                size_t* slot = vector_peek_at(fa->vreg_stack_slot, victim);
                if (slot && *slot == 0) *slot = alloc_spill(active[j].end);
                for (size_t k = j + 1; k < active_n; k++) active[k-1] = active[k];
                active_n--; reg_taken[try_r] = false; r = try_r;
              }
              break;
            }
          }
        }
      }
    }
    if (r == a64_reg_reserved) r = regalloc_get_free(alloc_pool, pool_n, reg_taken);
    if (r == a64_reg_reserved)
    {
      if (cur.must_in_reg)
      {
        size_t victim = (size_t)-1; size_t vend = 0;
        for (size_t j = 0; j < active_n; j++)
          if (!active[j].must && active[j].end >= vend) { victim = j; vend = active[j].end; }
        if (victim != (size_t)-1)
        {
          a64_register_name freed = active[victim].real;
          size_t vindex = list[active[victim].i].vindex;
          size_t* slot = vector_peek_at(fa->vreg_stack_slot, vindex);
          size_t* rend = vector_peek_at(fa->vreg_real_end, vindex);
          if (rend) *rend = (cur.start > 0 ? cur.start - 1 : 0);
          if (slot && *slot == 0) *slot = alloc_spill(active[victim].end);
          r = freed; reg_taken[r] = true;
          active[victim].i = i; active[victim].real = r; active[victim].must = cur.must_in_reg; active[victim].end = cur.end;
          size_t* real_cur = vector_peek_at(fa->vreg_real, cur.vindex); *real_cur = r;
          fa->used_registers |= (1u << r);
          continue;
        }
      }
      size_t* slot = vector_peek_at(fa->vreg_stack_slot, cur.vindex);
      *slot = alloc_spill(cur.end);
      continue;
    }
    reg_taken[r] = true; size_t* real = vector_peek_at(fa->vreg_real, cur.vindex); *real = r; fa->used_registers |= (1u << r);
    active[active_n++] = (Active){.i=i, .real=r, .must=cur.must_in_reg, .end=cur.end};
  }

  // Cleanup
  free(active); free(list); free(table); free(live); free(free_slots); free(coalesce_with); free(coalesce_at);
  for (size_t i = 1; i <= vector_size(call_points); i++) { size_t* p = vector_peek_at(call_points, i); if (p) free(p); }
}
