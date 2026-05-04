#include "include/a64_emit.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#endif

#include "include/generator.h"
#include "include/generator_aarch64.h"
#include "include/vector.h"
// AArch64 uses GAS default syntax; no Intel-syntax preamble is necessary.
void a64_emit_file_preamble(void)
{
  // Intentionally empty for now
}

static void emit_function_header(IRFunc* func_ir)
{
  output_file("    .text");
  if (!func_ir->user_defined.is_static)
    output_file("    .globl %.*s", (int)func_ir->user_defined.function_name_size,
                func_ir->user_defined.function_name);
  output_file("    .type %.*s, @function",
              (int)func_ir->user_defined.function_name_size,
              func_ir->user_defined.function_name);
  output_file("%.*s:", (int)func_ir->user_defined.function_name_size,
              func_ir->user_defined.function_name);
}

static size_t align16(size_t n) { return (n + 15) & ~((size_t)15); }

static char a64_reg_name_buf[8][8];
static int a64_reg_name_buf_index = 0;
static char a64_cmp_imm_buf[32];

static const char* a64_reg_name(a64_register_name r, OperandSize sz)
{
  // Use a small ring buffer to avoid overwriting when formatting multiple
  // registers within a single printf-style call.
  a64_reg_name_buf_index = (a64_reg_name_buf_index + 1) & 7;
  // Map SP specially
  if (r == a64_sp)
    return "sp";
  int n = (int)r; // x0..x30 map to 0..30
  if (n < 0)
    n = 0;
  if (n > 30)
    n = 30;
  snprintf(a64_reg_name_buf[a64_reg_name_buf_index],
           sizeof(a64_reg_name_buf[a64_reg_name_buf_index]), "%c%d",
           (sz == SIZE_QWORD ? 'x' : 'w'), n);
  return a64_reg_name_buf[a64_reg_name_buf_index];
}

static size_t get_vreg_index(A64_REG* r) { return r->reg_id + 1; }

static bool vreg_has_real_at(A64_FUNC* fa, size_t idx, size_t cur_id)
{
  size_t* real = vector_peek_at(fa->vreg_real, idx);
  if (!real || *real == a64_reg_reserved) return false;
  size_t* endp = fa->vreg_real_end ? vector_peek_at(fa->vreg_real_end, idx) : NULL;
  size_t end = endp ? *endp : 0;
  if (end == 0) return true;
  if (cur_id == 0) return true;
  return cur_id <= end;
}

static a64_register_name vreg_real_at(A64_FUNC* fa, size_t idx, size_t cur_id)
{
  if (!vreg_has_real_at(fa, idx, cur_id)) return a64_reg_reserved;
  size_t* real = vector_peek_at(fa->vreg_real, idx);
  return (a64_register_name)(real ? *real : a64_reg_reserved);
}

static size_t vreg_spill_offset(A64_FUNC* fa, size_t idx)
{
  size_t* slot = vector_peek_at(fa->vreg_stack_slot, idx);
  return slot ? *slot : 0;
}

static const char* scratch_x9(OperandSize sz) { return a64_reg_name(a64_x9, sz); }
static const char* scratch_x10(OperandSize sz) { return a64_reg_name(a64_x10, sz); }

// Helpers for immediate encodability
static bool a64_is_addsub_imm_encodable(long long imm)
{
  if (imm < 0) return false;
  unsigned long long u = (unsigned long long)imm;
  if (u <= 4095) return true; // 12-bit
  if ((u & 4095) == 0)
  {
    u >>= 12;
    if (u <= 4095) return true; // 12-bit with lsl #12
  }
  return false;
}

static inline uint64_t a64_mask_bits(int nbits)
{
  if (nbits >= 64) return ~((uint64_t)0);
  return (((uint64_t)1) << nbits) - ((uint64_t)1);
}

static inline uint64_t a64_rotr(uint64_t v, int r, int width)
{
  r %= width;
  if (r == 0) return v & a64_mask_bits(width);
  uint64_t mask = a64_mask_bits(width);
  v &= mask;
  if (width == 64)
    return ((v >> r) | (v << ((64 - r) & 63))) & mask;
  return ((v >> r) | ((v << (width - r)) & mask)) & mask;
}

static inline bool a64_is_single_run_ones_any_rotate(uint64_t pat, int width)
{
  uint64_t mask = a64_mask_bits(width);
  pat &= mask;
  if (pat == 0 || pat == mask) return false; // must have both 0 and 1
  for (int r = 0; r < width; r++)
  {
    uint64_t pr = a64_rotr(pat, r, width);
    // check pr == (1<<m)-1 form: pr != 0 and pr & (pr+1) == 0
    if (pr != 0 && ((pr & (pr + ((uint64_t)1))) == 0) && pr != mask)
      return true;
  }
  return false;
}

