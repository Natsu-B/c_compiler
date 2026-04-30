#include "include/x64_emit.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#include <string.h>
#endif

#include "include/generator.h"
#include "include/vector.h"
#include "include/generator_x64.h"

static const char* regs_64[] = {"rdi", "rsi", "rdx", "rcx", "r8",  "r9",
                                "r10", "r11", "rax", "rbx", "rsp", "rbp",
                                "r12", "r13", "r14", "r15"};
static const char* regs_32[] = {"edi",  "esi",  "edx",  "ecx", "r8d", "r9d",
                                "r10d", "r11d", "eax",  "ebx", "esp", "ebp",
                                "r12d", "r13d", "r14d", "r15d"};
static const char* regs_16[] = {"di",   "si",   "dx",   "cx",  "r8w", "r9w",
                                "r10w", "r11w", "ax",   "bx",  "sp",  "bp",
                                "r12w", "r13w", "r14w", "r15w"};
static const char* regs_8[] = {"dil",  "sil",  "dl",   "cl",  "r8b", "r9b",
                               "r10b", "r11b", "al",   "bl",  "spl", "bpl",
                               "r12b", "r13b", "r14b", "r15b"};

void x64_emit_file_preamble(void)
{
  // x86_64 uses Intel syntax in this compiler
  output_file("    .intel_syntax noprefix\n");
}

static const char* reg_name(enum register_name r, OperandSize sz)
{
  switch (sz)
  {
    case SIZE_QWORD: return regs_64[r];
    case SIZE_DWORD: return regs_32[r];
    case SIZE_WORD: return regs_16[r];
    case SIZE_BYTE: return regs_8[r];
    default: return regs_64[r];
  }
}

static const char* scratch_r10(OperandSize sz)
{
  return reg_name(r10, sz);
}

static const char* scratch_r11(OperandSize sz)
{
  return reg_name(r11, sz);
}

static size_t get_vreg_index(X64_REG* r)
{
  return r->reg_id + 1;  // virtual_regs is indexed by IR reg + 1
}

static bool vreg_has_real_at(X64_FUNC* fx, size_t idx, size_t cur_id)
{
  size_t* real = vector_peek_at(fx->vreg_real, idx);
  if (!real || *real == register_reserved) return false;
  size_t* endp = fx->vreg_real_end ? vector_peek_at(fx->vreg_real_end, idx) : NULL;
  size_t end = endp ? *endp : 0;
  if (end == 0) return true;
  if (cur_id == 0) return true;  // if no context, assume valid
  return cur_id <= end;
}

static enum register_name vreg_real_at(X64_FUNC* fx, size_t idx, size_t cur_id)
{
  if (!vreg_has_real_at(fx, idx, cur_id)) return register_reserved;
  size_t* real = vector_peek_at(fx->vreg_real, idx);
  return (enum register_name)(real ? *real : register_reserved);
}

static bool remat_lea_address(X64_FUNC* fx, size_t idx, int extra_disp,
                              char* out, size_t outsz)
{
  size_t* islea = fx->vreg_is_remat_lea ? vector_peek_at(fx->vreg_is_remat_lea, idx) : NULL;
  if (!islea || !*islea) return false;
  size_t* isrbp = vector_peek_at(fx->vreg_lea_is_rbp, idx);
  size_t* isrip = vector_peek_at(fx->vreg_lea_is_rip, idx);
  int* disp = vector_peek_at(fx->vreg_lea_disp, idx);
  char** vname = vector_peek_at(fx->vreg_lea_var_name, idx);
  size_t* vlen = vector_peek_at(fx->vreg_lea_var_name_len, idx);
  if (!isrbp || !isrip || !disp) return false;
  long total = (long)(*disp) + (long)extra_disp;
  if (*isrbp)
  {
    snprintf(out, outsz, "[rbp%+ld]", total);
    return true;
  }
  if (*isrip)
  {
    if (vname && *vname && vlen)
    {
      if (total)
        snprintf(out, outsz, "[rip + %.*s%+ld]", (int)(*vlen), *vname, total);
      else
        snprintf(out, outsz, "[rip + %.*s]", (int)(*vlen), *vname);
    }
    else
    {
      snprintf(out, outsz, "[rip%+ld]", total);
    }
    return true;
  }
  return false;
}

static size_t vreg_spill_offset(X64_FUNC* fx, size_t idx)
{
  size_t* slot = vector_peek_at(fx->vreg_stack_slot, idx);
  return slot ? *slot : 0;
}

static bool operand_maps_to_mem_at(X64_Operand* op, X64_FUNC* fx, size_t cur_id)
{
  if (op->kind == OP_MEM || op->kind == OP_MEM_RELATIVE) return true;
  if (op->kind == OP_REG && op->reg && op->reg->reg_type == virtual_regs)
  {
    size_t idx = get_vreg_index(op->reg);
    return !vreg_has_real_at(fx, idx, cur_id);
  }
  return false;
}

static void emit_mov_store_spill(const char* src_reg, OperandSize sz, size_t spill)
{
  switch (sz)
  {
    case SIZE_QWORD:
      output_file("    mov QWORD PTR [rbp-%zu], %s", spill, src_reg);
      break;
    case SIZE_DWORD:
      output_file("    mov DWORD PTR [rbp-%zu], %s", spill, src_reg);
      break;
    case SIZE_WORD:
      output_file("    mov WORD PTR [rbp-%zu], %s", spill, src_reg);
      break;
    case SIZE_BYTE:
      output_file("    mov BYTE PTR [rbp-%zu], %s", spill, src_reg);
      break;
    default: break;
  }
}

static void emit_mov_load_spill(const char* dst_reg, OperandSize sz, size_t spill)
{
  switch (sz)
  {
    case SIZE_QWORD:
      output_file("    mov %s, QWORD PTR [rbp-%zu]", dst_reg, spill);
      break;
    case SIZE_DWORD:
      output_file("    mov %s, DWORD PTR [rbp-%zu]", dst_reg, spill);
      break;
    case SIZE_WORD:
      output_file("    mov %s, WORD PTR [rbp-%zu]", dst_reg, spill);
      break;
    case SIZE_BYTE:
      output_file("    mov %s, BYTE PTR [rbp-%zu]", dst_reg, spill);
      break;
    default: break;
  }
}

static void emit_two_operand_line(const char* mnem, const char* dst,
                                  const char* src)
{
  fprintf(fout, "    ");
  fprintf(fout, "%s", mnem);
  fprintf(fout, " ");
  fprintf(fout, "%s", dst);
  fprintf(fout, ", ");
  fprintf(fout, "%s", src);
  fprintf(fout, "\n");
}

