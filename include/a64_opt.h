#ifndef A64_OPT_C_COMPILER
#define A64_OPT_C_COMPILER

#include "common.h"
#include "generator_aarch64.h"

void a64_peephole_optimize(A64_FUNC* func);
void a64_compact_unused(A64_FUNC* func);

#endif // A64_OPT_C_COMPILER