static inline bool a64_is_repeated_pattern(uint64_t val, int nbits, int chunk)
{
  uint64_t chunk_mask = a64_mask_bits(chunk);
  uint64_t pat = val & chunk_mask;
  uint64_t rep = 0;
  int reps = nbits / chunk;
  for (int i = 0; i < reps; i++)
  {
    rep |= (pat << (i * chunk));
  }
  return (rep & a64_mask_bits(nbits)) == (val & a64_mask_bits(nbits));
}

static bool a64_is_logical_imm(uint64_t imm, int nbits)
{
  imm &= a64_mask_bits(nbits);
  if (imm == 0 || imm == a64_mask_bits(nbits)) return false;
  // Try element sizes
  const int sizes64[] = {2,4,8,16,32,64};
  const int sizes32[] = {2,4,8,16,32};
  const int* sizes = sizes32;
  int count = (int)(sizeof(sizes32) / sizeof(sizes32[0]));
  if (nbits == 64)
  {
    sizes = sizes64;
    count = (int)(sizeof(sizes64) / sizeof(sizes64[0]));
  }
  for (int i = 0; i < count; i++)
  {
    int chunk = sizes[i];
    if (!a64_is_repeated_pattern(imm, nbits, chunk)) continue;
    uint64_t pat = imm & a64_mask_bits(chunk);
    if (a64_is_single_run_ones_any_rotate(pat, chunk)) return true;
  }
  return false;
}

static void emit_str_spill(const char* src_reg, OperandSize sz, size_t off)
{
  switch (sz)
  {
    case SIZE_QWORD: output_file("    str %s, [x29, -%zu]", src_reg, off); break;
    case SIZE_DWORD: output_file("    str %s, [x29, -%zu]", src_reg, off); break;
    case SIZE_WORD:  output_file("    strh %s, [x29, -%zu]", src_reg, off); break;
    case SIZE_BYTE:  output_file("    strb %s, [x29, -%zu]", src_reg, off); break;
    default: break;
  }
}

static void emit_ldr_spill(const char* dst_reg, OperandSize sz, size_t off)
{
  switch (sz)
  {
    case SIZE_QWORD: output_file("    ldr %s, [x29, -%zu]", dst_reg, off); break;
    case SIZE_DWORD: output_file("    ldr %s, [x29, -%zu]", dst_reg, off); break;
    case SIZE_WORD:  output_file("    ldrh %s, [x29, -%zu]", dst_reg, off); break;
    case SIZE_BYTE:  output_file("    ldrb %s, [x29, -%zu]", dst_reg, off); break;
    default: break;
  }
}

static void format_mem(char* buf, size_t bufsz, A64_Operand* op, A64_FUNC* fa, size_t cur_id)
{
  if (op->kind == A64_OP_MEM_RELATIVE)
  {
    // We'll emit ADRP+ADD before use; return [x9, #disp]
    snprintf(buf, bufsz, "[%s, #%d]", scratch_x9(SIZE_QWORD), op->mem.displacement);
    return;
  }
  const char* base = NULL;
  int disp = op->mem.displacement;
  if (op->mem.base)
  {
    if (op->mem.base->reg_type == a64_real_regs)
      base = a64_reg_name(op->mem.base->real_reg, SIZE_QWORD);
    else
    {
      size_t idx = get_vreg_index(op->mem.base);
      if (vreg_has_real_at(fa, idx, cur_id))
        base = a64_reg_name(vreg_real_at(fa, idx, cur_id), SIZE_QWORD);
      else
        base = NULL; // unresolved, caller must load into scratch
    }
  }
  if (base)
    snprintf(buf, bufsz, "[%s, #%d]", base, disp);
  else
    snprintf(buf, bufsz, "[%s, #%d]", "x29", disp); // fallback; caller should fixup
}

static void emit_prologue_full(IRFunc* func_ir, A64_FUNC* fa, size_t* out_alloc)
{
  // Prologue: create frame and save FP/LR
  output_file("    stp x29, x30, [sp, #-16]!");
  output_file("    mov x29, sp");

  // Save callee-saved used
  for (int r = (int)a64_x19; r <= (int)a64_x28; r++)
  {
    if (fa->used_registers & (1 << r))
      output_file("    str x%d, [sp, #-16]!", r);
  }

  size_t locals = func_ir->user_defined.stack_size + fa->spill_stack_size;
  size_t alloc = align16(locals);
  if (alloc)
    output_file("    sub sp, sp, %zu", alloc);
  if (out_alloc) *out_alloc = alloc;
}

static void emit_epilogue_full(A64_FUNC* fa, size_t alloc)
{
  if (alloc)
    output_file("    add sp, sp, %zu", alloc);
  // Restore callee-saved in reverse
  for (int r = (int)a64_x28; r >= (int)a64_x19; r--)
  {
    if (fa->used_registers & (1 << r))
      output_file("    ldr x%d, [sp], #16", r);
  }
  output_file("    ldp x29, x30, [sp], #16");
  output_file("    ret");
}

