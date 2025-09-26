#ifndef X64_REGALLOC_C_COMPILER
#define X64_REGALLOC_C_COMPILER

#include "common.h"
#include "generator_x64.h"

// Constraint-aware linear scan with interval splitting, caller-boundary
// splits, copy coalescing, and rematerialization (Phase 2)
void x64_regalloc(IRFunc* func_ir, X64_FUNC* func_x64);

#endif
