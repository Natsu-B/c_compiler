#ifndef A64_LIVENESS_C_COMPILER
#define A64_LIVENESS_C_COMPILER

#include "common.h"
#include "generator_aarch64.h"

void a64_number_program_points(A64_FUNC* func);
void a64_build_mir_liveness(IRFunc* func_ir, A64_FUNC* func);
void a64_dump_mir_cfg(IRFunc* func_ir, A64_FUNC* func);

#endif // A64_LIVENESS_C_COMPILER