static void a64_emit_two_or_three(const char* mnem, A64_ASM* as, A64_FUNC* fa)
{
  // For arithmetic/logic/shift: 3-op; MOV/NEG/MVN are 2-op
  A64_Operand* d = &as->operands[0];
  A64_Operand* s1 = &as->operands[1];
  A64_Operand* s2 = &as->operands[2];
  OperandSize dsz = SIZE_DWORD;
  if (d->kind == A64_OP_REG && d->reg) dsz = d->reg->size ? d->reg->size : SIZE_DWORD;
  const char* dst = NULL, *src1 = NULL, *src2 = NULL;
  char buf1[64], buf2[64];

  // Resolve dst
  if (d->kind == A64_OP_REG)
  {
    if (d->constraint.has_fixed_register)
      dst = a64_reg_name(d->constraint.fixed_register, dsz);
    else if (d->reg->reg_type == a64_real_regs)
      dst = a64_reg_name(d->reg->real_reg, dsz);
    else
    {
      size_t idx = get_vreg_index(d->reg);
      if (vreg_has_real_at(fa, idx, as->insn_id))
        dst = a64_reg_name(vreg_real_at(fa, idx, as->insn_id), dsz);
      else
        dst = scratch_x10(dsz);
    }
  }

  // Resolve s1
  if (s1->kind == A64_OP_REG)
  {
    if (s1->constraint.has_fixed_register)
      src1 = a64_reg_name(s1->constraint.fixed_register, s1->reg ? s1->reg->size : dsz);
    else if (s1->reg->reg_type == a64_real_regs)
      src1 = a64_reg_name(s1->reg->real_reg, s1->reg->size ? s1->reg->size : dsz);
    else
    {
      size_t idx = get_vreg_index(s1->reg);
      if (vreg_has_real_at(fa, idx, as->insn_id))
        src1 = a64_reg_name(vreg_real_at(fa, idx, as->insn_id), s1->reg->size ? s1->reg->size : dsz);
      else
      {
        // load from spill
        size_t spill = vreg_spill_offset(fa, idx);
        emit_ldr_spill(scratch_x9(s1->reg->size), s1->reg->size, spill);
        src1 = scratch_x9(s1->reg->size);
      }
    }
    // If destination is 64-bit and src1 is 32-bit, extend into a 64-bit scratch.
    OperandSize s1sz = s1->reg ? (s1->reg->size ? s1->reg->size : dsz) : dsz;
    if (dsz == SIZE_QWORD && s1sz == SIZE_DWORD)
    {
      output_file("    sxtw %s, %s", scratch_x10(SIZE_QWORD), src1);
      src1 = scratch_x10(SIZE_QWORD);
    }
  }
  else if (s1->kind == A64_OP_IMM)
  {
    snprintf(buf1, sizeof(buf1), "#%lld", s1->imm);
    src1 = buf1;
  }

  // Resolve s2 if needed
  if (s2->kind == A64_OP_REG)
  {
    OperandSize s2sz = s2->reg->size ? s2->reg->size : dsz;
    if (s2->reg->reg_type == a64_real_regs)
      src2 = a64_reg_name(s2->reg->real_reg, s2sz);
    else
    {
      size_t idx = get_vreg_index(s2->reg);
      if (vreg_has_real_at(fa, idx, as->insn_id))
        src2 = a64_reg_name(vreg_real_at(fa, idx, as->insn_id), s2sz);
      else
      {
        size_t spill = vreg_spill_offset(fa, idx);
        emit_ldr_spill(scratch_x9(s2sz), s2sz, spill);
        src2 = scratch_x9(s2sz);
      }
    }
    // If destination is 64-bit and src2 is 32-bit, extend into 64-bit scratch.
    if (dsz == SIZE_QWORD && s2sz == SIZE_DWORD)
    {
      // Prefer signed extend to match C's default signed int semantics.
      output_file("    sxtw %s, %s", scratch_x9(SIZE_QWORD), src2);
      src2 = scratch_x9(SIZE_QWORD);
    }
  }
  else if (s2->kind == A64_OP_IMM)
  {
    bool use_imm = false;
    switch (as->kind)
    {
      case A64_ADD:
      case A64_SUB:
        use_imm = a64_is_addsub_imm_encodable(s2->imm);
        break;
      case A64_AND:
      case A64_ORR:
      case A64_EOR:
      {
        // Check AArch64 logical immediate encoding constraints
        OperandSize opsz = dsz; // instruction width follows dest
        uint64_t u = (opsz == SIZE_QWORD) ? (uint64_t)s2->imm : (uint64_t)((uint32_t)s2->imm);
        use_imm = a64_is_logical_imm(u, (opsz == SIZE_QWORD) ? 64 : 32);
        break;
      }
      default:
        use_imm = false; // safe default
        break;
    }
    if (use_imm)
    {
      // Print as hex for clarity with logical mask immediates
      if (dsz == SIZE_QWORD)
        snprintf(buf2, sizeof(buf2), "#0x%llx", (unsigned long long)((uint64_t)s2->imm));
      else
        snprintf(buf2, sizeof(buf2), "#0x%x", (unsigned int)((uint32_t)s2->imm));
      src2 = buf2;
    }
    else
    {
      // materialize immediate to scratch
      output_file("    mov %s, #%lld", scratch_x9(SIZE_QWORD), s2->imm);
      src2 = scratch_x9(dsz);
    }
  }

  if (!src2)
    output_file("    %s %s, %s", mnem, dst ? dst : scratch_x10(dsz), src1 ? src1 : scratch_x9(dsz));
  else
    output_file("    %s %s, %s, %s", mnem, dst ? dst : scratch_x10(dsz), src1 ? src1 : scratch_x9(dsz), src2);

  // Store back if dst is spilled vreg
  if (d->kind == A64_OP_REG && d->reg && d->reg->reg_type == a64_virtual_regs &&
      !vreg_has_real_at(fa, get_vreg_index(d->reg), as->insn_id))
  {
    size_t spill = vreg_spill_offset(fa, get_vreg_index(d->reg));
    emit_str_spill(dst ? dst : scratch_x10(dsz), dsz, spill);
  }
}