static void emit_one_operand_line(const char* mnem, const char* operand)
{
  fprintf(fout, "    ");
  fprintf(fout, "%s", mnem);
  fprintf(fout, " ");
  fprintf(fout, "%s", operand);
  fprintf(fout, "\n");
}

static void emit_div_operand_line(bool is_unsigned, const char* operand)
{
  if (is_unsigned)
    fprintf(fout, "    div ");
  else
    fprintf(fout, "    idiv ");
  fprintf(fout, "%s", operand);
  fprintf(fout, "\n");
}

static void sync_split_vregs_before_insn(X64_FUNC* fx, size_t insn_id)
{
  if (!insn_id || !fx->vreg_real_end) return;

  size_t nregs = vector_size(fx->virtual_regs);
  for (size_t vi = 1; vi <= nregs; vi++)
  {
    size_t* realp = vector_peek_at(fx->vreg_real, vi);
    size_t* rendp = vector_peek_at(fx->vreg_real_end, vi);
    size_t* slotp = vector_peek_at(fx->vreg_stack_slot, vi);
    bool should_sync = false;
    size_t real = register_reserved;
    size_t rend = 0;
    size_t slot = 0;

    if (realp && rendp && slotp)
    {
      real = *realp;
      rend = *rendp;
      slot = *slotp;
      if (real != register_reserved && slot != 0 && rend != 0 &&
          rend + 1 == insn_id)
        should_sync = true;
    }

    if (should_sync)
    {
      X64_REG* vinfo = vector_peek_at(fx->virtual_regs, vi);
      OperandSize size = SIZE_QWORD;
      if (vinfo && vinfo->size) size = vinfo->size;
      emit_mov_store_spill(reg_name((enum register_name)real, size), size,
                           slot);
    }
  }
}

static void format_mem(char* buf, size_t bufsz, X64_Operand* op, X64_FUNC* fx, size_t cur_id)
{
  // RIP-relative addressing for global symbols
  if (op->kind == OP_MEM_RELATIVE)
  {
    int disp = op->mem.displacement;
    if (op->mem.var_name && op->mem.var_name_len)
    {
      if (disp)
        snprintf(buf, bufsz, "[rip + %.*s%+d]", (int)op->mem.var_name_len,
                 op->mem.var_name, disp);
      else
        snprintf(buf, bufsz, "[rip + %.*s]", (int)op->mem.var_name_len,
                 op->mem.var_name);
    }
    else
    {
      snprintf(buf, bufsz, "[rip%+d]", disp);
    }
    return;
  }
  // Only base + disp supported here (index/scale optional)
  const char* base = NULL;
  int disp = op->mem.displacement;
  if (op->mem.base)
  {
    if (op->mem.base->reg_type == real_regs)
      base = reg_name(op->mem.base->real_reg, SIZE_QWORD);
    else
    {
      size_t idx = get_vreg_index(op->mem.base);
      if (vreg_has_real_at(fx, idx, cur_id))
        base = reg_name(vreg_real_at(fx, idx, cur_id), SIZE_QWORD);
      else
      {
        // Try to rematerialize base address (LEA) when available
        char addr[64];
        if (remat_lea_address(fx, idx, op->mem.displacement, addr, sizeof(addr)))
        {
          snprintf(buf, bufsz, "%s", addr);
          return;
        }
        base = NULL;  // require pre-load into scratch (handled by caller)
      }
    }
  }
  if (base)
    snprintf(buf, bufsz, "[%s%+d]", base, disp);
  else
    snprintf(buf, bufsz, "[rbp%+d]", disp);  // fallback, caller should have loaded
}

static bool requires_reg_dest(X64_ASMKind k)
{
  switch (k)
  {
    case X64_LEA:
    case X64_MOVSX:
    case X64_MOVSXD:
    case X64_MOVZX:
    case X64_IMUL:  // two-operand imul requires a register destination
      return true;
    default: return false;
  }
}

