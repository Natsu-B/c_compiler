#ifndef X64_LIVENESS_C_COMPILER
#define X64_LIVENESS_C_COMPILER

#include "common.h"
#include "generator_x64.h"

// Assign sequential instruction IDs within the function's MIR blocks
void x64_number_program_points(X64_FUNC* func_x64);

// Build MIR-level liveness (block-level in/out) using IR CFG and X64 operands
void x64_build_mir_liveness(IRFunc* func_ir, X64_FUNC* func_x64);

// Debug: dump MIR CFG, block in/out sets, and instruction IDs
void x64_dump_mir_cfg(IRFunc* func_ir, X64_FUNC* func_x64);

#endif