static void a64_emit_extend(A64_ASM* as, A64_FUNC* fa)
{
  // Unary: dst <- extend(src). Src must be a W-reg in syntax; dst can be W/X.
  const char* mnem = NULL;
  switch (as->kind)
  {
    case A64_SXTB: mnem = "sxtb"; break;
    case A64_SXTH: mnem = "sxth"; break;
    case A64_SXTW: mnem = "sxtw"; break;
    case A64_UXTB: mnem = "uxtb"; break;
    case A64_UXTH: mnem = "uxth"; break;
    case A64_UXTW: mnem = "uxtw"; break;
    default: mnem = "mov"; break;
  }

  A64_Operand* d = &as->operands[0];
  A64_Operand* s = &as->operands[1];
  OperandSize dsz = SIZE_DWORD;
  if (d->kind == A64_OP_REG && d->reg) dsz = d->reg->size ? d->reg->size : SIZE_DWORD;

  const char* dst = NULL; const char* src = NULL;

  // Resolve dst
  if (d->kind == A64_OP_REG)
  {
    if (d->constraint.has_fixed_register)
      dst = a64_reg_name(d->constraint.fixed_register, dsz);
    else if (d->reg->reg_type == a64_real_regs)
      dst = a64_reg_name(d->reg->real_reg, dsz);
    else
    {
      size_t idx = get_vreg_index(d->reg);
      if (vreg_has_real_at(fa, idx, as->insn_id))
        dst = a64_reg_name(vreg_real_at(fa, idx, as->insn_id), dsz);
      else
        dst = scratch_x10(dsz);
    }
  }

  // Resolve src (force W-reg naming)
  if (s->kind == A64_OP_REG)
  {
    if (s->reg->reg_type == a64_real_regs)
      src = a64_reg_name(s->reg->real_reg, SIZE_DWORD);
    else
    {
      size_t idx = get_vreg_index(s->reg);
      if (vreg_has_real_at(fa, idx, as->insn_id))
        src = a64_reg_name(vreg_real_at(fa, idx, as->insn_id), SIZE_DWORD);
      else
      {
        size_t spill = vreg_spill_offset(fa, idx);
        emit_ldr_spill(scratch_x9(SIZE_DWORD), SIZE_DWORD, spill);
        src = scratch_x9(SIZE_DWORD);
      }
    }
  }
  else if (s->kind == A64_OP_IMM)
  {
    // Load imm into W scratch then extend
    output_file("    mov %s, #%lld", scratch_x9(SIZE_DWORD), s->imm);
    src = scratch_x9(SIZE_DWORD);
  }

  output_file("    %s %s, %s", mnem, dst ? dst : scratch_x10(dsz), src ? src : scratch_x9(SIZE_DWORD));

  // Spill back if dst is a spilled vreg
  if (d->kind == A64_OP_REG && d->reg && d->reg->reg_type == a64_virtual_regs &&
      !vreg_has_real_at(fa, get_vreg_index(d->reg), as->insn_id))
  {
    size_t spill = vreg_spill_offset(fa, get_vreg_index(d->reg));
    emit_str_spill(dst ? dst : scratch_x10(dsz), dsz, spill);
  }
}

