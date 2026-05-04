#ifndef X64_OPT_C_COMPILER
#define X64_OPT_C_COMPILER

#include "generator_x64.h"

// Simple MIR peephole optimizations applied after regalloc:
// - Remove redundant moves between a virtual register and the same fixed real
//   register it was assigned to (e.g., vreg->rdi when vreg is in rdi).
// - Remove redundant moves from a fixed real register into a virtual register
//   that is assigned to the same real register (e.g., rax->vreg in rax).
void x64_peephole_optimize(X64_FUNC* func_x64);

// Remove instructions marked X64_UNUSED from blocks to shrink the final MIR
// stream before validation and emission.
void x64_compact_unused(X64_FUNC* func_x64);

#endif