static void emit_two_op(const char* mnem, X64_ASM* as, X64_FUNC* fx)
{
  X64_Operand* dst = &as->operands[0];
  X64_Operand* src = &as->operands[1];
  bool dst_mem = operand_maps_to_mem_at(dst, fx, as->insn_id);
  bool src_mem = operand_maps_to_mem_at(src, fx, as->insn_id);
  bool src_is_spill_mem = false;
  bool src_in_scratch_r10 = false;
  char src_spill_buf[64];
  if (src->kind == OP_REG && src->reg && src->reg->reg_type == virtual_regs &&
      !vreg_has_real_at(fx, get_vreg_index(src->reg), as->insn_id))
  {
    size_t spill = vreg_spill_offset(fx, get_vreg_index(src->reg));
    if (spill)
    {
      OperandSize ssz = src->reg->size ? src->reg->size : SIZE_QWORD;
      switch (ssz)
      {
        case SIZE_QWORD: snprintf(src_spill_buf, sizeof(src_spill_buf), "QWORD PTR [rbp-%zu]", spill); break;
        case SIZE_DWORD: snprintf(src_spill_buf, sizeof(src_spill_buf), "DWORD PTR [rbp-%zu]", spill); break;
        case SIZE_WORD: snprintf(src_spill_buf, sizeof(src_spill_buf), "WORD PTR [rbp-%zu]", spill); break;
        case SIZE_BYTE: snprintf(src_spill_buf, sizeof(src_spill_buf), "BYTE PTR [rbp-%zu]", spill); break;
        default: snprintf(src_spill_buf, sizeof(src_spill_buf), "QWORD PTR [rbp-%zu]", spill); break;
      }
      src_is_spill_mem = true;
    }
  }

  // Fast-path: direct store to spilled vreg for MOV when legal.
  // If destination is a spilled virtual register (no real reg assigned),
  // and source is an immediate or a real register, emit a single memory
  // store instead of using scratch registers.
  if (as->kind == X64_MOV && dst->kind == OP_REG && dst->reg &&
      dst->reg->reg_type == virtual_regs)
  {
    size_t dst_idx = get_vreg_index(dst->reg);
    if (!vreg_has_real_at(fx, dst_idx, as->insn_id))
    {
      size_t spill = vreg_spill_offset(fx, dst_idx);
      OperandSize store_sz = dst->reg->size ? dst->reg->size : SIZE_QWORD;
      // Source is immediate: mov [spill], imm
      if (src->kind == OP_IMM)
      {
        switch (store_sz)
        {
          case SIZE_QWORD:
            output_file("    mov QWORD PTR [rbp-%zu], %lld", spill, src->imm);
            break;
          case SIZE_DWORD:
            output_file("    mov DWORD PTR [rbp-%zu], %lld", spill, src->imm);
            break;
          case SIZE_WORD:
            output_file("    mov WORD PTR [rbp-%zu], %lld", spill, src->imm);
            break;
          case SIZE_BYTE:
            output_file("    mov BYTE PTR [rbp-%zu], %lld", spill, src->imm);
            break;
          default:
            output_file("    mov QWORD PTR [rbp-%zu], %lld", spill, src->imm);
            break;
        }
        return;
      }
      // Source is a register with a real mapping: mov [spill], <reg>
      if (src->kind == OP_REG && src->reg)
      {
        enum register_name rr = register_reserved;
        if (src->reg->reg_type == real_regs)
        {
          rr = src->reg->real_reg;
        }
        else if (src->reg->reg_type == virtual_regs)
        {
          size_t sidx = get_vreg_index(src->reg);
          if (vreg_has_real_at(fx, sidx, as->insn_id))
          {
            rr = vreg_real_at(fx, sidx, as->insn_id);
          }
        }
        if (rr != register_reserved)
        {
          // Use destination width for the store; source reg will be printed
          // with a matching width below.
          const char* sr = reg_name(rr, store_sz);
          switch (store_sz)
          {
            case SIZE_QWORD:
              output_file("    mov QWORD PTR [rbp-%zu], %s", spill, sr);
              break;
            case SIZE_DWORD:
              output_file("    mov DWORD PTR [rbp-%zu], %s", spill, sr);
              break;
            case SIZE_WORD:
              output_file("    mov WORD PTR [rbp-%zu], %s", spill, sr);
              break;
            case SIZE_BYTE:
              output_file("    mov BYTE PTR [rbp-%zu], %s", spill, sr);
              break;
            default:
              output_file("    mov QWORD PTR [rbp-%zu], %s", spill, sr);
              break;
          }
          return;
        }
      }
      // Otherwise fall through to generic path which will use scratch.
    }
  }

  // If both are memory-like, load src into scratch r10 for ops that require it
  const char* src_text = NULL;
  char src_buf[128];
  // MOV fast-path: reg <- spilled vreg or rematerializable immediate
  if (as->kind == X64_MOV && dst->kind == OP_REG && dst->reg &&
      dst->reg->reg_type == real_regs && src->kind == OP_REG && src->reg &&
      src->reg->reg_type == virtual_regs &&
      !vreg_has_real_at(fx, get_vreg_index(src->reg), as->insn_id))
  {
    // Try rematerialize immediate
    size_t sidx = get_vreg_index(src->reg);
    size_t* is_imm = fx->vreg_is_remat_imm ? vector_peek_at(fx->vreg_is_remat_imm, sidx) : NULL;
    long long* imm_valp = fx->vreg_remat_imm_value ? vector_peek_at(fx->vreg_remat_imm_value, sidx) : NULL;
    if (is_imm && *is_imm && imm_valp)
    {
      snprintf(src_buf, sizeof(src_buf), "%lld", *imm_valp);
      emit_two_operand_line(
          "mov", reg_name(dst->reg->real_reg,
                          dst->reg->size ? dst->reg->size : SIZE_DWORD),
          src_buf);
      return;
    }
    // Use spill slot as memory source
    size_t spill = vreg_spill_offset(fx, sidx);
    if (spill)
    {
      OperandSize ssz = src->reg->size ? src->reg->size : SIZE_QWORD;
      char m[64];
      switch (ssz)
      {
        case SIZE_QWORD: snprintf(m, sizeof(m), "QWORD PTR [rbp-%zu]", spill); break;
        case SIZE_DWORD: snprintf(m, sizeof(m), "DWORD PTR [rbp-%zu]", spill); break;
        case SIZE_WORD: snprintf(m, sizeof(m), "WORD PTR [rbp-%zu]", spill); break;
        case SIZE_BYTE: snprintf(m, sizeof(m), "BYTE PTR [rbp-%zu]", spill); break;
        default: snprintf(m, sizeof(m), "QWORD PTR [rbp-%zu]", spill); break;
      }
      emit_two_operand_line(
          "mov", reg_name(dst->reg->real_reg,
                          dst->reg->size ? dst->reg->size : SIZE_DWORD),
          m);
      return;
    }
    // Fallback to scratch load
    size_t spill2 = vreg_spill_offset(fx, sidx);
    emit_mov_load_spill(scratch_r10(src->reg->size), src->reg->size, spill2);
    emit_two_operand_line(
        "mov",
        reg_name(dst->reg->real_reg,
                 dst->reg->size ? dst->reg->size : SIZE_DWORD),
        scratch_r10(src->reg->size));
    return;
  }
  if (src->kind == OP_IMM)
  {
    snprintf(src_buf, sizeof(src_buf), "%lld", src->imm);
    src_text = src_buf;
  }
  else if (src_mem && dst_mem)
  {
    // For LEA, the source must be a memory addressing mode; do not preload.
    if (as->kind == X64_LEA)
    {
      char m[64];
      format_mem(m, sizeof(m), src, fx, as->insn_id);
      snprintf(src_buf, sizeof(src_buf), "%s", m);
      src_text = src_buf;
    }
    else
    {
      // load src to r10
      if (src->kind == OP_REG)
      {
        size_t spill = vreg_spill_offset(fx, get_vreg_index(src->reg));
        emit_mov_load_spill(scratch_r10(src->reg->size), src->reg->size, spill);
        src_text = scratch_r10(src->reg->size);
        src_in_scratch_r10 = true;
      }
      else if (src->kind == OP_MEM)
      {
        char m[64];
        format_mem(m, sizeof(m), src, fx, as->insn_id);
        // Detect unresolved base fallback (format_mem prints [rbp+disp]) and fix it
        bool unresolved_base = false;
        if (src->mem.base && src->mem.base->reg_type == virtual_regs)
        {
          size_t idx = get_vreg_index(src->mem.base);
          if (!vreg_has_real_at(fx, idx, as->insn_id)) unresolved_base = true;
        }
        OperandSize sz = src->mem.mov_size ? src->mem.mov_size : SIZE_QWORD;
        if (unresolved_base)
        {
          size_t base_spill = vreg_spill_offset(fx, get_vreg_index(src->mem.base));
          // Load spilled base into scratch then use it for the addressing
          output_file("    mov %s, QWORD PTR [rbp-%zu]", scratch_r10(SIZE_QWORD), base_spill);
          int disp = src->mem.displacement;
          char addr[64];
          snprintf(addr, sizeof(addr), "[%s%+d]", scratch_r10(SIZE_QWORD), disp);
          output_file("    mov %s, %s", scratch_r10(sz), addr);
        }
        else
        {
          output_file("    mov %s, %s", scratch_r10(sz), m);
        }
        src_text = scratch_r10(sz);
        src_in_scratch_r10 = true;
      }
    }
  }
  else if (src->kind == OP_REG && src->reg && src->reg->reg_type == virtual_regs)
  {
    // Honor fixed-register constraint for source operand
    if (src->constraint.has_fixed_register)
    {
      // Use operand's declared size if available; default to DWORD
      OperandSize ssz = src->reg ? src->reg->size : SIZE_DWORD;
      src_text = reg_name(src->constraint.fixed_register, ssz);
    }
    else
    {
      size_t idx = get_vreg_index(src->reg);
      if (vreg_has_real_at(fx, idx, as->insn_id))
        src_text = reg_name(vreg_real_at(fx, idx, as->insn_id), src->reg->size);
      else
      {
        if (src_is_spill_mem)
        {
          // Prefer direct memory source when legal
          bool allow_mem_src = false;
          switch (as->kind)
          {
            case X64_MOV: case X64_ADD: case X64_SUB: case X64_AND:
            case X64_OR: case X64_XOR: case X64_CMP:
              allow_mem_src = true; break;
            default: break;
          }
          if (allow_mem_src)
          {
            src_text = src_spill_buf;
          }
        }
        // Try rematerialization for immediates when legal
        size_t* is_imm = fx->vreg_is_remat_imm ? vector_peek_at(fx->vreg_is_remat_imm, idx) : NULL;
        long long* imm_valp = fx->vreg_remat_imm_value ? vector_peek_at(fx->vreg_remat_imm_value, idx) : NULL;
        bool allow_imm = false;
        switch (as->kind)
        {
          case X64_ADD: case X64_SUB: case X64_AND: case X64_OR:
          case X64_XOR: case X64_CMP: case X64_MOV:
            allow_imm = true; break;
          default: break;
        }
        if (allow_imm && is_imm && *is_imm && imm_valp)
        {
          snprintf(src_buf, sizeof(src_buf), "%lld", *imm_valp);
          src_text = src_buf;
        }
        else
        {
          // Try rematerialize LEA into scratch when available
          char addr2[64];
          if (remat_lea_address(fx, idx, 0, addr2, sizeof(addr2)))
          {
            output_file("    lea %s, %s", scratch_r10(SIZE_QWORD), addr2);
            src_text = scratch_r10(src->reg->size ? src->reg->size : SIZE_QWORD);
            src_in_scratch_r10 = true;
          }
          else
          {
          // Use memory source directly when legal to avoid extra scratch moves
          bool allow_mem_src = false;
          switch (as->kind)
          {
            case X64_MOV: case X64_ADD: case X64_SUB: case X64_AND:
            case X64_OR: case X64_XOR: case X64_CMP:
              allow_mem_src = true; break;
            default: break;
          }
          size_t spill = vreg_spill_offset(fx, idx);
          if (allow_mem_src && spill)
          {
            // We should already have src_is_spill_mem path; keep fallback here.
            switch (src->reg->size)
            {
              case SIZE_QWORD: snprintf(src_buf, sizeof(src_buf), "QWORD PTR [rbp-%zu]", spill); break;
              case SIZE_DWORD: snprintf(src_buf, sizeof(src_buf), "DWORD PTR [rbp-%zu]", spill); break;
              case SIZE_WORD: snprintf(src_buf, sizeof(src_buf), "WORD PTR [rbp-%zu]", spill); break;
              case SIZE_BYTE: snprintf(src_buf, sizeof(src_buf), "BYTE PTR [rbp-%zu]", spill); break;
              default: snprintf(src_buf, sizeof(src_buf), "QWORD PTR [rbp-%zu]", spill); break;
            }
            src_text = src_buf;
          }
          else
          {
            emit_mov_load_spill(scratch_r10(src->reg->size), src->reg->size, spill);
            src_text = scratch_r10(src->reg->size);
            src_in_scratch_r10 = true;
          }
          }
        }
    }
  }
  }
  else if (src->kind == OP_MEM || src->kind == OP_MEM_RELATIVE)
  {
    char m[64];
    format_mem(m, sizeof(m), src, fx, as->insn_id);
    // Detect unresolved base fallback (format_mem prints [rbp+disp]) and fix it
    bool unresolved_base = false;
    if (src->mem.base && src->mem.base->reg_type == virtual_regs)
    {
      size_t idx = get_vreg_index(src->mem.base);
      if (!vreg_has_real_at(fx, idx, as->insn_id)) unresolved_base = true;
    }
    if (unresolved_base)
    {
      size_t base_spill = vreg_spill_offset(fx, get_vreg_index(src->mem.base));
      output_file("    mov %s, QWORD PTR [rbp-%zu]", scratch_r10(SIZE_QWORD), base_spill);
      int disp = src->mem.displacement;
      snprintf(src_buf, sizeof(src_buf), "[%s%+d]", scratch_r10(SIZE_QWORD), disp);
      src_text = src_buf;
    }
    else
    {
      snprintf(src_buf, sizeof(src_buf), "%s", m);
      src_text = src_buf;
    }
  }
  else if (src->kind == OP_REG && src->reg && src->reg->reg_type == real_regs)
  {
    src_text = reg_name(src->reg->real_reg, src->reg->size);
  }

  // Build destination
  const char* dst_text = NULL;
  char dst_buf[128];
  bool store_back = false;
  OperandSize dst_sz = SIZE_QWORD;
  // Ops that overwrite dest fully (no need to preload when using scratch)
  bool overwrites_dest =
      (as->kind == X64_MOV || as->kind == X64_LEA || as->kind == X64_MOVSX ||
       as->kind == X64_MOVSXD || as->kind == X64_MOVZX);
  if (dst->kind == OP_REG)
  {
    dst_sz = dst->reg ? dst->reg->size : SIZE_DWORD;
    // fixed register constraint takes precedence
    if (dst->constraint.has_fixed_register)
    {
      dst_text = reg_name(dst->constraint.fixed_register, dst_sz);
    }
    else if (dst->reg && dst->reg->reg_type == virtual_regs)
    {
      size_t idx = get_vreg_index(dst->reg);
      if (vreg_has_real_at(fx, idx, as->insn_id))
        dst_text = reg_name(vreg_real_at(fx, idx, as->insn_id), dst_sz);
      else if (requires_reg_dest(as->kind) || as->kind == X64_MOV)
      {
        dst_text = scratch_r11(dst_sz);
        store_back = true;
      }
      else
      {
        size_t spill = vreg_spill_offset(fx, idx);
        switch (dst_sz)
        {
          case SIZE_QWORD:
            snprintf(dst_buf, sizeof(dst_buf), "QWORD PTR [rbp-%zu]", spill);
            break;
          case SIZE_DWORD:
            snprintf(dst_buf, sizeof(dst_buf), "DWORD PTR [rbp-%zu]", spill);
            break;
          case SIZE_WORD:
            snprintf(dst_buf, sizeof(dst_buf), "WORD PTR [rbp-%zu]", spill);
            break;
          case SIZE_BYTE:
            snprintf(dst_buf, sizeof(dst_buf), "BYTE PTR [rbp-%zu]", spill);
            break;
          default:
            snprintf(dst_buf, sizeof(dst_buf), "QWORD PTR [rbp-%zu]", spill);
            break;
        }
        dst_text = dst_buf;
      }
    }
    else if (dst->reg)
    {
      dst_text = reg_name(dst->reg->real_reg, dst_sz);
    }
    // else: leave dst_text NULL; later code paths will handle via store_back
  }
  else if (dst->kind == OP_MEM)
  {
    dst_sz = dst->mem.mov_size ? dst->mem.mov_size : SIZE_QWORD;
    char m[64];
    format_mem(m, sizeof(m), dst, fx, as->insn_id);
    // Use sized memory operand for MOV to avoid ambiguity
    bool unresolved_base = false;
    if (dst->mem.base && dst->mem.base->reg_type == virtual_regs)
    {
      size_t idx = get_vreg_index(dst->mem.base);
      if (!vreg_has_real_at(fx, idx, as->insn_id)) unresolved_base = true;
    }
    if (unresolved_base)
    {
      size_t base_spill = vreg_spill_offset(fx, get_vreg_index(dst->mem.base));
      // Load spilled base into r11 (separate from r10 used for src)
      output_file("    mov %s, QWORD PTR [rbp-%zu]", scratch_r11(SIZE_QWORD), base_spill);
      int disp = dst->mem.displacement;
      char addr[64];
      snprintf(addr, sizeof(addr), "[%s%+d]", scratch_r11(SIZE_QWORD), disp);
      if (as->kind == X64_MOV)
      {
        switch (dst_sz)
        {
          case SIZE_QWORD: snprintf(dst_buf, sizeof(dst_buf), "QWORD PTR %s", addr); break;
          case SIZE_DWORD: snprintf(dst_buf, sizeof(dst_buf), "DWORD PTR %s", addr); break;
          case SIZE_WORD: snprintf(dst_buf, sizeof(dst_buf), "WORD PTR %s", addr); break;
          case SIZE_BYTE: snprintf(dst_buf, sizeof(dst_buf), "BYTE PTR %s", addr); break;
          default: snprintf(dst_buf, sizeof(dst_buf), "%s", addr); break;
        }
      }
      else
      {
        snprintf(dst_buf, sizeof(dst_buf), "%s", addr);
      }
    }
    else
    {
      if (as->kind == X64_MOV)
      {
        switch (dst_sz)
        {
          case SIZE_QWORD: snprintf(dst_buf, sizeof(dst_buf), "QWORD PTR %s", m); break;
          case SIZE_DWORD: snprintf(dst_buf, sizeof(dst_buf), "DWORD PTR %s", m); break;
          case SIZE_WORD: snprintf(dst_buf, sizeof(dst_buf), "WORD PTR %s", m); break;
          case SIZE_BYTE: snprintf(dst_buf, sizeof(dst_buf), "BYTE PTR %s", m); break;
          default: snprintf(dst_buf, sizeof(dst_buf), "%s", m); break;
        }
      }
      else
      {
        snprintf(dst_buf, sizeof(dst_buf), "%s", m);
      }
    }
    dst_text = dst_buf;
  }

  // Ensure register-register operand sizes match when required.
  // Many two-operand x64 ops require both operands be the same size (e.g., add/sub/and/or/xor/cmp/shift/imul).
  // Use the destination-chosen size for the source register operand in these cases.
  if (src->kind == OP_REG)
  {
    bool must_match_sizes = true;
    switch (as->kind)
    {
      case X64_MOVSX:
      case X64_MOVSXD:
      case X64_MOVZX:
      case X64_LEA:
        must_match_sizes = false;  // source size semantics differ
        break;
      default:
        break;
    }
    if (must_match_sizes)
    {
      if (src->reg && src->reg->reg_type == virtual_regs)
      {
        size_t idx = get_vreg_index(src->reg);
      if (vreg_has_real_at(fx, idx, as->insn_id))
      {
        // Reformat src_text using the same hardware register in dst size
        src_text = reg_name(vreg_real_at(fx, idx, as->insn_id), dst_sz);
      }
      else if (!src_in_scratch_r10 && dst_sz == SIZE_QWORD &&
               src->reg->size != SIZE_QWORD)
      {
        size_t spill = vreg_spill_offset(fx, idx);
        switch (src->reg->size)
        {
          case SIZE_DWORD:
            output_file("    movsxd %s, DWORD PTR [rbp-%zu]",
                        scratch_r10(SIZE_QWORD), spill);
            break;
          case SIZE_WORD:
            output_file("    movsx %s, WORD PTR [rbp-%zu]",
                        scratch_r10(SIZE_QWORD), spill);
            break;
          case SIZE_BYTE:
            output_file("    movsx %s, BYTE PTR [rbp-%zu]",
                        scratch_r10(SIZE_QWORD), spill);
            break;
          default:
            emit_mov_load_spill(scratch_r10(src->reg->size), src->reg->size,
                                spill);
            break;
        }
        src_text = scratch_r10(SIZE_QWORD);
        src_in_scratch_r10 = true;
      }
      else if (src_in_scratch_r10)
      {
        // For spilled vregs we loaded into r10 with their natural size; use
        // the dst size view of r10 to satisfy instruction operand size rules.
        src_text = scratch_r10(dst_sz);
      }
      }
      else if (src->reg && src->reg->reg_type == real_regs)
      {
        src_text = reg_name(src->reg->real_reg, dst_sz);
      }
    }
  }

  // If dst is memory and src is memory, we've already loaded src to r10
  // If we are using a scratch destination that represents a spilled vreg and
  // the operation reads the previous destination value, preload it now.
  // If destination is a 64-bit memory location and the source is a 32-bit
  // real register, move the source into a 64-bit scratch to satisfy operand sizes.
  bool src_is_spilled_vreg = false;
  if (src->kind == OP_REG && src->reg && src->reg->reg_type == virtual_regs)
  {
    size_t idx = get_vreg_index(src->reg);
    src_is_spilled_vreg = !vreg_has_real_at(fx, idx, as->insn_id);
  }
  if ((dst->kind == OP_MEM) && src_text && dst_sz == SIZE_QWORD && !src_is_spilled_vreg &&
      (src->kind == OP_REG && src->reg && (src->reg->size == SIZE_DWORD)))
  {
    // Zero-extend into r10 by writing its 32-bit low part. Always use the
    // 32-bit view of the source hardware register regardless of prior sizing.
    enum register_name hw;
    if (src->reg->reg_type == virtual_regs)
      hw = vreg_real_at(fx, get_vreg_index(src->reg), as->insn_id);
    else
      hw = src->reg->real_reg;
    output_file("    mov %s, %s", scratch_r10(SIZE_DWORD),
                reg_name(hw, SIZE_DWORD));
    src_text = scratch_r10(SIZE_QWORD);
  }
  if (store_back && !overwrites_dest && dst->kind == OP_REG &&
      dst->reg && dst->reg->reg_type == virtual_regs)
  {
    size_t spill = vreg_spill_offset(fx, get_vreg_index(dst->reg));
    emit_mov_load_spill(dst_text, dst_sz, spill);
  }

  if (as->kind == X64_MOVZX && dst_sz == SIZE_QWORD && src->kind == OP_REG &&
      src->reg && src->reg->size == SIZE_DWORD)
  {
    const char* zext_dst = NULL;
    const char* zext_src = NULL;
    if (dst->constraint.has_fixed_register)
      zext_dst = reg_name(dst->constraint.fixed_register, SIZE_DWORD);
    else if (dst->reg && dst->reg->reg_type == virtual_regs)
    {
      size_t idx = get_vreg_index(dst->reg);
      if (vreg_has_real_at(fx, idx, as->insn_id))
        zext_dst = reg_name(vreg_real_at(fx, idx, as->insn_id), SIZE_DWORD);
      else
        zext_dst = scratch_r11(SIZE_DWORD);
    }
    else if (dst->reg)
      zext_dst = reg_name(dst->reg->real_reg, SIZE_DWORD);

    if (src->reg->reg_type == virtual_regs)
    {
      size_t idx = get_vreg_index(src->reg);
      if (vreg_has_real_at(fx, idx, as->insn_id))
        zext_src = reg_name(vreg_real_at(fx, idx, as->insn_id), SIZE_DWORD);
      else if (src_is_spill_mem)
        zext_src = src_spill_buf;
    }
    else
      zext_src = reg_name(src->reg->real_reg, SIZE_DWORD);

    if (zext_dst && zext_src)
    {
      output_file("    mov %s, %s", zext_dst, zext_src);
      if (store_back)
      {
        size_t spill = vreg_spill_offset(fx, get_vreg_index(dst->reg));
        emit_mov_store_spill(scratch_r11(SIZE_QWORD), SIZE_QWORD, spill);
      }
      return;
    }
  }

  // Final fallback: ensure src_text is resolved
  if (!src_text)
  {
    if (src->kind == OP_REG && src->reg)
    {
      if (src->reg->reg_type == virtual_regs)
      {
        size_t idx = get_vreg_index(src->reg);
        if (vreg_has_real_at(fx, idx, as->insn_id))
          src_text = reg_name(vreg_real_at(fx, idx, as->insn_id), src->reg->size);
        else
        {
          size_t spill = vreg_spill_offset(fx, idx);
          emit_mov_load_spill(scratch_r10(src->reg->size), src->reg->size, spill);
          src_text = scratch_r10(src->reg->size);
          src_in_scratch_r10 = true;
        }
      }
      else
      {
        src_text = reg_name(src->reg->real_reg, src->reg->size);
      }
    }
    else if (src->kind == OP_MEM || src->kind == OP_MEM_RELATIVE)
    {
      char m[64];
      format_mem(m, sizeof(m), src, fx, as->insn_id);
      snprintf(src_buf, sizeof(src_buf), "%s", m);
      src_text = src_buf;
    }
  }
  // Normalize shift count: x86-64 variable shifts use CL
  bool is_shift = (as->kind == X64_SHL || as->kind == X64_SHR ||
                   as->kind == X64_SAL || as->kind == X64_SAR);
  if (is_shift && src->kind != OP_IMM)
  {
    // Load count into ECX and use CL as the count operand
    if (src->kind == OP_REG && src->reg)
    {
      enum register_name hw;
      if (src->reg->reg_type == virtual_regs)
      {
        size_t idx = get_vreg_index(src->reg);
        if (vreg_has_real_at(fx, idx, as->insn_id))
        {
          hw = vreg_real_at(fx, idx, as->insn_id);
          output_file("    mov ecx, %s", reg_name(hw, SIZE_DWORD));
        }
        else
        {
          size_t spill = vreg_spill_offset(fx, idx);
          output_file("    mov ecx, DWORD PTR [rbp-%zu]", spill);
        }
      }
      else
      {
        hw = src->reg->real_reg;
        output_file("    mov ecx, %s", reg_name(hw, SIZE_DWORD));
      }
    }
    else if (src->kind == OP_MEM || src->kind == OP_MEM_RELATIVE)
    {
      char m[64];
      format_mem(m, sizeof(m), src, fx, as->insn_id);
      output_file("    mov ecx, DWORD PTR %s", m);
    }
    src_text = "cl";
  }
  emit_two_operand_line(mnem, dst_text, src_text);

  if (store_back)
  {
    size_t spill = vreg_spill_offset(fx, get_vreg_index(dst->reg));
    emit_mov_store_spill(dst_text, dst_sz, spill);
  }
}