static void a64_emit_memop(A64_ASM* as, A64_FUNC* fa, bool is_load)
{
  A64_Operand* mop = &as->operands[1];
  OperandSize sz = mop->mem.mov_size ? mop->mem.mov_size : SIZE_QWORD;
  const char* mnem = NULL;
  switch (sz)
  {
    case SIZE_QWORD: mnem = is_load ? "ldr" : "str"; break;
    case SIZE_DWORD: mnem = is_load ? "ldr" : "str"; break; // w-reg name drives width
    case SIZE_WORD:  mnem = is_load ? "ldrh" : "strh"; break;
    case SIZE_BYTE:  mnem = is_load ? "ldrb" : "strb"; break;
    default: mnem = is_load ? "ldr" : "str"; break;
  }

  // Handle OP_MEM_RELATIVE via address materialization to x9
  if (mop->kind == A64_OP_MEM_RELATIVE)
  {
    // Prefer pseudo-literal: ldr x9, =sym (assembler may choose adrp+add or literal pool)
    if (mop->mem.var_name && mop->mem.var_name_len)
    {
      output_file("    ldr %s, =%.*s", scratch_x9(SIZE_QWORD), (int)mop->mem.var_name_len, mop->mem.var_name);
    }
  }

  char addr[64];
  bool unresolved = false;
  if (mop->kind == A64_OP_MEM && mop->mem.base && mop->mem.base->reg_type == a64_virtual_regs)
  {
    size_t idx = get_vreg_index(mop->mem.base);
    if (!vreg_has_real_at(fa, idx, as->insn_id)) unresolved = true;
  }
  if (unresolved && mop->kind == A64_OP_MEM)
  {
    size_t spill = vreg_spill_offset(fa, get_vreg_index(mop->mem.base));
    output_file("    ldr %s, [x29, -%zu]", scratch_x9(SIZE_QWORD), spill);
    snprintf(addr, sizeof(addr), "[%s, #%d]", scratch_x9(SIZE_QWORD), mop->mem.displacement);
  }
  else
  {
    format_mem(addr, sizeof(addr), mop, fa, as->insn_id);
  }

  if (is_load)
  {
    const char* dst = NULL;
    if (as->operands[0].kind == A64_OP_REG)
    {
      if (as->operands[0].reg->reg_type == a64_real_regs)
        dst = a64_reg_name(as->operands[0].reg->real_reg, as->operands[0].reg->size);
      else
      {
        size_t idx = get_vreg_index(as->operands[0].reg);
        if (vreg_has_real_at(fa, idx, as->insn_id))
          dst = a64_reg_name(vreg_real_at(fa, idx, as->insn_id), as->operands[0].reg->size);
        else
          dst = scratch_x10(as->operands[0].reg->size);
      }
    }
    output_file("    %s %s, %s", mnem, dst, addr);
    if (as->operands[0].kind == A64_OP_REG && as->operands[0].reg && as->operands[0].reg->reg_type == a64_virtual_regs &&
        !vreg_has_real_at(fa, get_vreg_index(as->operands[0].reg), as->insn_id))
    {
      size_t spill = vreg_spill_offset(fa, get_vreg_index(as->operands[0].reg));
      emit_str_spill(dst, as->operands[0].reg->size, spill);
    }
  }
  else
  {
    // store: src operand can be vreg or real reg
    const char* src = NULL;
    if (as->operands[0].kind == A64_OP_REG)
    {
      if (as->operands[0].reg->reg_type == a64_real_regs)
        src = a64_reg_name(as->operands[0].reg->real_reg, as->operands[0].reg->size);
      else
      {
        size_t idx = get_vreg_index(as->operands[0].reg);
        if (vreg_has_real_at(fa, idx, as->insn_id))
          src = a64_reg_name(vreg_real_at(fa, idx, as->insn_id), as->operands[0].reg->size);
        else
        {
          size_t spill = vreg_spill_offset(fa, idx);
          emit_ldr_spill(scratch_x10(as->operands[0].reg->size), as->operands[0].reg->size, spill);
          src = scratch_x10(as->operands[0].reg->size);
        }
      }
    }
    else if (as->operands[0].kind == A64_OP_IMM)
    {
      // load imm into scratch and store
      output_file("    mov %s, #%lld", scratch_x10(SIZE_QWORD), as->operands[0].imm);
      src = scratch_x10(SIZE_QWORD);
    }
    output_file("    %s %s, %s", mnem, src, addr);
  }
}

static void a64_emit_cmp(A64_ASM* as, A64_FUNC* fa)
{
  A64_Operand* lhs = &as->operands[0];
  A64_Operand* rhs = &as->operands[1];
  const char* l = NULL; const char* r = NULL; OperandSize sz = SIZE_DWORD;
  if (lhs->kind == A64_OP_REG)
  {
    sz = lhs->reg ? lhs->reg->size : SIZE_DWORD;
    if (lhs->reg->reg_type == a64_real_regs)
      l = a64_reg_name(lhs->reg->real_reg, sz);
    else
    {
      size_t idx = get_vreg_index(lhs->reg);
      if (vreg_has_real_at(fa, idx, as->insn_id))
        l = a64_reg_name(vreg_real_at(fa, idx, as->insn_id), sz);
      else
      {
        size_t spill = vreg_spill_offset(fa, idx);
        emit_ldr_spill(scratch_x9(sz), sz, spill);
        l = scratch_x9(sz);
      }
    }
  }
  if (rhs->kind == A64_OP_REG)
  {
    OperandSize ssz = rhs->reg ? rhs->reg->size : sz;
    if (rhs->reg->reg_type == a64_real_regs)
      r = a64_reg_name(rhs->reg->real_reg, ssz);
    else
    {
      size_t idx = get_vreg_index(rhs->reg);
      if (vreg_has_real_at(fa, idx, as->insn_id))
        r = a64_reg_name(vreg_real_at(fa, idx, as->insn_id), ssz);
      else
      {
        size_t spill = vreg_spill_offset(fa, idx);
        emit_ldr_spill(scratch_x10(ssz), ssz, spill);
        r = scratch_x10(ssz);
      }
    }
  }
  else if (rhs->kind == A64_OP_IMM)
  {
    if (a64_is_addsub_imm_encodable(rhs->imm))
    {
      snprintf(a64_cmp_imm_buf, sizeof(a64_cmp_imm_buf), "#%lld", rhs->imm);
      r = a64_cmp_imm_buf;
      output_file("    cmp %s, %s", l ? l : scratch_x9(sz), r);
      return;
    }
    // Fallback: load immediate and compare register
    output_file("    mov %s, #%lld", scratch_x10(SIZE_QWORD), rhs->imm);
    r = scratch_x10(sz);
  }
  output_file("    cmp %s, %s", l ? l : scratch_x9(sz), r ? r : scratch_x10(sz));
}

