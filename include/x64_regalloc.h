#ifndef X64_REGALLOC_C_COMPILER
#define X64_REGALLOC_C_COMPILER

#include "common.h"
#include "generator_x64.h"

// Constraint-aware linear scan (scaffold; no splitting yet)
void x64_regalloc(IRFunc* func_ir, X64_FUNC* func_x64);

#endif