static void emit_unary(const char* mnem, X64_ASM* as, X64_FUNC* fx)
{
  X64_Operand* dst = &as->operands[0];
  const char* dst_text = NULL;
  char buf[128];
  bool store_back = false;
  // SETcc always writes a byte; override operand size
  bool is_setcc = (as->kind == X64_SETE || as->kind == X64_SETNE ||
                   as->kind == X64_SETL || as->kind == X64_SETB ||
                   as->kind == X64_SETLE || as->kind == X64_SETBE);
  OperandSize op_sz = is_setcc ? SIZE_BYTE :
                                (dst->kind == OP_REG ? (dst->reg ? dst->reg->size : SIZE_DWORD) : SIZE_QWORD);

  if (dst->kind == OP_REG && dst->reg && dst->reg->reg_type == virtual_regs)
  {
    size_t idx = get_vreg_index(dst->reg);
    if (vreg_has_real_at(fx, idx, as->insn_id))
      dst_text = reg_name(vreg_real_at(fx, idx, as->insn_id), op_sz);
    else
    {
      if (dst->constraint.require_register || requires_reg_dest(as->kind) || is_setcc)
      {
        // Use scratch and store back
        dst_text = scratch_r11(op_sz);
        store_back = true;
      }
      else
      {
        size_t spill = vreg_spill_offset(fx, idx);
        // Use sized memory operand if not setcc
        if (is_setcc)
          snprintf(buf, sizeof(buf), "BYTE PTR [rbp-%zu]", spill);
        else
          snprintf(buf, sizeof(buf), "QWORD PTR [rbp-%zu]", spill);
        dst_text = buf;
      }
    }
  }
  else if (dst->kind == OP_REG && dst->reg && dst->reg->reg_type == real_regs)
  {
    dst_text = reg_name(dst->reg->real_reg, op_sz);
  }
  else if (dst->kind == OP_MEM)
  {
    char m[128];
    format_mem(m, sizeof(m), dst, fx, as->insn_id);
    if (is_setcc)
    {
      output_file("    %s BYTE PTR %s", mnem, m);
      return;
    }
    else
    {
      snprintf(buf, sizeof(buf), "%s", m);
      dst_text = buf;
    }
  }
  // Preload spilled destination into scratch for read-modify unary ops
  if (store_back && !is_setcc)
  {
    size_t spill = vreg_spill_offset(fx, get_vreg_index(dst->reg));
    emit_mov_load_spill(dst_text, op_sz, spill);
  }
  if (!dst_text)
  {
    // Final fallback: operate on scratch to avoid NULL output
    dst_text = scratch_r11(op_sz);
  }
  emit_one_operand_line(mnem, dst_text);

  // For SETcc into a spilled virtual, zero-extend the byte to full width
  if (is_setcc)
  {
    // Zero-extend the setcc byte result into the full destination width
    enum register_name r = r11;
    OperandSize zext_dst = (dst->kind == OP_REG ? (dst->reg ? dst->reg->size : SIZE_DWORD) : SIZE_DWORD);
    if (dst->kind == OP_REG)
    {
      if (dst->constraint.has_fixed_register)
        r = dst->constraint.fixed_register;
      else if (dst->reg && dst->reg->reg_type == real_regs)
        r = dst->reg->real_reg;
      else if (dst->reg && dst->reg->reg_type == virtual_regs)
      {
        size_t idx = get_vreg_index(dst->reg);
        if (vreg_has_real_at(fx, idx, as->insn_id))
          r = vreg_real_at(fx, idx, as->insn_id);
        else
          r = r11;  // scratch path already used for setcc
      }
    }
    if (zext_dst > SIZE_BYTE)
      output_file("    movzx %s, %s", reg_name(r, zext_dst), reg_name(r, SIZE_BYTE));
  }

  if (store_back && dst->kind == OP_REG && dst->reg && dst->reg->reg_type == virtual_regs)
  {
    size_t spill = vreg_spill_offset(fx, get_vreg_index(dst->reg));
    // For SETcc results, store zero-extended width (e.g., 32-bit) to avoid
    // later mismatches like cmp dword ptr [spill], 0 after a byte store.
    OperandSize store_sz = op_sz;
    if (is_setcc)
      store_sz = (dst->kind == OP_REG ? dst->reg->size : SIZE_DWORD);
    if (is_setcc)
    {
      // After setcc we emitted movzx into the same register; use the widened
      // name for the store (scratch r11 path).
      emit_mov_store_spill(reg_name(r11, store_sz), store_sz, spill);
    }
    else
    {
      emit_mov_store_spill(dst_text, store_sz, spill);
    }
  }
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

static size_t count_callee_saved(unsigned int used_mask)
{
  size_t cnt = 0;
  if (used_mask & (1 << rbx)) cnt++;
  if (used_mask & (1 << r12)) cnt++;
  if (used_mask & (1 << r13)) cnt++;
  if (used_mask & (1 << r14)) cnt++;
  if (used_mask & (1 << r15)) cnt++;
  return cnt;
}

static void emit_prologue(IRFunc* func_ir, X64_FUNC* func_x64, size_t* out_alloc)
{
  output_file("    push rbp");
  output_file("    mov rbp, rsp");

  size_t locals = func_ir->user_defined.stack_size;
  size_t spills = func_x64->spill_stack_size;
  size_t base = locals + spills;
  // Reserve local/spill space directly below RBP so [rbp-offsetof] never
  // overlaps saved callee-saved registers. Then push the callee-saved
  // registers beneath that area.
  //
  // Align so that RSP is 16B-aligned before calls.
  // Entry: RSP%16==8; after push rbp -> 0; after sub alloc -> (-alloc)%16;
  // after k pushes -> (-alloc - 8*k)%16.
  size_t k = count_callee_saved(func_x64->used_registers);
  size_t required_mod = (k % 2) ? 8 : 0;
  size_t padding = (16 + required_mod - (base % 16)) % 16;
  size_t alloc = base + padding;
  if (alloc)
    output_file("    sub rsp, %zu", alloc);
  // Save callee-saved registers used
  if (func_x64->used_registers & (1 << rbx)) output_file("    push rbx");
  if (func_x64->used_registers & (1 << r12)) output_file("    push r12");
  if (func_x64->used_registers & (1 << r13)) output_file("    push r13");
  if (func_x64->used_registers & (1 << r14)) output_file("    push r14");
  if (func_x64->used_registers & (1 << r15)) output_file("    push r15");
  if (out_alloc) *out_alloc = alloc;
}

static void emit_epilogue(X64_FUNC* func_x64)
{
  // Restore callee-saved registers in reverse order
  if (func_x64->used_registers & (1 << r15)) output_file("    pop r15");
  if (func_x64->used_registers & (1 << r14)) output_file("    pop r14");
  if (func_x64->used_registers & (1 << r13)) output_file("    pop r13");
  if (func_x64->used_registers & (1 << r12)) output_file("    pop r12");
  if (func_x64->used_registers & (1 << rbx)) output_file("    pop rbx");
  output_file("    mov rsp, rbp");
  output_file("    pop rbp");
  output_file("    ret");
}

void x64_emit_function(IRFunc* func_ir, X64_FUNC* func_x64)
{
  emit_function_header(func_ir);
  size_t alloc = 0;
  emit_prologue(func_ir, func_x64, &alloc);
  // Default return value for main: C specifies reaching end of main returns 0
  if (func_ir->user_defined.function_name_size == 4 &&
      strncmp(func_ir->user_defined.function_name, "main", 4) == 0)
  {
    output_file("    mov eax, 0");
  }
  // Create a unique return label per function to support early returns
  char ret_label[128];
  snprintf(ret_label, sizeof(ret_label), ".Lret_%.*s",
           (int)func_ir->user_defined.function_name_size,
           func_ir->user_defined.function_name);

  // Emit MIR body
  bool saw_explicit_return = false;
  for (size_t bi = 1; bi <= vector_size(func_x64->asm_blocks); bi++)
  {
    X64_Blocks* blk = vector_peek_at(func_x64->asm_blocks, bi);
    for (size_t ji = 1; ji <= vector_size(blk->asm_list); ji++)
    {
      X64_ASM* as = vector_peek_at(blk->asm_list, ji);
      sync_split_vregs_before_insn(func_x64, as->insn_id);
      // Debug: print MIR instruction kind before emission to help diagnose crashes
      output_file("# x64 kind=%d", (int)as->kind);
      switch (as->kind)
      {
        case X64_LABEL:
          output_file("%s:", as->jump_target_label);
          break;
        case X64_MOV: emit_two_op("mov", as, func_x64); break;
        case X64_MOVSX: emit_two_op("movsx", as, func_x64); break;
        case X64_MOVSXD: emit_two_op("movsxd", as, func_x64); break;
        case X64_MOVZX: emit_two_op("movzx", as, func_x64); break;
        case X64_LEA: emit_two_op("lea", as, func_x64); break;
        case X64_ADD: emit_two_op("add", as, func_x64); break;
        case X64_SUB: emit_two_op("sub", as, func_x64); break;
        case X64_AND: emit_two_op("and", as, func_x64); break;
        case X64_OR: emit_two_op("or", as, func_x64); break;
        case X64_XOR: emit_two_op("xor", as, func_x64); break;
        case X64_SHL: emit_two_op("shl", as, func_x64); break;
        case X64_SHR: emit_two_op("shr", as, func_x64); break;
        case X64_SAL: emit_two_op("sal", as, func_x64); break;
        case X64_SAR: emit_two_op("sar", as, func_x64); break;
        case X64_IMUL: emit_two_op("imul", as, func_x64); break;
        case X64_MUL: emit_two_op("mul", as, func_x64); break;
        case X64_NEG: emit_unary("neg", as, func_x64); break;
        case X64_NOT: emit_unary("not", as, func_x64); break;
        case X64_CMP: emit_two_op("cmp", as, func_x64); break;
        case X64_SETE: emit_unary("sete", as, func_x64); break;
        case X64_SETNE: emit_unary("setne", as, func_x64); break;
        case X64_SETL: emit_unary("setl", as, func_x64); break;
        case X64_SETB: emit_unary("setb", as, func_x64); break;
        case X64_SETLE: emit_unary("setle", as, func_x64); break;
        case X64_SETBE: emit_unary("setbe", as, func_x64); break;
        case X64_JMP: output_file("    jmp %s", as->jump_target_label); break;
        case X64_JE: output_file("    je %s", as->jump_target_label); break;
        case X64_JNE: output_file("    jne %s", as->jump_target_label); break;
        case X64_JZ: output_file("    jz %s", as->jump_target_label); break;
        case X64_JNG: output_file("    jng %s", as->jump_target_label); break;
        case X64_JNGE: output_file("    jnge %s", as->jump_target_label); break;
        case X64_CQO: output_file("    cqo"); break;
        case X64_CDQ: output_file("    cdq"); break;
        case X64_IDIV:
        case X64_DIV:
        {
          // operand 0 is divisor; rax/rdx already prepared by MIR sequence
          if (as->operands[0].kind == OP_REG &&
              as->operands[0].reg->reg_type == virtual_regs)
          {
            size_t idx = get_vreg_index(as->operands[0].reg);
            if (vreg_has_real_at(func_x64, idx, as->insn_id))
              emit_div_operand_line(
                  as->kind == X64_DIV,
                  reg_name(vreg_real_at(func_x64, idx, as->insn_id),
                           as->operands[0].reg->size));
            else
            {
              size_t spill = vreg_spill_offset(func_x64, idx);
              emit_mov_load_spill(scratch_r10(as->operands[0].reg->size),
                                  as->operands[0].reg->size, spill);
              emit_div_operand_line(as->kind == X64_DIV,
                                    scratch_r10(as->operands[0].reg->size));
            }
          }
          else if (as->operands[0].kind == OP_MEM)
          {
            char m[64];
            format_mem(m, sizeof(m), &as->operands[0], func_x64, as->insn_id);
            emit_div_operand_line(as->kind == X64_DIV, m);
          }
          else if (as->operands[0].kind == OP_REG &&
                   as->operands[0].reg->reg_type == real_regs)
          {
            emit_div_operand_line(as->kind == X64_DIV,
                                  reg_name(as->operands[0].reg->real_reg,
                                           as->operands[0].reg->size));
          }
          break;
        }
        case X64_CALL:
        {
          // Before the call, flush any vregs whose allocations were split at
          // this call boundary into their spill slots so that later uses can
          // reload correct values.
          size_t nregs = vector_size(func_x64->virtual_regs);
          for (size_t vi = 1; vi <= nregs; vi++)
          {
            size_t* realp = vector_peek_at(func_x64->vreg_real, vi);
            size_t* rendp = func_x64->vreg_real_end ? vector_peek_at(func_x64->vreg_real_end, vi) : NULL;
            size_t* slotp = vector_peek_at(func_x64->vreg_stack_slot, vi);
            X64_REG* vinfo = vector_peek_at(func_x64->virtual_regs, vi);
            if (!realp || !rendp || !slotp || !vinfo) continue;
            enum register_name r = (enum register_name)(*realp);
            size_t rend = *rendp;
            size_t slot = *slotp;
            if (r != register_reserved && rend != 0 && rend <= as->insn_id && slot)
            {
              emit_mov_store_spill(reg_name(r, vinfo->size ? vinfo->size : SIZE_QWORD),
                                   vinfo->size ? vinfo->size : SIZE_QWORD, slot);
            }
          }
          output_file("    call %s", as->jump_target_label);
          break;
        }
        case X64_BUILTIN_ASM:
          output_file("    %.*s", (int)as->builtin_asm.asm_len,
                      as->builtin_asm.asm_str);
          break;
        case X64_RETURN:
          // Virtual return: jump to function epilogue label
          saw_explicit_return = true;
          output_file("    jmp %s", ret_label);
          break;
        case X64_RET:
        case X64_PUSH:
        case X64_POP:
        case X64_LEAVE:
        case X64_UNUSED:
          break;
      }
    }
  }
  // If main falls through without an explicit return, ensure we return 0
  if (!saw_explicit_return &&
      func_ir->user_defined.function_name_size == 4 &&
      strncmp(func_ir->user_defined.function_name, "main", 4) == 0)
  {
    output_file("    mov eax, 0");
  }
  // Function epilogue label target for early returns
  output_file("%s:", ret_label);
  emit_epilogue(func_x64);
}