static void a64_emit_cset(A64_ASMKind k, A64_ASM* as, A64_FUNC* fa)
{
  const char* cond = NULL;
  switch (k)
  {
    case A64_CSET_EQ: cond = "eq"; break; case A64_CSET_NE: cond = "ne"; break;
    case A64_CSET_LT: cond = "lt"; break; case A64_CSET_LO: cond = "lo"; break;
    case A64_CSET_LE: cond = "le"; break; case A64_CSET_LS: cond = "ls"; break;
    default: cond = "eq"; break;
  }
  // Dest must be register; prefer 32-bit result
  A64_Operand* d = &as->operands[0];
  OperandSize dsz = d->reg ? d->reg->size : SIZE_DWORD;
  if (dsz == SIZE_QWORD) dsz = SIZE_DWORD;
  const char* dst = NULL;
  if (d->constraint.has_fixed_register)
    dst = a64_reg_name(d->constraint.fixed_register, dsz);
  else if (d->kind == A64_OP_REG)
  {
    if (d->reg->reg_type == a64_real_regs)
      dst = a64_reg_name(d->reg->real_reg, dsz);
    else
    {
      size_t idx = get_vreg_index(d->reg);
      if (vreg_has_real_at(fa, idx, as->insn_id))
        dst = a64_reg_name(vreg_real_at(fa, idx, as->insn_id), dsz);
      else
        dst = scratch_x10(dsz);
    }
  }
  output_file("    cset %s, %s", dst, cond);
  if (d->kind == A64_OP_REG && d->reg && d->reg->reg_type == a64_virtual_regs &&
      !vreg_has_real_at(fa, get_vreg_index(d->reg), as->insn_id))
  {
    size_t spill = vreg_spill_offset(fa, get_vreg_index(d->reg));
    emit_str_spill(dst, dsz, spill);
  }
}

void a64_emit_function(IRFunc* func_ir, A64_FUNC* fa)
{
  emit_function_header(func_ir);

  size_t alloc = 0;
  emit_prologue_full(func_ir, fa, &alloc);

  // Default return value for main (C: reaching end returns 0)
  if (func_ir->user_defined.function_name_size == 4 &&
      strncmp(func_ir->user_defined.function_name, "main", 4) == 0)
  {
    output_file("    mov w0, 0");
  }

  // Create a unified return label to support early returns
  char ret_label[128];
  snprintf(ret_label, sizeof(ret_label), ".Lret_%.*s",
           (int)func_ir->user_defined.function_name_size,
           func_ir->user_defined.function_name);

  bool saw_return = false;
  for (size_t bi = 1; bi <= vector_size(fa->asm_blocks); bi++)
  {
    A64_Blocks* blk = vector_peek_at(fa->asm_blocks, bi);
    for (size_t ji = 1; ji <= vector_size(blk->asm_list); ji++)
    {
      A64_ASM* as = vector_peek_at(blk->asm_list, ji);
      output_file("# a64 kind=%d", (int)as->kind);
      switch (as->kind)
      {
        case A64_LABEL:
          output_file("%s:", as->jump_target_label);
          break;
        case A64_MOV:
          a64_emit_two_or_three("mov", as, fa);
          break;
        case A64_ADD: a64_emit_two_or_three("add", as, fa); break;
        case A64_SUB: a64_emit_two_or_three("sub", as, fa); break;
        case A64_MUL: a64_emit_two_or_three("mul", as, fa); break;
        case A64_SDIV: a64_emit_two_or_three("sdiv", as, fa); break;
        case A64_UDIV: a64_emit_two_or_three("udiv", as, fa); break;
        case A64_AND: a64_emit_two_or_three("and", as, fa); break;
        case A64_ORR: a64_emit_two_or_three("orr", as, fa); break;
        case A64_EOR: a64_emit_two_or_three("eor", as, fa); break;
        case A64_LSLV: a64_emit_two_or_three("lslv", as, fa); break;
        case A64_LSRV: a64_emit_two_or_three("lsrv", as, fa); break;
        case A64_ASRV: a64_emit_two_or_three("asrv", as, fa); break;
        case A64_NEG: a64_emit_two_or_three("neg", as, fa); break;
        case A64_MVN: a64_emit_two_or_three("mvn", as, fa); break;
        case A64_SXTB: case A64_SXTH: case A64_SXTW:
        case A64_UXTB: case A64_UXTH: case A64_UXTW:
          a64_emit_extend(as, fa); break;
        case A64_LDR: a64_emit_memop(as, fa, true); break;
        case A64_STR: a64_emit_memop(as, fa, false); break;
        case A64_ADRP:
        {
          // Resolve destination register
          const char* dst = NULL;
          OperandSize dsz = SIZE_QWORD;
          if (as->operands[0].kind == A64_OP_REG && as->operands[0].reg)
            dsz = as->operands[0].reg->size ? as->operands[0].reg->size : SIZE_QWORD;
          if (as->operands[0].kind == A64_OP_REG)
          {
            if (as->operands[0].reg->reg_type == a64_real_regs)
              dst = a64_reg_name(as->operands[0].reg->real_reg, dsz);
            else
            {
              size_t idx = get_vreg_index(as->operands[0].reg);
              if (vreg_has_real_at(fa, idx, as->insn_id))
                dst = a64_reg_name(vreg_real_at(fa, idx, as->insn_id), dsz);
              else
                dst = scratch_x10(dsz);
            }
          }
          if (as->operands[1].kind == A64_OP_MEM_RELATIVE && as->operands[1].mem.var_name)
          {
            // Try to fold ADRP+ADD into single materialization. Look ahead.
            // If next insn is ADD_ADDR for the same symbol, emit either ADR (for local labels) or LDR =sym.
            // Then skip the next ADD_ADDR by advancing the loop counter here.
            // NOTE: We are inside the per-block loop; we can look ahead by reading ji+1.
            // The variables 'blk' and 'ji' are in scope here.
            // Identify next instruction
            if (ji + 1 <= vector_size(blk->asm_list))
            {
              A64_ASM* nxt = vector_peek_at(blk->asm_list, ji + 1);
              bool same = false;
              if (nxt && nxt->kind == A64_ADD_ADDR)
              {
                if (nxt->operands[1].kind == A64_OP_MEM_RELATIVE && nxt->operands[1].mem.var_name &&
                    nxt->operands[1].mem.var_name_len == as->operands[1].mem.var_name_len &&
                    strncmp(nxt->operands[1].mem.var_name, as->operands[1].mem.var_name,
                            (int)as->operands[1].mem.var_name_len) == 0)
                  same = true;
              }
              if (same)
              {
                // Heuristic: local labels (start with '.') are likely near; use ADR. Otherwise use literal LDR pseudo.
                if (as->operands[1].mem.var_name_len && as->operands[1].mem.var_name[0] == '.')
                  output_file("    adr %s, %.*s", dst,
                              (int)as->operands[1].mem.var_name_len, as->operands[1].mem.var_name);
                else
                  output_file("    ldr %s, =%.*s", dst,
                              (int)as->operands[1].mem.var_name_len, as->operands[1].mem.var_name);
                // Apply additional constant displacement if present
                int add_disp = as->operands[1].mem.displacement;
                if (add_disp)
                {
                  if (a64_is_addsub_imm_encodable(add_disp))
                    output_file("    add %s, %s, #%d", dst, dst, add_disp);
                  else
                  {
                    output_file("    mov %s, #%d", scratch_x9(SIZE_QWORD), add_disp);
                    output_file("    add %s, %s, %s", dst, dst, scratch_x9(SIZE_QWORD));
                  }
                }
                // If dst is a spilled vreg, write back now (use current 'as' insn_id)
                if (as->operands[0].kind == A64_OP_REG && as->operands[0].reg &&
                    as->operands[0].reg->reg_type == a64_virtual_regs &&
                    !vreg_has_real_at(fa, get_vreg_index(as->operands[0].reg), as->insn_id))
                {
                  size_t spill = vreg_spill_offset(fa, get_vreg_index(as->operands[0].reg));
                  emit_str_spill(dst, dsz, spill);
                }
                // Skip the following ADD_ADDR
                ji++;
                break;
              }
            }
            // Fallback: keep ADRP (pair with subsequent ADD_ADDR)
            output_file("    adrp %s, %.*s", dst,
                        (int)as->operands[1].mem.var_name_len, as->operands[1].mem.var_name);
          }
          break;
        }
        case A64_ADD_ADDR:
        {
          const char* dst = NULL;
          OperandSize dsz = SIZE_QWORD;
          if (as->operands[0].kind == A64_OP_REG && as->operands[0].reg)
            dsz = as->operands[0].reg->size ? as->operands[0].reg->size : SIZE_QWORD;
          if (as->operands[0].kind == A64_OP_REG)
          {
            if (as->operands[0].reg->reg_type == a64_real_regs)
              dst = a64_reg_name(as->operands[0].reg->real_reg, dsz);
            else
            {
              size_t idx = get_vreg_index(as->operands[0].reg);
              if (vreg_has_real_at(fa, idx, as->insn_id))
                dst = a64_reg_name(vreg_real_at(fa, idx, as->insn_id), dsz);
              else
                dst = scratch_x10(dsz);
            }
          }
          if (as->operands[1].kind == A64_OP_MEM_RELATIVE && as->operands[1].mem.var_name)
          {
            output_file("    add %s, %s, :lo12:%.*s", dst, dst,
                        (int)as->operands[1].mem.var_name_len, as->operands[1].mem.var_name);
            // Apply constant displacement if present
            int add_disp = as->operands[1].mem.displacement;
            if (add_disp)
            {
              if (a64_is_addsub_imm_encodable(add_disp))
                output_file("    add %s, %s, #%d", dst, dst, add_disp);
              else
              {
                output_file("    mov %s, #%d", scratch_x9(SIZE_QWORD), add_disp);
                output_file("    add %s, %s, %s", dst, dst, scratch_x9(SIZE_QWORD));
              }
            }
          }
          // Store back if needed
          if (as->operands[0].kind == A64_OP_REG && as->operands[0].reg && as->operands[0].reg->reg_type == a64_virtual_regs &&
              !vreg_has_real_at(fa, get_vreg_index(as->operands[0].reg), as->insn_id))
          {
            size_t spill = vreg_spill_offset(fa, get_vreg_index(as->operands[0].reg));
            emit_str_spill(dst, dsz, spill);
          }
          break;
        }
        case A64_CMP: a64_emit_cmp(as, fa); break;
        case A64_CSET_EQ: case A64_CSET_NE: case A64_CSET_LT:
        case A64_CSET_LO: case A64_CSET_LE: case A64_CSET_LS:
          a64_emit_cset(as->kind, as, fa); break;
        case A64_B: output_file("    b %s", as->jump_target_label); break;
        case A64_BEQ: output_file("    b.eq %s", as->jump_target_label); break;
        case A64_BNE: output_file("    b.ne %s", as->jump_target_label); break;
        case A64_CALL:
        {
          // Flush split mappings at call boundary
          size_t nregs = vector_size(fa->virtual_regs);
          for (size_t vi = 1; vi <= nregs; vi++)
          {
            size_t* realp = vector_peek_at(fa->vreg_real, vi);
            size_t* rendp = fa->vreg_real_end ? vector_peek_at(fa->vreg_real_end, vi) : NULL;
            size_t* slotp = vector_peek_at(fa->vreg_stack_slot, vi);
            A64_REG* vinfo = vector_peek_at(fa->virtual_regs, vi);
            if (realp && rendp && slotp && vinfo)
            {
              a64_register_name r = (a64_register_name)(*realp);
              size_t rend = *rendp;
              size_t slot = *slotp;
              if (r != a64_reg_reserved && rend != 0 && rend <= as->insn_id && slot)
              {
                emit_str_spill(a64_reg_name(r, vinfo->size ? vinfo->size : SIZE_QWORD),
                               vinfo->size ? vinfo->size : SIZE_QWORD, slot);
              }
            }
          }
          output_file("    bl %s", as->jump_target_label);
          break;
        }
        case A64_BUILTIN_ASM:
          output_file("    %.*s", (int)as->builtin_asm.asm_len, as->builtin_asm.asm_str);
          break;
        case A64_RETURN:
        {
          saw_return = true;
          // If a return value was provided, move it into x0/w0 right before returning.
          if (as->operands[0].kind == A64_OP_REG && as->operands[0].reg)
          {
            OperandSize rsz = as->operands[0].reg->size ? as->operands[0].reg->size : SIZE_DWORD;
            const char* src = NULL;
            if (as->operands[0].reg->reg_type == a64_real_regs)
              src = a64_reg_name(as->operands[0].reg->real_reg, rsz);
            else
            {
              size_t idx = get_vreg_index(as->operands[0].reg);
              if (vreg_has_real_at(fa, idx, as->insn_id))
                src = a64_reg_name(vreg_real_at(fa, idx, as->insn_id), rsz);
              else
              {
                // Load from spill into scratch then move
                size_t spill = vreg_spill_offset(fa, idx);
                emit_ldr_spill(scratch_x9(rsz), rsz, spill);
                src = scratch_x9(rsz);
              }
            }
            output_file("    mov %s, %s", a64_reg_name(a64_x0, rsz), src);
          }
          output_file("    b %s", ret_label);
          break;
        }
        case A64_UNUSED:
          break;
        default:
          break;
      }
    }
  }
  if (!saw_return && func_ir->user_defined.function_name_size == 4 &&
      strncmp(func_ir->user_defined.function_name, "main", 4) == 0)
  {
    output_file("    mov w0, 0");
  }
  output_file("%s:", ret_label);
  emit_epilogue_full(fa, alloc);
}
